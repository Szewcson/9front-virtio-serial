/* SPDX-License-Identifier: MIT */
#if !defined(QGA_HOST_TEST) && !defined(VCONSOLE_USER_TEST) && !defined(QGA_VCONSOLE_KERNEL)
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#define QGA_VCONSOLE_KERNEL
#endif
#include "vconsole-mmio.h"

static int
mmvalid(const VcMmio *m)
{
	int i;
	static const int minimum[VcRegionMax] = {56, 2, 1, 8};

	if(m == nil || m->state < VcMmMapped || m->state > VcMmFault || m->epoch == 0)
		return 0;
	for(i = 0; i < VcRegionMax; i++)
		if(m->map[i] == nil || m->length[i] < minimum[i] || m->length[i] > VcMmMapMax)
			return 0;
	return m->ops.read != nil && m->ops.write != nil && m->ops.fence != nil &&
		m->ops.pause != nil && m->ops.unmap != nil;
}

static int
readreg(VcMmio *m, int region, int offset, int width, Vcu32 *value)
{
	int r;

	if(region < 0 || region >= VcRegionMax || offset < 0 ||
	   (width != 1 && width != 2 && width != 4) ||
	   (offset&(width-1)) != 0 || offset > m->length[region]-width)
		return VcInvalid;
	*value = 0;
	m->ops.fence(m->aux);
	r = m->ops.read(m->aux, m->map[region], offset, width, value);
	m->ops.fence(m->aux);
	if(r != 0 || (width == 1 && *value > 255) || (width == 2 && *value > 65535))
		return VcFault;
	return 0;
}

static int
writereg(VcMmio *m, int region, int offset, int width, Vcu32 value)
{
	int r;

	if(region < 0 || region >= VcRegionMax || offset < 0 ||
	   (width != 1 && width != 2 && width != 4) ||
	   (offset&(width-1)) != 0 || offset > m->length[region]-width)
		return VcInvalid;
	m->ops.fence(m->aux);
	r = m->ops.write(m->aux, m->map[region], offset, width, value);
	m->ops.fence(m->aux);
	return r == 0 ? 0 : VcFault;
}

static int
mmfault(VcMmio *m)
{
	Vcu32 status;

	m->state = VcMmFault;
	/* Best effort only: never mistake FAILED for confirmed reset/quiescence. */
	if(readreg(m, VcMmCommon, VcRegStatus, 1, &status) == 0 && status != 255)
		writereg(m, VcMmCommon, VcRegStatus, 1, status|VcFailed);
	return VcFault;
}

int
vcmminit(VcMmio *m, const VcMmOps *ops, void *aux)
{
	if(m == nil || ops == nil || ops->map == nil || ops->unmap == nil ||
	   ops->read == nil || ops->write == nil || ops->fence == nil || ops->pause == nil)
		return VcInvalid;
	memset(m, 0, sizeof *m);
	m->ops = *ops;
	m->aux = aux;
	m->epoch = 1;
	return 0;
}

int
vcmmmap(VcMmio *m, const Vcu8 *cfg, int bytes, const VcBar *bars)
{
	VcPci pci;
	void *maps[VcRegionMax];
	int lengths[VcRegionMax], i;

	if(m == nil || m->state != VcMmEmpty || m->epoch == 0 ||
	   m->ops.map == nil || m->ops.unmap == nil ||
	   vcpci(&pci, cfg, bytes, bars) < 0)
		return VcInvalid;
	for(i = 0; i < VcRegionMax; i++)
		if(m->map[i] != nil) return VcInvalid;
	lengths[0] = 56;
	lengths[1] = pci.region[1].length < VcMmMapMax ? pci.region[1].length : VcMmMapMax;
	lengths[2] = 1;
	lengths[3] = 8;
	for(i = 0; i < VcRegionMax; i++){
		maps[i] = m->ops.map(m->aux, pci.region[i].address, lengths[i]);
		if(maps[i] == nil){
			while(i-- > 0) m->ops.unmap(m->aux, maps[i], lengths[i]);
			return VcFault;
		}
	}
	m->pci = pci;
	memmove(m->map, maps, sizeof maps);
	memmove(m->length, lengths, sizeof lengths);
	m->state = VcMmMapped;
	return 0;
}

int
vcmmreset(VcMmio *m)
{
	Vcu32 status;
	int i;

	if(!mmvalid(m) || m->epoch == ~(Vcu64)0) return VcInvalid;
	if(writereg(m, VcMmCommon, VcRegStatus, 1, 0) < 0) return mmfault(m);
	for(i = 0; i < VcMmResetPolls; i++){
		if(readreg(m, VcMmCommon, VcRegStatus, 1, &status) < 0) return mmfault(m);
		if(status == 0){
			m->epoch++;
			m->ports = m->queues = 0;
			memset(m->enabled, 0, sizeof m->enabled);
			memset(m->configured, 0, sizeof m->configured);
			m->state = VcMmReset;
			return 0;
		}
		if(i+1 < VcMmResetPolls) m->ops.pause(m->aux);
	}
	return mmfault(m);
}

static int
statusis(VcMmio *m, Vcu32 expected)
{
	Vcu32 status;

	return readreg(m, VcMmCommon, VcRegStatus, 1, &status) == 0 && status == expected;
}

int
vcmmnegotiate(VcMmio *m)
{
	Vcu32 low, high, before, after, ports, queues;
	int i;

	if(!mmvalid(m) || m->state != VcMmReset) return VcInvalid;
	if(!statusis(m, 0) ||
	   writereg(m, VcMmCommon, VcRegStatus, 1, VcAck) < 0 || !statusis(m, VcAck) ||
	   writereg(m, VcMmCommon, VcRegStatus, 1, VcAck|VcDriver) < 0 || !statusis(m, VcAck|VcDriver))
		return mmfault(m);
	if(writereg(m, VcMmCommon, VcRegDevSelect, 4, 0) < 0 ||
	   readreg(m, VcMmCommon, VcRegDevFeature, 4, &low) < 0 ||
	   writereg(m, VcMmCommon, VcRegDevSelect, 4, 1) < 0 ||
	   readreg(m, VcMmCommon, VcRegDevFeature, 4, &high) < 0 ||
	   !(low&2) || !(high&1)) /* MULTIPORT and VERSION_1 only. */
		return mmfault(m);
	if(writereg(m, VcMmCommon, VcRegDrvSelect, 4, 0) < 0 ||
	   writereg(m, VcMmCommon, VcRegDrvFeature, 4, 2) < 0 ||
	   writereg(m, VcMmCommon, VcRegDrvSelect, 4, 1) < 0 ||
	   writereg(m, VcMmCommon, VcRegDrvFeature, 4, 1) < 0 ||
	   writereg(m, VcMmCommon, VcRegStatus, 1, VcAck|VcDriver|VcFeaturesOk) < 0 ||
	   !statusis(m, VcAck|VcDriver|VcFeaturesOk))
		return mmfault(m);
	/* INTx-only: the PCI owner rejects enabled MSI/MSI-X before mapping. */
	if(writereg(m, VcMmCommon, VcRegConfigVector, 2, 65535) < 0 ||
	   readreg(m, VcMmCommon, VcRegQueues, 2, &queues) < 0)
		return mmfault(m);
	for(i = 0; i < VcMmConfigTries; i++){
		if(readreg(m, VcMmCommon, VcRegGeneration, 1, &before) < 0 ||
		   readreg(m, VcMmDevice, 4, 4, &ports) < 0 ||
		   readreg(m, VcMmCommon, VcRegGeneration, 1, &after) < 0)
			return mmfault(m);
		if(before == after) break;
	}
	if(i == VcMmConfigTries || ports < 1 || ports > VcPortMax ||
	   queues < 2*ports+2 || !statusis(m, VcAck|VcDriver|VcFeaturesOk))
		return mmfault(m);
	m->ports = ports;
	m->queues = queues;
	m->state = VcMmFeatures;
	return 0;
}

int
vcmmqueue(VcMmio *m, int id, VcQueueInfo *out)
{
	VcQueueInfo result;
	Vcu32 size, enable, notify;
	Vcu64 address, offset;

	if(!mmvalid(m) || m->state != VcMmFeatures || out == nil || id < 0 ||
	   m->ports < 1 || m->ports > VcPortMax || id >= 2*m->ports+2 || id >= m->queues)
		return VcInvalid;
	if(!statusis(m, VcAck|VcDriver|VcFeaturesOk) ||
	   writereg(m, VcMmCommon, VcRegQueueSelect, 2, id) < 0 ||
	   readreg(m, VcMmCommon, VcRegQueueSize, 2, &size) < 0 ||
	   readreg(m, VcMmCommon, VcRegQueueEnable, 2, &enable) < 0 ||
	   readreg(m, VcMmCommon, VcRegQueueNotify, 2, &notify) < 0 ||
	   size < 1 || size > 32768 || (size&(size-1)) != 0 || enable != 0 ||
	   vcnotify(&m->pci, notify, &address) < 0)
		return mmfault(m);
	offset = address-m->pci.region[VcMmNotify].address;
	if(offset > (Vcu32)m->length[VcMmNotify]-2 ||
	   !statusis(m, VcAck|VcDriver|VcFeaturesOk))
		return mmfault(m);
	memset(&result, 0, sizeof result);
	result.id = id;
	result.size = size < VcQueueMax ? size : VcQueueMax;
	result.notify = offset;
	result.epoch = m->epoch;
	*out = result;
	return 0;
}

int
vcmmenable(VcMmio *m, const VcQueueInfo *info, Vcu64 desc, Vcu64 avail, Vcu64 used)
{
	VcQueueInfo current;
	Vcu64 address[3], length[3];
	Vcu32 value;
	int i, j;

	if(!mmvalid(m) || m->state != VcMmFeatures || info == nil ||
	   info->epoch != m->epoch || info->id < 0 || info->id >= 2*VcPortMax+2 ||
	   info->size < 1 || info->size > VcQueueMax || (info->size&(info->size-1)) != 0)
		return VcInvalid;
	address[0] = desc;
	address[1] = avail;
	address[2] = used;
	length[0] = info->size*16;
	length[1] = 6+info->size*2;
	length[2] = 6+info->size*8;
	if((desc&15) != 0 || (avail&1) != 0 || (used&3) != 0) return VcInvalid;
	for(i = 0; i < 3; i++){
		if(address[i] == 0 || address[i] > ~(Vcu64)0-length[i]) return VcInvalid;
		for(j = 0; j < i; j++)
			if(address[i] < address[j]+length[j] && address[j] < address[i]+length[i])
				return VcInvalid;
	}
	if(vcmmqueue(m, info->id, &current) < 0) return VcFault;
	if(current.epoch != info->epoch || current.size != info->size || current.notify != info->notify)
		return mmfault(m);
	if(writereg(m, VcMmCommon, VcRegQueueSize, 2, info->size) < 0 ||
	   readreg(m, VcMmCommon, VcRegQueueSize, 2, &value) < 0 || value != (Vcu32)info->size ||
	   writereg(m, VcMmCommon, VcRegQueueVector, 2, 65535) < 0)
		return mmfault(m);
	/* Modern PCI permits two aligned 32-bit accesses for each 64-bit field. */
	for(i = 0; i < 3; i++)
		if(writereg(m, VcMmCommon, VcRegQueueDesc+i*8, 4, address[i]) < 0 ||
		   writereg(m, VcMmCommon, VcRegQueueDesc+i*8+4, 4, address[i]>>32) < 0 ||
		   readreg(m, VcMmCommon, VcRegQueueDesc+i*8, 4, &value) < 0 || value != (Vcu32)address[i] ||
		   readreg(m, VcMmCommon, VcRegQueueDesc+i*8+4, 4, &value) < 0 || value != address[i]>>32)
			return mmfault(m);
	if(!statusis(m, VcAck|VcDriver|VcFeaturesOk) ||
	   writereg(m, VcMmCommon, VcRegQueueEnable, 2, 1) < 0 ||
	   readreg(m, VcMmCommon, VcRegQueueEnable, 2, &value) < 0 || value != 1 ||
	   !statusis(m, VcAck|VcDriver|VcFeaturesOk))
		return mmfault(m);
	m->enabled[info->id] = 1;
	m->configured[info->id] = *info;
	return 0;
}

int
vcmmstart(VcMmio *m)
{
	int i, count;

	if(!mmvalid(m) || m->state != VcMmFeatures || m->ports < 1 || m->ports > VcPortMax)
		return VcInvalid;
	count = 2*m->ports+2;
	for(i = 0; i < count; i++)
		if(m->enabled[i] != 1 || m->configured[i].id != i || m->configured[i].epoch != m->epoch)
			return VcInvalid;
	if(!statusis(m, VcAck|VcDriver|VcFeaturesOk) ||
	   writereg(m, VcMmCommon, VcRegStatus, 1, VcAck|VcDriver|VcFeaturesOk|VcDriverOk) < 0 ||
	   !statusis(m, VcAck|VcDriver|VcFeaturesOk|VcDriverOk))
		return mmfault(m);
	m->state = VcMmRunning;
	return 0;
}

int
vcmmhealth(VcMmio *m)
{
	Vcu32 before, after, ports, expected;
	int i;

	if(!mmvalid(m) || (m->state != VcMmFeatures && m->state != VcMmRunning))
		return VcInvalid;
	expected = VcAck|VcDriver|VcFeaturesOk|(m->state == VcMmRunning ? VcDriverOk : 0);
	if(!statusis(m, expected)) return mmfault(m);
	for(i = 0; i < VcMmConfigTries; i++){
		if(readreg(m, VcMmCommon, VcRegGeneration, 1, &before) < 0 ||
		   readreg(m, VcMmDevice, 4, 4, &ports) < 0 ||
		   readreg(m, VcMmCommon, VcRegGeneration, 1, &after) < 0)
			return mmfault(m);
		if(before == after) break;
	}
	if(i == VcMmConfigTries || ports != (Vcu32)m->ports || !statusis(m, expected))
		return mmfault(m);
	return 0;
}

int
vcmmkick(VcMmio *m, const VcQueueInfo *info)
{
	const VcQueueInfo *saved;

	if(!mmvalid(m) || m->state != VcMmRunning || info == nil ||
	   m->ports < 1 || m->ports > VcPortMax || info->id < 0 || info->id >= 2*m->ports+2 ||
	   info->epoch != m->epoch || m->enabled[info->id] != 1)
		return VcInvalid;
	saved = &m->configured[info->id];
	if(saved->epoch != info->epoch || saved->id != info->id || saved->size != info->size ||
	   saved->notify != info->notify || (info->notify&1) != 0 ||
	   info->notify > (Vcu32)m->length[VcMmNotify]-2)
		return VcInvalid;
	if(!statusis(m, VcAck|VcDriver|VcFeaturesOk|VcDriverOk) ||
	   writereg(m, VcMmNotify, info->notify, 2, info->id) < 0 ||
	   !statusis(m, VcAck|VcDriver|VcFeaturesOk|VcDriverOk))
		return mmfault(m);
	return 0;
}

int
vcmmdrop(VcMmio *m, int drained)
{
	int i;

	if(!mmvalid(m) || m->state != VcMmReset || drained != 1) return VcInvalid;
	for(i = VcRegionMax-1; i >= 0; i--){
		m->ops.unmap(m->aux, m->map[i], m->length[i]);
		m->map[i] = nil;
		m->length[i] = 0;
	}
	m->state = VcMmEmpty;
	return 0;
}
