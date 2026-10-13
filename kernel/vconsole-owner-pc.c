/* SPDX-License-Identifier: MIT */
#ifdef QGA_HOST_TEST
#include "../tests/vconsole-owner-pc-shim.h"
#else
#include <u.h>
#include "/sys/src/9/port/lib.h"
#include "/sys/src/9/pc64/mem.h"
#include "/sys/src/9/pc64/dat.h"
#include "/sys/src/9/pc64/fns.h"
#include "/sys/src/9/port/pci.h"
#define QGA_VCONSOLE_KERNEL
#endif
#include "vconsole-owner-pc.h"

int
vcpcownersinit(VcPcOwners *r)
{
	if(r == nil) return VcInvalid;
	memset(r, 0, sizeof *r);
	return 0;
}

static int
readcommand(Pcidev *p)
{
	int value;
	value = pcicfgr16(p, 4);
	return value < 0 || value >= 65535 ? VcFault : value;
}

static int
writecommand(VcPcOwner *o, int value)
{
	if(value < 0 || value >= 65535) return VcInvalid;
	/* Registry claim is already held even if an applied write fails readback. */
	coherence();
	pcicfgw16(o->device, 4, value); /* NOT a dword touching W1C PCI status */
	coherence();
	if(readcommand(o->device) != value) return VcFault;
	o->device->pcr = value; /* keep native PCI cache consistent */
	return 0;
}

/* Bounded live config snapshot, before ANY command changes. Registry alone
 * cannot prove foreign-driver ownership, so BME/MSI activity is rejected too.
 * No MSI capability is accepted: native mpintrassign otherwise auto-enables it. */
static int
idleconfig(Pcidev *p)
{
	Vcu8 seen[VcPciSize];
	int at, next, cap, value, steps;
	value = readcommand(p);
	if(value < 0 || (value&4) != 0 || p->msi > 0) return VcInvalid;
	if(pcicfgr16(p, 0) != p->vid || pcicfgr16(p, 2) != p->did) return VcInvalid;
	memset(seen, 0, sizeof seen);
	at = pcicfgr8(p, 0x34); steps = 0;
	while(at != 0){
		if(at < 0x40 || at > VcPciSize-4 || (at&3) != 0 || seen[at] || ++steps > 48)
			return VcInvalid;
		seen[at] = 1;
		cap = pcicfgr8(p, at); next = pcicfgr8(p, at+1);
		if(cap < 0 || cap > 255 || next < 0 || next > 255 || cap == 5) return VcInvalid;
		if(cap == 1){
			if(at > VcPciSize-6) return VcInvalid;
			value = pcicfgr16(p, at+4);
			if(value < 0 || value > 65535 || (value&3) != 0) return VcInvalid;
		}
		if(cap == 0x11){
			value = pcicfgr16(p, at+2);
			if(value < 0 || value > 65535 || (value&0x8000) != 0) return VcInvalid;
		}
		at = next;
	}
	return 0;
}

static void
unreserve(VcPcOwner *o)
{
	VcPcOwners *r;
	r = o->registry;
	ilock(&r->lock);
	if(r->slot[o->slot] == o) r->slot[o->slot] = nil;
	o->claimed = 0;
	iunlock(&r->lock);
}

int
vcpcownerclaim(VcPcOwners *r, VcPcOwner *o, Pcidev *p)
{
	int i, slot, saved, result;
	if(r == nil || o == nil || p == nil || coherence == nil || p->vid != 0x1af4 ||
	   (p->did != 0x1043 && p->did != 0x1003) || idleconfig(p) < 0) return VcInvalid;
	saved = readcommand(p);
	if(saved < 0 || (saved&4) != 0) return VcInvalid;
	ilock(&r->lock);
	slot = -1;
	for(i = 0; i < VcPcOwnersMax; i++){
		if(r->slot[i] == nil){ if(slot < 0) slot = i; }
		else if(r->slot[i] == o || r->slot[i]->device->tbdf == p->tbdf){
			iunlock(&r->lock); return VcInvalid;
		}
	}
	if(slot < 0 || r->issued == ~(Vcu64)0){ iunlock(&r->lock); return VcInvalid; }
	/* Check published pointer identity BEFORE memset, including accidental
	 * duplicate same-object calls. No allocation/MMIO/callout under this lock. */
	memset(o, 0, sizeof *o);
	o->registry = r; o->device = p; o->saved = saved;
	o->slot = slot; o->identity = ++r->issued; o->claimed = 1; r->slot[slot] = o;
	iunlock(&r->lock);
	if(vcpcinit(&o->pc) < 0){ unreserve(o); return VcFault; }
	result = writecommand(o, saved|2|VcPcIntDisable);
	if(result == 0) result = vcpcmap(&o->pc, p);
	if(result < 0){
		/* Nothing DMA-exposed yet, but never relinquish an ambiguous command.
		 * A failed mapping has already unwound its own acquired references. */
		if(o->pc.mmio.state == VcMmEmpty && writecommand(o, saved) == 0) unreserve(o);
	}
	return result;
}

int
vcpcownerbind(VcPcOwner *o, VcController *c)
{
	if(o == nil || !o->claimed || c == nil || o->controller != nil ||
	   c->mmio != &o->pc.mmio || c->identity != o->identity || c->state != VcCtlIdle ||
	   o->pc.mmio.state != VcMmMapped) return VcInvalid;
	o->controller = c;
	return 0;
}

int
vcpcownerprepare(VcPcOwner *o, int capacity, Vcu64 limit)
{
	Vcu64 extra;
	int result;
	if(o == nil || !o->claimed || o->controller == nil || o->detached ||
	   limit == 0 || limit > VcPumpBudgetMax) return VcInvalid;
	extra = sizeof(VcPcOwner)-sizeof(VcMmio)+sizeof(VcPcWorker)+sizeof(VcPcWake)+
		sizeof(VcPcCtlLock)+sizeof(VcPcOwners)+sizeof(VcPcIo)+sizeof(VcPcDev)+sizeof(VcDevRegistry);
	if(limit <= extra) return VcInvalid;
	o->prepared = 0;
	result = vccprepare(o->controller, capacity, limit-extra);
	if(result == 0) o->prepared = 1;
	return result;
}

int
vcpcownerstart(VcPcOwner *o, VcController *c, VcPcWake *wake)
{
	int value, result;
	if(o == nil || !o->claimed || !o->prepared || c == nil || wake == nil || o->controller != c || o->wake != nil ||
	   c->mmio != &o->pc.mmio || c->identity != o->identity || c->state != VcCtlPrepared ||
	   o->pc.mmio.state != VcMmFeatures || wake->irq.epoch != o->pc.mmio.epoch ||
	   wake->isr != o->pc.mmio.map[VcMmIsr] || o->device->intl == 0 || o->device->intl == 255)
		return VcInvalid;
	value = readcommand(o->device);
	if(value < 0 || (value&(2|4|VcPcIntDisable)) != (2|VcPcIntDisable) ||
	   idleconfig(o->device) < 0) return VcInvalid;
	o->wake = wake;
	o->irqtried = 1; /* set BEFORE native registration, not an asserted success */
	intrenable(o->device->intl, vcpcwakeintr, wake, o->device->tbdf, "vconsole");
	/* Native registration has no success result. Keep INTx masked until a
	 * checked native registration API exists; bounded worker waits poll rings.
	 * Never turn a silent registration failure into an unhandled IRQ storm. */
	result = writecommand(o, value|4|VcPcIntDisable);
	if(result == 0) result = vccstart(c, &wake->irq);
	if(result < 0){
		vccstop(c); vcirqclose(&wake->irq);
		/* Claim/storage retained. Cooperative stop performs checked cleanup. */
	}
	return result;
}

int
vcpcownerquiesce(VcPcOwner *o)
{
	int value;
	if(o == nil || !o->claimed || o->registry == nil || o->device == nil) return VcInvalid;
	if(!o->detached && o->controller != nil) vccstop(o->controller);
	if(!o->detached && o->wake != nil) vcirqclose(&o->wake->irq);
	value = readcommand(o->device);
	if(value < 0 || writecommand(o, (value|VcPcIntDisable)&~4) < 0) return VcFault;
	if(o->irqtried){
		intrdisable(o->device->intl, vcpcwakeintr, o->wake, o->device->tbdf, "vconsole");
		o->irqtried = 0;
	}
	return 0;
}

int
vcpcownerstop(VcPcOwner *o)
{
	int result;
	if((result = vcpcownerquiesce(o)) < 0) return result;
	if(!o->detached && o->wake != nil && !vcirqdrained(&o->wake->irq)) return VcStreamWait;
	if(!o->detached && o->controller != nil){
		result = vccreset(o->controller);
		if(result < 0) return result;
		if(vccdetach(o->controller) < 0) return VcFault;
	}else if(!o->detached && o->pc.mmio.state != VcMmEmpty){
		if(vcmmreset(&o->pc.mmio) < 0 || vcmmdrop(&o->pc.mmio, 1) < 0) return VcFault;
	}
	o->detached = 1;
	if(writecommand(o, o->saved) < 0) return VcFault;
	unreserve(o);
	return 0;
}
