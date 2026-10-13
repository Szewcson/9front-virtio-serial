/* SPDX-License-Identifier: MIT */
/* Actual PCI owner/mapping/IRQ helpers with fake native primitives and ordered
 * controller calls. Worker/DMA behavior is tested separately with real helpers. */
#include "vconsole-owner-pc-shim.h"
#include "../kernel/vconsole-owner-pc.h"
#include <pthread.h>
#include <stdio.h>
typedef struct NativeFixture NativeFixture;
struct NativeFixture {
	Pcidev device;
	VcController controller;
	VcPcWake wake;
	Vcu8 cfg[VcPciSize];
	void *mapping[4];
	int lengths[4], maps, unmaps, cfgwrites, failmap, badwrite, unreadable;
	int registers, unregisters, starts, resets, detaches, leases, failstart, failreset, prepares;
	Vcu64 preparelimit;
	int isrreads, wakes;
	pthread_mutex_t config, irq;
};
static NativeFixture *fixtures[8];
static int checks, fixturecount;
static pthread_mutex_t nativelock = PTHREAD_MUTEX_INITIALIZER;
static const int offsets[4] = {0, 4096, 64, 68};

static void
must(int ok)
{
	if(!ok) abort();
}

static void
check(int ok, const char *name)
{
	checks++; if(ok) return;
	fprintf(stderr, "FAIL: %s\n", name); exit(1);
}

static void
barrier(void)
{
	/* Simulated platform barrier, not a DMA/coherence hardware guarantee. */
	__sync_synchronize();
}
void (*coherence)(void) = barrier;

void
ilock(Lock *l)
{
	USED(l); must(pthread_mutex_lock(&nativelock) == 0);
}

void
iunlock(Lock *l)
{
	USED(l); must(pthread_mutex_unlock(&nativelock) == 0);
}

int
pcicfgr8(Pcidev *p, int at)
{
	NativeFixture *f;
	int v;
	f = p->testaux; must(at >= 0 && at < VcPciSize);
	must(pthread_mutex_lock(&f->config) == 0);
	v = f->unreadable ? -1 : f->cfg[at];
	must(pthread_mutex_unlock(&f->config) == 0); return v;
}

int
pcicfgr16(Pcidev *p, int at)
{
	NativeFixture *f;
	int v;
	f = p->testaux; must(at >= 0 && at <= VcPciSize-2 && !(at&1));
	must(pthread_mutex_lock(&f->config) == 0);
	v = f->unreadable ? -1 : f->cfg[at]|f->cfg[at+1]<<8;
	must(pthread_mutex_unlock(&f->config) == 0); return v;
}

void
pcicfgw16(Pcidev *p, int at, int v)
{
	NativeFixture *f;
	f = p->testaux; must(at == 4 && v >= 0 && v < 65535);
	must(pthread_mutex_lock(&f->config) == 0); f->cfgwrites++;
	if(!f->badwrite){ f->cfg[at] = v; f->cfg[at+1] = v>>8; }
	must(pthread_mutex_unlock(&f->config) == 0);
}

void*
vmap(uint64_t address, int64_t bytes)
{
	NativeFixture *f;
	int i, j;
	for(i = 0; i < fixturecount; i++){
		f = fixtures[i]; if(f == nil) continue;
		for(j = 0; j < 4; j++) if(address == (f->device.mem[4].bar&~(Vcu64)15)+offsets[j]){
			must(bytes > 0 && bytes <= VcMmMapMax && f->mapping[j] == nil);
			if(j == f->failmap) return nil;
			f->mapping[j] = calloc(1, bytes); must(f->mapping[j] != nil);
			f->lengths[j] = bytes; f->maps++; return f->mapping[j];
		}
	}
	abort();
}

void
vunmap(void *base, int64_t bytes)
{
	NativeFixture *f;
	int i, j;
	for(i = 0; i < fixturecount; i++){
		f = fixtures[i]; if(f == nil) continue;
		for(j = 0; j < 4; j++) if(f->mapping[j] == base){
			must(base != nil && bytes == f->lengths[j]);
			free(base); f->mapping[j] = nil; f->unmaps++; return;
		}
	}
	abort();
}

void*
mallocalign(unsigned long n, unsigned long a, long off, unsigned long span)
{
	USED(n); USED(a); USED(off); USED(span); abort();
}

uintptr_t
testpaddr(void *p)
{
	USED(p); abort();
}

void
delay(int n)
{
	must(n == 1);
}

static NativeFixture*
fromwake(VcPcWake *wake)
{
	int i;
	for(i = 0; i < fixturecount; i++)
		if(fixtures[i] != nil && &fixtures[i]->wake == wake) return fixtures[i];
	abort();
}

void
intrenable(int irq, void (*fn)(Ureg*, void*), void *aux, int tbdf, char *name)
{
	NativeFixture *f;
	f = fromwake(aux);
	must(irq == f->device.intl && tbdf == f->device.tbdf && fn == vcpcwakeintr &&
		strcmp(name, "vconsole") == 0 && (pcicfgr16(&f->device, 4)&4) == 0);
	f->registers++; /* intentionally no success result, as native API */
}

void
intrdisable(int irq, void (*fn)(Ureg*, void*), void *aux, int tbdf, char *name)
{
	NativeFixture *f;
	f = fromwake(aux);
	must(irq == f->device.intl && tbdf == f->device.tbdf && fn == vcpcwakeintr &&
		strcmp(name, "vconsole") == 0 && vcirqdrained(&f->wake.irq));
	f->unregisters++;
}

int
vccstop(VcController *c)
{
	c->closed = 1; c->admit = 0;
	return c->irq != nil ? vcirqclose(c->irq) : 0;
}

int
vccprepare(VcController *c, int capacity, Vcu64 limit)
{
	NativeFixture *f;
	f = c->dmaaux; must(capacity == 256 && c->state == VcCtlIdle && limit > 0);
	f->preparelimit = limit; f->prepares++;
	c->mmio->state = VcMmFeatures; c->mmio->epoch++; c->state = VcCtlPrepared;
	return 0; /* explicit ordered-call stub; real planner is in controller suite */
}

int
vccstart(VcController *c, VcIrq *irq)
{
	NativeFixture *f;
	f = fromwake(irq->aux);
	must(f->registers == 1 && (pcicfgr16(&f->device, 4)&(2|4|VcPcIntDisable)) == (6|VcPcIntDisable));
	f->starts++;
	if(f->failstart) return VcFault;
	c->irq = irq; c->state = VcCtlRunning; c->admit = 1;
	c->mmio->state = VcMmRunning;
	((Vcu8*)f->mapping[0])[VcRegStatus] = 15;
	return 0;
}

int
vccreset(VcController *c)
{
	NativeFixture *f;
	int result;
	f = c->dmaaux; vccstop(c);
	if(f->leases) return VcStreamWait;
	if(f->failreset) return VcFault;
	result = vcmmreset(c->mmio);
	if(result == 0){ c->state = VcCtlIdle; f->resets++; }
	return result;
}

int
vccdetach(VcController *c)
{
	NativeFixture *f;
	int result;
	f = c->dmaaux; result = vcmmdrop(c->mmio, 1);
	if(result == 0){ c->state = VcCtlDetached; c->irq = nil; f->detaches++; }
	return result;
}

static void
irqmutex(void *aux)
{
	NativeFixture *f;
	f = fromwake(aux); must(pthread_mutex_lock(&f->irq) == 0);
}

static void
irqunmutex(void *aux)
{
	NativeFixture *f;
	f = fromwake(aux); must(pthread_mutex_unlock(&f->irq) == 0);
}

static int
irqread(void *aux, Vcu32 *out)
{
	NativeFixture *f;
	f = fromwake(aux); f->isrreads++; *out = 1; return 0;
}

static void
irqfence(void *aux)
{
	USED(aux); barrier();
}

static void
irqwake(void *aux)
{
	NativeFixture *f;
	f = fromwake(aux); f->wakes++;
}

void
vcpcwakeintr(Ureg *ureg, void *aux)
{
	VcPcWake *wake;
	USED(ureg); wake = aux; vcirqinterrupt(&wake->irq);
}

static void
word(Vcu8 *p, Vcu32 n)
{
	int i;
	for(i = 0; i < 4; i++) p[i] = n>>(8*i);
}

static NativeFixture*
fixture(int id)
{
	NativeFixture *f;
	int i, at;
	check(id >= 0 && id < 8 && fixtures[id] == nil, "bounded fixture slot");
	f = calloc(1, sizeof *f); check(f != nil, "heap native owner fixture");
	must(pthread_mutex_init(&f->config, nil) == 0 && pthread_mutex_init(&f->irq, nil) == 0);
	fixtures[id] = f; if(fixturecount <= id) fixturecount = id+1;
	f->device.testaux = f; f->device.vid = 0x1af4; f->device.did = 0x1043;
	f->device.tbdf = 0x1000000+id; f->device.intl = 10; f->failmap = -1;
	f->device.mem[4].bar = 0x100004+(Vcu64)id*0x10000;
	f->device.mem[4].size = 8192;
	word(f->cfg, 0x10431af4); f->cfg[4] = 0x40; f->device.pcr = 0x40;
	f->cfg[6] = 0x10; f->cfg[0x34] = 0x40;
	for(i = 0; i < 4; i++){
		at = 0x40+i*20; f->cfg[at] = 9;
		f->cfg[at+1] = i == 3 ? 0 : at+20;
		f->cfg[at+2] = i == 1 ? 20 : 16; f->cfg[at+3] = i+1; f->cfg[at+4] = 4;
		word(f->cfg+at+8, offsets[i]); word(f->cfg+at+12, i == 0 ? 56 : i == 1 ? 4096 : i == 2 ? 1 : 8);
		if(i == 1) word(f->cfg+at+16, 2);
	}
	return f;
}

static void
destroy(NativeFixture *f)
{
	int i;
	check(f->maps == f->unmaps, "all owned maps released once");
	for(i = 0; i < fixturecount; i++) if(fixtures[i] == f){ fixtures[i] = nil; break; }
	must(i < fixturecount && pthread_mutex_destroy(&f->config) == 0 && pthread_mutex_destroy(&f->irq) == 0);
	free(f);
}

static void
bindprepare(VcPcOwner *o, NativeFixture *f)
{
	static const VcIrqOps ops = {irqmutex, irqunmutex, irqread, irqfence, irqwake};
	f->controller.mmio = &o->pc.mmio; f->controller.identity = o->identity;
	f->controller.dmaaux = f;
	check(vcpcownerbind(o, &f->controller) == 0, "bind manager BEFORE any DMA acquisition");
	check(vcpcownerprepare(o, 256, 1) == VcInvalid && f->prepares == 0,
		"undersized native overhead rejected before controller allocation");
	check(vcpcownerprepare(o, 256, VcPumpBudgetMax) == 0 && f->prepares == 1 &&
		f->preparelimit == VcPumpBudgetMax-(sizeof(VcPcOwner)-sizeof(VcMmio)+
		sizeof(VcPcWorker)+sizeof(VcPcWake)+sizeof(VcPcCtlLock)+sizeof(VcPcOwners)+sizeof(VcPcIo)+
		sizeof(VcPcDev)+sizeof(VcDevRegistry)),
		"standard pinned native contexts budgeted BEFORE manager acquisition");
	f->wake.isr = o->pc.mmio.map[2];
	check(vcirqinit(&f->wake.irq, &ops, &f->wake, o->pc.mmio.epoch) == 0, "actual pinned IRQ gate");
}

static void
lifecycle(void)
{
	VcPcOwners r;
	VcPcOwner o, duplicate;
	NativeFixture *f;
	int i, reads;
	check(vcpcownersinit(&r) == 0, "fresh bounded native registry");
	f = fixture(0);
	check(vcpcownerclaim(&r, &o, &f->device) == 0 && o.claimed && o.identity != 0 &&
		pcicfgr16(&f->device, 4) == (0x40|2|VcPcIntDisable), "reserve/map with BME off and INTx masked");
	check(vcpcownerclaim(&r, &duplicate, &f->device) == VcInvalid && f->cfgwrites == 1,
		"duplicate TBDF rejected before writes");
	check(vcpcownerclaim(&r, &o, &f->device) == VcInvalid && o.claimed &&
		o.pc.mmio.state == VcMmMapped && f->cfgwrites == 1 && f->maps == 4,
		"same-object duplicate claim cannot erase owned mappings");
	bindprepare(&o, f);
	check(vcpcownerstart(&o, &f->controller, &f->wake) == 0 && f->starts == 1,
		"IRQ attempt and checked BME precede DRIVER_OK; unchecked INTx remains masked");
	f->leases = 1;
	check(vcpcownerquiesce(&o) == 0 && o.claimed && f->unmaps == 0 && f->resets == 0 &&
		f->unregisters == 1 && (pcicfgr16(&f->device, 4)&4) == 0,
		"quiesce masks bus master without reset/free while Dev remains published");
	check(vcpcownerquiesce(&o) == 0 && f->unregisters == 1 && f->resets == 0,
		"repeated quiesce cannot duplicate deregistration or reset");
	check(vcpcownerstop(&o) == VcStreamWait && o.claimed && f->unmaps == 0 &&
		f->unregisters == 1 && (pcicfgr16(&f->device, 4)&4) == 0,
		"cooperative teardown cannot unmap while worker/client owns lease");
	f->leases = 0; f->failreset = 1;
	check(vcpcownerstop(&o) == VcFault && o.claimed && f->unmaps == 0,
		"failed confirmed reset retains registry and maps");
	f->failreset = 0;
	check(vcpcownerstop(&o) == 0 && !o.claimed && f->unmaps == 4 &&
		f->device.pcr == 0x40 && pcicfgr16(&f->device, 4) == 0x40,
		"reset before unmap, then saved 16-bit command/cache restored");
	reads = f->isrreads; vcpcwakeintr(nil, &f->wake);
	check(f->isrreads == reads, "late pre-dispatched IRQ cannot read unmapped ISR");
	check(vcpcownerstop(&o) == VcInvalid && f->unregisters == 1, "no duplicate unregister/free");
	destroy(f);
	for(i = 0; i < 4; i++){
		f = fixture(0); f->failmap = i;
		check(vcpcownerclaim(&r, &o, &f->device) == VcFault && !o.claimed &&
			pcicfgr16(&f->device, 4) == 0x40, "every partial mapping failure unwound and restored");
		destroy(f);
	}
	f = fixture(0); f->badwrite = 1;
	check(vcpcownerclaim(&r, &o, &f->device) == VcFault, "failed command readback never maps");
	/* Restore may confirm the unchanged original command, permitting unreserve. */
	check(!o.claimed && f->maps == 0, "unchanged saved command confirmed after rejected write");
	destroy(f);
	f = fixture(0);
	check(vcpcownerclaim(&r, &o, &f->device) == 0, "prepare failed startup"); bindprepare(&o, f);
	f->failstart = 1;
	check(vcpcownerstart(&o, &f->controller, &f->wake) == VcFault &&
		vcirqdrained(&f->wake.irq) && o.claimed && f->unmaps == 0, "failed startup closes gate and retains maps");
	check(vcpcownerstop(&o) == 0, "failed startup cleaned with actual reset"); destroy(f);
	f = fixture(0);
	check(vcpcownerclaim(&r, &o, &f->device) == 0, "prepare ambiguous master activation"); bindprepare(&o, f);
	f->badwrite = 1;
	check(vcpcownerstart(&o, &f->controller, &f->wake) == VcFault && f->starts == 0 &&
		o.claimed && vcirqdrained(&f->wake.irq), "BME readback failure cannot reach DRIVER_OK");
	check(vcpcownerstop(&o) == VcFault && o.claimed && o.detached && f->unmaps == 4 &&
		f->resets == 1 && f->detaches == 1, "failed saved-command restore retains claim after safe detach");
	f->badwrite = 0;
	check(vcpcownerstop(&o) == 0 && !o.claimed && f->resets == 1 && f->detaches == 1 &&
		f->unregisters == 1, "restore retry never resets/detaches or unregisters twice"); destroy(f);
}

static void
rejects(void)
{
	VcPcOwners r;
	VcPcOwner o;
	NativeFixture *f;
	int i;
	for(i = 0; i < 7; i++){
		vcpcownersinit(&r); f = fixture(0);
		if(i == 0) f->cfg[4] |= 4;
		if(i == 1){ f->cfg[0x34] = 0xe0; f->cfg[0xe0] = 5; }
		if(i == 2){ f->cfg[0x34] = 0xe0; f->cfg[0xe0] = 0x11; f->cfg[0xe3] = 0x80; }
		if(i == 3){ f->cfg[0x34] = 0xe0; f->cfg[0xe0] = 1; f->cfg[0xe4] = 3; }
		if(i == 4) f->cfg[0x34] = 255;
		if(i == 5) f->cfg[0x41] = 0x40;
		if(i == 6) f->unreadable = 1;
		check(vcpcownerclaim(&r, &o, &f->device) == VcInvalid && f->cfgwrites == 0 && f->maps == 0,
			"BME/MSI/MSIX/power/cap-cycle/absent device rejected before mutation");
		destroy(f);
	}
	vcpcownersinit(&r); r.issued = ~(Vcu64)0; f = fixture(0);
	check(vcpcownerclaim(&r, &o, &f->device) == VcInvalid && f->cfgwrites == 0,
		"controller identity never wraps"); destroy(f);
}

typedef struct Claim Claim;
struct Claim { VcPcOwners *r; VcPcOwner o; Pcidev *p; int result; };
static void*
claimthread(void *aux)
{
	Claim *c;
	c = aux; c->result = vcpcownerclaim(c->r, &c->o, c->p); return nil;
}

static void
concurrent(void)
{
	VcPcOwners r;
	VcPcOwner owners[5];
	NativeFixture *f[5];
	Claim claims[2];
	pthread_t threads[2];
	int i, winner;
	vcpcownersinit(&r);
	for(i = 0; i < 5; i++) f[i] = fixture(i);
	for(i = 0; i < 4; i++) check(vcpcownerclaim(&r, &owners[i], &f[i]->device) == 0, "bounded distinct controllers");
	check(vcpcownerclaim(&r, &owners[4], &f[4]->device) == VcInvalid && f[4]->cfgwrites == 0,
		"registry capacity rejects before PCI write");
	for(i = 0; i < 4; i++) check(vcpcownerstop(&owners[i]) == 0, "unbound controller mapping reset cleanup");
	for(i = 0; i < 5; i++) destroy(f[i]);
	vcpcownersinit(&r); f[0] = fixture(0);
	for(i = 0; i < 2; i++){
		memset(&claims[i], 0, sizeof claims[i]); claims[i].r = &r; claims[i].p = &f[0]->device;
		must(pthread_create(&threads[i], nil, claimthread, &claims[i]) == 0);
	}
	for(i = 0; i < 2; i++) must(pthread_join(threads[i], nil) == 0);
	winner = claims[0].result == 0 ? 0 : 1;
	check(claims[winner].result == 0 && claims[1-winner].result == VcInvalid &&
		f[0]->cfgwrites == 1 && f[0]->maps == 4, "real concurrent duplicate-TBDF reservation has one winner");
	check(vcpcownerstop(&claims[winner].o) == 0, "concurrent winner cleanup"); destroy(f[0]);
}

int
main(void)
{
	lifecycle(); rejects(); concurrent();
	printf("%d native PCI owner checks passed (fake native primitives, ordered controller stubs)\n", checks);
	return 0;
}
