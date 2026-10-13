/* SPDX-License-Identifier: MIT */
/* Compile into a future PC64 kernel driver, never the user-space agent. */
#ifdef QGA_HOST_TEST
#include "../tests/vconsole-pc-shim.h"
#else
#include <u.h>
#include "/sys/src/9/port/lib.h"
#include "/sys/src/9/pc64/mem.h"
#include "/sys/src/9/pc64/dat.h"
#include "/sys/src/9/pc64/fns.h"
#include "/sys/src/9/port/pci.h"
#include "/sys/src/9/pc/io.h"
#define QGA_VCONSOLE_KERNEL
#endif
#include "vconsole-pc.h"

static void*
pcmap(void *aux, Vcu64 address, int size)
{
	USED(aux);
	/* Stock PC64 vunmap keeps mappings. Stay in the deterministic direct VMAP
	 * window so retries cannot allocate fresh high-physical virtual ranges.
	 * vmap adds the page offset internally; mapped lengths are capped here. */
	if(size < 1 || size > VcMmMapMax || address < BY2PG ||
	   address >= VMAPSIZE || address > VMAPSIZE-size)
		return nil;
	return vmap(address, size);
}

static void
pcunmap(void *aux, void *base, int size)
{
	USED(aux);
	vunmap(base, size);
}

static int
pcaccess(VcPc *pc, void *base, int offset, int width)
{
	int i;

	if(pc == nil || base == nil || offset < 0 ||
	   (width != 1 && width != 2 && width != 4) ||
	   (uintptr)offset > ~(uintptr)0-(uintptr)base ||
	   (((uintptr)base+offset)&(width-1)) != 0)
		return 0;
	for(i = 0; i < VcRegionMax; i++)
		if(base == pc->mmio.map[i] && pc->mmio.length[i] >= width &&
		   pc->mmio.length[i] <= VcMmMapMax && offset <= pc->mmio.length[i]-width)
			return 1;
	return 0;
}

static int
pcread(void *aux, void *base, int offset, int width, Vcu32 *value)
{
	volatile Vcu8 *p;

	if(value == nil || !pcaccess(aux, base, offset, width)) return VcInvalid;
	p = (Vcu8*)base+offset;
	switch(width){
	case 1: *value = *p; break;
	case 2: *value = *(volatile Vcu16*)p; break;
	case 4: *value = *(volatile Vcu32*)p; break;
	}
	return 0;
}

static int
pcwrite(void *aux, void *base, int offset, int width, Vcu32 value)
{
	volatile Vcu8 *p;

	if(!pcaccess(aux, base, offset, width) ||
	   (width == 1 && value > 255) || (width == 2 && value > 65535))
		return VcInvalid;
	p = (Vcu8*)base+offset;
	switch(width){
	case 1: *p = value; break;
	case 2: *(volatile Vcu16*)p = value; break;
	case 4: *(volatile Vcu32*)p = value; break;
	}
	return 0;
}

static void
pcfence(void *aux)
{
	USED(aux);
	coherence();
}

static void
pcpause(void *aux)
{
	USED(aux);
	delay(1);
}

int
vcpcinit(VcPc *pc)
{
	static const VcMmOps ops = {pcmap, pcunmap, pcread, pcwrite, pcfence, pcpause};

	if(pc == nil || coherence == nil) return VcInvalid;
	pc->device = nil;
	return vcmminit(&pc->mmio, &ops, pc);
}

static int
pcdmaalloc(void *aux, int bytes, int align, VcDmaMemory *out)
{
	void *p;
	Vcuptr va;
	Vcu64 first, last;

	USED(aux);
	if(out == nil || bytes < 1 || bytes > VcDmaMax || align != VcDmaAlign)
		return VcInvalid;
	p = mallocalign(bytes, align, 0, 0);
	if(p == nil) return VcFault;
	va = (Vcuptr)p;
	/* paddr can panic on nonkernel pointers. Only the native direct KZERO RAM
	 * allocation is eligible; its linear mapping guarantees interior continuity. */
	if(va < KZERO || va > ~(Vcuptr)0-(Vcu32)bytes || (va&15) != 0){ free(p); return VcFault; }
	first = PADDR(p);
	last = PADDR((Vcu8*)p+bytes-1);
	if(first == 0 || (first&15) != 0 || first > ~(Vcu64)0-(Vcu32)bytes ||
	   last != first+(Vcu32)bytes-1){ free(p); return VcFault; }
	memset(out, 0, sizeof *out);
	out->base = p;
	out->physical = first;
	out->bytes = bytes;
	return 0;
}

static void
pcdmafree(void *aux, const VcDmaMemory *memory)
{
	USED(aux);
	free(memory->base);
}

int
vcpcdmaops(VcDmaOps *out)
{
	static const VcDmaOps ops = {pcdmaalloc, pcdmafree, pcfence};

	if(out == nil || coherence == nil) return VcInvalid;
	*out = ops;
	return 0;
}

int
vcpcmap(VcPc *pc, Pcidev *device)
{
	Vcu8 cfg[VcPciSize];
	VcBar bars[VcBarMax];
	VcPci checked;
	Vcu64 bar;
	int i, value, at, type, result;

	if(pc == nil || device == nil || pc->mmio.state != VcMmEmpty ||
	   device->vid != 0x1af4 || (device->did != 0x1043 && device->did != 0x1003))
		return VcInvalid;
	for(i = 0; i < VcPciSize; i++){
		value = pcicfgr8(device, i);
		if(value < 0 || value > 255) return VcInvalid;
		cfg[i] = value;
	}
	if((cfg[0]|(Vcu16)cfg[1]<<8) != device->vid ||
	   (cfg[2]|(Vcu16)cfg[3]<<8) != device->did || !(cfg[4]&2))
		return VcInvalid; /* PCI owner must already have enabled memory decoding. */
	memset(bars, 0, sizeof bars);
	for(i = 0; i < VcBarMax; i++){
		bar = device->mem[i].bar;
		type = (bar>>1)&3;
		if(device->mem[i].size > 0 && !(bar&1) && (type == 0 || type == 2)){
			if(type == 2 && i+1 == VcBarMax) return VcInvalid;
			bars[i].memory = 1;
			bars[i].address = bar&~(Vcu64)15;
			bars[i].size = device->mem[i].size;
		}
		if(!(bar&1) && type == 2 && i+1 < VcBarMax) i++; /* Skip 64-bit BAR high half. */
	}
	if(vcpci(&checked, cfg, sizeof cfg, bars) < 0) return VcInvalid;
	/* vcpci checked list offsets/cycles; each header fits the IRQ control field.
	 * INTx-only for now: never steal enabled MSI/MSI-X from a live driver. */
	for(at = cfg[0x34]; at != 0; at = cfg[at+1])
		if((cfg[at] == 5 && (cfg[at+2]&1)) ||
		   (cfg[at] == 0x11 && (cfg[at+3]&0x80)))
			return VcInvalid;
	result = vcmmmap(&pc->mmio, cfg, sizeof cfg, bars);
	if(result == 0) pc->device = device;
	return result;
}
