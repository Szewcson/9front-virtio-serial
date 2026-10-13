/* SPDX-License-Identifier: MIT */
/* Actual supervisor transitions with deterministic native lifecycle doubles.
 * Not a hardware/kernel scheduler proof: other suites exercise real helpers. */
#include <stdio.h>
#include "../kernel/vconsole-boot-pc.c"
static int checks, call, failat, ready, joined, livefd, stopfail, withdrawn;
static int quiesced, stopped, stops, workerstops, publishcalls, claimcalls;
static Vcu64 seenlimit;
static int stage(void) { return ++call == failat ? VcFault : 0; }
static void check(int ok, const char *name)
{
	checks++;
	if(!ok){ fprintf(stderr, "FAIL: %s\n", name); exit(1); }
}
int vcpcownerclaim(VcPcOwners *r, VcPcOwner *o, Pcidev *p)
{
	int result;
	claimcalls++; result = stage();
	/* Ambiguous failed command may retain the claim. */
	o->claimed = 1; o->registry = r; o->device = p; o->identity = claimcalls;
	o->pc.mmio.epoch = 2;
	return result;
}
int vcpcctlops(VcCtlOps *o) { memset(o, 0, sizeof *o); return stage(); }
int vcpcdmaops(VcDmaOps *o) { memset(o, 0, sizeof *o); return stage(); }
int vccinit(VcController *c, VcMmio *m, const VcCtlOps *o, void *a,
	const VcDmaOps *d, void *da, Vcu64 id)
{
	USED(o); USED(a); USED(d); USED(da); c->mmio = m; c->identity = id; return stage();
}
int vcpcownerbind(VcPcOwner *o, VcController *c) { o->controller = c; return stage(); }
int vcpcownerprepare(VcPcOwner *o, int n, Vcu64 limit)
{
	USED(o); check(n == 256, "fixed frame capacity"); seenlimit = limit; return stage();
}
int vcpcwakeinit(VcPcWake *w, const VcMmio *m) { w->irq.epoch = m->epoch; return stage(); }
int vcpcioinit(VcPcIo *i, VcController *c, Vcu64 e)
{
	i->io.controller = c; i->io.epoch = e; return stage();
}
int vcpcworkerinit(VcPcWorker *w, VcController *c, VcPcWake *irq, int n, int ms)
{
	check(n == 32 && ms == BootPollMs, "bounded worker budget/deadline");
	w->worker.controller = c; w->wake = irq; return stage();
}
int vcpcworkerbindio(VcPcWorker *w, VcPcIo *i) { w->io = i; return stage(); }
int vcpcdevinit(VcPcDev *d, VcPcIo *i, VcPcWorker *w, int slot)
{
	check(slot == 0, "single unambiguous endpoint"); d->io = i; d->worker = w; return stage();
}
int vcpcownerstart(VcPcOwner *o, VcController *c, VcPcWake *w)
{
	USED(o); USED(c); USED(w); return stage();
}
int vcwstart(VcWorker *w) { USED(w); return stage(); }
int vcpcdevpublish(VcPcDev *d)
{
	USED(d); publishcalls++;
	if(!ready || joined) return VcStreamClosed;
	return 0;
}
int vcwjoined(VcWorker *w, int *result) { USED(w); *result = 0; return joined; }
int vciostatus(VcIo *i, VcIoStatus *s)
{
	USED(i); memset(s, 0, sizeof *s);
	s->stopped = stopped; s->owner = stopped ? VcIoRetired : VcIoLive;
	return 0;
}
int vcwstop(VcWorker *w) { USED(w); workerstops++; return 0; }
int vcpcownerquiesce(VcPcOwner *o) { USED(o); quiesced++; return stopfail ? VcFault : 0; }
int vcpcdevwithdraw(VcPcDev *d)
{
	USED(d); check(quiesced && joined, "quiesce/join before withdraw");
	if(livefd) return VcStreamWait;
	withdrawn++; return 0;
}
int vcpcownerstop(VcPcOwner *o)
{
	check(quiesced > 0, "quiesce before reset/drop");
	stops++; o->claimed = 0; return 0;
}

static void fresh(BootState *b)
{
	memset(b, 0, sizeof *b);
	call = failat = ready = joined = livefd = stopfail = withdrawn = 0;
	quiesced = stopped = stops = workerstops = publishcalls = claimcalls = 0;
	seenlimit = 0;
}

int main(void)
{
	BootState *b;
	BootContext *c;
	int i, j;
	b = calloc(1, sizeof *b); check(b != nil, "fixture allocation");
	for(i = 1; i <= 13; i++){
		fresh(b); failat = i; c = &b->context[0];
		check(bootacquire(b, c) < 0 && call == i, "each startup stage short-circuits");
		c->phase = BootStopping; joined = 1;
		check(bootstep(c) == 0 && c->phase == BootRetired && !c->owner.claimed,
			"startup failure cleaned without publication/retry");
		check(!c->retry && !publishcalls && stops == 1, "no silent startup retry");
	}
	fresh(b); c = &b->context[0];
	check(bootacquire(b, c) == 0 && call == 13 && c->workerstarted, "ordered complete startup");
	check(seenlimit == VcPumpBudgetMax-sizeof *b-BootAllocatorReserve,
		"full retained-context pool reserved before DMA");
	c->phase = BootLaunching;
	for(i = 0; i < BootPollMax-1; i++)
		check(bootstep(c) == VcStreamWait && !c->published && !workerstops,
			"Launched not publish proof");
	check(bootstep(c) == VcStreamWait && c->phase == BootStopping && workerstops,
		"startup poll budget triggers cooperative stop");
	joined = 1; check(bootstep(c) == 0 && !c->retry, "timeout does not retry spawn");
	fresh(b); c = &b->context[0];
	check(bootacquire(b, c) == 0, "new context"); c->phase = BootLaunching; ready = 1;
	check(bootstep(c) == 0 && c->published && c->phase == BootOnline, "actual runner publication");
	for(i = 0; i < BootPollMax+3; i++)
		check(bootstep(c) == VcStreamWait && !quiesced, "online has no arbitrary deadline");
	stopped = 1; livefd = 1;
	check(bootstep(c) == VcStreamWait && c->phase == BootStopping && !stops && !withdrawn,
		"disconnect masks DMA before waiting for worker");
	joined = 1;
	for(i = 0; i < BootPollMax-2; i++)
		check(bootstep(c) == VcStreamWait && !stops && c->published, "live FD delays reset/drop");
	check(bootstep(c) == VcFault && c->phase == BootFailed && c->owner.claimed && c->published,
		"drain timeout retains claim and endpoint");
	check(bootstep(c) == VcInvalid && !stops, "failed context fail-stops");
	fresh(b); ready = 1;
	for(j = 0; j < BootEpochs; j++){
		c = &b->context[b->used++]; joined = stopped = 0;
		check(bootacquire(b, c) == 0, "fresh epoch acquired"); c->phase = BootLaunching;
		check(bootstep(c) == 0, "fresh namespace published");
		joined = stopped = 1;
		check(bootstep(c) == 0 && c->phase == BootRetired && c->retry && !c->published,
			"withdraw then confirmed reset before fresh epoch");
		check(withdrawn == j+1 && stops == j+1, "one reset/withdraw per epoch");
	}
	check(b->used == BootEpochs, "bounded retained epoch pool");
	fresh(b); c = &b->context[0]; check(bootacquire(b, c) == 0, "PCI failure fixture");
	c->phase = BootStopping; joined = 1; stopfail = 1;
	for(i = 0; i < BootPollMax-1; i++) check(bootstep(c) == VcStreamWait, "bounded ambiguous PCI retry");
	check(bootstep(c) == VcFault && !stops && c->owner.claimed, "ambiguous writes never free");
	free(b); printf("vconsole boot tests: %d assertions passed\n", checks); return 0;
}
