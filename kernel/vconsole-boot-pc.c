/* SPDX-License-Identifier: MIT */
#ifdef QGA_HOST_TEST
#include "../tests/vconsole-boot-shim.h"
#else
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/pci.h"
#define QGA_VCONSOLE_KERNEL
#endif
#include "vconsole-owner-pc.h"

/* One controller, at most four complete, fresh epochs per boot. All callback
 * targets (including retired workers/IRQs/Chans) remain statically pinned.
 * Supervisor alone owns these flags; worker/broker/Dev locks own runtime state.
 * No unbounded retry, reset under a live FD, or reuse of a retired object. */
enum {
	BootEpochs = 4, BootPollMs = 100, BootPollMax = 100,
	BootUnused = 0, BootLaunching, BootOnline, BootStopping, BootRetired, BootFailed,
	BootAllocatorReserve = 65536,
};
typedef struct BootContext BootContext;
typedef struct BootState BootState;
struct BootContext {
	VcPcOwner owner;
	VcController controller;
	VcPcCtlLock gate;
	VcPcWake wake;
	VcPcWorker worker;
	VcPcIo io;
	VcPcDev dev;
	int phase, polls, workerstarted, published, retry;
};
struct BootState {
	VcPcOwners owners;
	BootContext context[BootEpochs];
	Pcidev *device;
	int used, wantirq;
};
#ifndef QGA_HOST_TEST
static BootState boot;
#endif

static int
bootacquire(BootState *b, BootContext *c)
{
	VcCtlOps ops;
	VcDmaOps dma;
	Vcu64 reserve;
	int result;
	/* Count ALL pinned epochs plus conservative allocator slack before DMA.
	 * Owner's own accounting double-counts the current context deliberately. */
	reserve = sizeof *b + BootAllocatorReserve;
	if(reserve >= VcPumpBudgetMax) return VcInvalid;
	if((result = vcpcownerclaim(&b->owners, &c->owner, b->device)) < 0) return result;
	if(b->wantirq && vcpcownerirq(&c->owner)<0) return VcInvalid;
	if(vcpcctlops(&ops) < 0 || vcpcdmaops(&dma) < 0 ||
	   vccinit(&c->controller, &c->owner.pc.mmio, &ops, &c->gate,
		&dma, nil, c->owner.identity) < 0 ||
	   vcpcownerbind(&c->owner, &c->controller) < 0 ||
	   vcpcownerprepare(&c->owner, 256, VcPumpBudgetMax-reserve) < 0 ||
	   vcpcwakeinit(&c->wake, &c->owner.pc.mmio) < 0 ||
	   vcpcioinit(&c->io, &c->controller, c->owner.pc.mmio.epoch) < 0 ||
	   vcpcworkerinit(&c->worker, &c->controller, &c->wake, 32, BootPollMs) < 0 ||
	   vcpcworkerbindio(&c->worker, &c->io) < 0 ||
	   vcpcdevinit(&c->dev, &c->io, &c->worker, 0) < 0 ||
	   vcpcownerstart(&c->owner, &c->controller, &c->wake) < 0) return VcFault;
	c->workerstarted = 1; /* even failed spawn must complete its held lease */
	return vcwstart(&c->worker.worker);
}

static int
bootstep(BootContext *c)
{
	VcIoStatus status;
	int result, done;
	if(c->phase == BootLaunching){
		if(vcpcdevpublish(&c->dev) == 0){
			c->published = 1; c->phase = BootOnline; c->polls = 0;
			return 0;
		}
		if(!vcwjoined(&c->worker.worker, &result) && ++c->polls < BootPollMax)
			return VcStreamWait;
		c->phase = BootStopping; c->polls = 0; /* startup failure: no auto retry */
	}
	if(c->phase == BootOnline){
		done = vcwjoined(&c->worker.worker, &result);
		if(!done && vciostatus(&c->io.io, &status) == 0 &&
		   !status.stopped && status.owner != VcIoRetired) return VcStreamWait;
		c->phase = BootStopping; c->polls = 0; c->retry = 1;
	}
	if(c->phase != BootStopping) return VcInvalid;
	if(c->workerstarted) vcwstop(&c->worker.worker);
	/* Remove DMA permission immediately; never treat this as reset proof. */
	result = c->owner.claimed ? vcpcownerquiesce(&c->owner) : 0;
	if(result == 0 && c->workerstarted && !vcwjoined(&c->worker.worker, &done))
		result = VcStreamWait;
	if(result == 0 && c->published){
		result = vcpcdevwithdraw(&c->dev); /* BEFORE reset and mapping release */
		if(result == 0) c->published = 0;
	}
	if(result == 0 && c->owner.claimed) result = vcpcownerstop(&c->owner);
	if(result == 0){ c->phase = BootRetired; return 0; }
	/* Ambiguous PCI writes/reset failure/live Chans retain all resources.
	 * Polls are a retry bound, not a proof of native scheduling latency. */
	if(++c->polls >= BootPollMax){ c->phase = BootFailed; return VcFault; }
	return VcStreamWait;
}

#ifndef QGA_HOST_TEST
static int
never(void*) { return 0; }

static int
bootpause(Rendez *r)
{
	if(waserror()) return VcFault;
	tsleep(r, never, nil, BootPollMs);
	poperror();
	return 0;
}

static void
bootproc(void*)
{
	BootContext *c;
	Pcidev *p;
	Rendez pause;
	int found, scanned, result;
	char *irqmode;
	memset(&pause, 0, sizeof pause);
	irqmode=getconf("qgavirtioirq");
	boot.wantirq=irqmode!=nil && strcmp(irqmode,"1")==0;
	/* Enumerate only the bounded console PCI matches, never other devices.
	 * Multiple controllers are ambiguous: do not silently choose one. */
	found = scanned = 0;
	for(p = nil; (p = pcimatch(p, 0x1af4, 0)) != nil;){
		if(++scanned > VcPortMax){ found = -1; break; }
		if(p->did != 0x1043 && p->did != 0x1003) continue;
		boot.device = p;
		if(++found > 1) break;
	}
	if(found != 1){
		print("vconsole: expected one console PCI device, found %d; no device claimed\n", found);
		return;
	}
	vcpcownersinit(&boot.owners);
	while(boot.used < BootEpochs){
		c = &boot.context[boot.used++]; /* never reinitialize a used context */
		result = bootacquire(&boot, c);
		c->phase = result == 0 ? BootLaunching : BootStopping;
		if(result < 0) print("vconsole: startup failed %d; draining\n", result);
		for(;;){
			bootstep(c);
			if(c->phase == BootOnline && c->polls == 0){
				print("vconsole: #Y0 ready, epoch %d/%d, %s\n", boot.used, BootEpochs,
					boot.wantirq ? "checked INTx with health polling" : "polling INTx masked");
				c->polls = 1; /* log once; no startup deadline while online */
			}
			if(c->phase == BootRetired || c->phase == BootFailed) break;
			if(bootpause(&pause) < 0){ c->phase = BootStopping; c->retry = 0; }
		}
		if(c->phase == BootFailed){
			print("vconsole: drain timed out; resources retained, reboot required\n");
			return;
		}
		if(!c->retry) return;
	}
	print("vconsole: fresh epoch pool exhausted; reboot required\n");
}

void
vcpcbootinit(void)
{
	char *enabled;
	enabled = getconf("qgavirtio");
	if(enabled == nil || strcmp(enabled, "1") != 0){
		print("vconsole: disabled (set qgavirtio=1 to enable)\n");
		return;
	}
	/* Dev.init runs from init0 process. kproc's native resource wait is NOT
	 * bounded, but no PCI/MMIO/lease is acquired until our runner enters. */
	if(up == nil) return;
	if(waserror()){ print("vconsole: supervisor spawn failed\n"); return; }
	kproc("vconsoleboot", bootproc, nil);
	poperror();
}
#endif
