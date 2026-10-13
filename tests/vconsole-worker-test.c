/* SPDX-License-Identifier: MIT */
#include "vconsole-controller-fixture.h"
#include "../kernel/vconsole-worker.h"
typedef struct WorkerFixture WorkerFixture;
struct WorkerFixture {
	Bus *bus;
	VcWorker worker;
	int launches, waits, yields, wakes, failspawn, failwait, failyield, autostop;
	int lockdepth, async, holdspawn, holdwait, entered, released, startresult, runresult;
	int services, shutdowns, failservice;
#ifdef QGA_HOST_TEST
	pthread_mutex_t state, wait;
	pthread_cond_t cond;
	pthread_t runner;
#endif
};

static void
workergatelock(void *aux)
{
	WorkerFixture *f;
	f = aux;
#ifdef QGA_HOST_TEST
	must(pthread_mutex_lock(&f->state) == 0);
#endif
	must(f->lockdepth++ == 0);
}

static void
workergateunlock(void *aux)
{
	WorkerFixture *f;
	f = aux; must(--f->lockdepth == 0);
#ifdef QGA_HOST_TEST
	must(pthread_mutex_unlock(&f->state) == 0);
#endif
}

static void
waitpoint(WorkerFixture *f, int launch)
{
#ifdef QGA_HOST_TEST
	must(pthread_mutex_lock(&f->wait) == 0);
	if(launch ? f->holdspawn : f->holdwait){
		f->entered = 1; must(pthread_cond_broadcast(&f->cond) == 0);
		while(!f->released) must(pthread_cond_wait(&f->cond, &f->wait) == 0);
	}
	must(pthread_mutex_unlock(&f->wait) == 0);
#else
	USED(f); USED(launch);
#endif
}

#ifdef QGA_HOST_TEST
static void*
runthread(void *aux)
{
	WorkerFixture *f;
	f = aux; f->runresult = vcwrun(&f->worker); return nil;
}
#endif

static int
spawn(void *aux, VcWorker *w)
{
	WorkerFixture *f;
	f = aux; must(w == &f->worker); f->launches++;
	waitpoint(f, 1);
	if(f->failspawn) return VcFault; /* known NO runner on failure */
#ifdef QGA_HOST_TEST
	if(f->async){ must(pthread_create(&f->runner, nil, runthread, f) == 0); return 0; }
#endif
	f->runresult = vcwrun(w); /* completion before spawn returns is legitimate */
	return 0;
}

static int
waitwork(void *aux, int deadline)
{
	WorkerFixture *f;
	f = aux; must(deadline >= 1 && deadline <= 1000); f->waits++;
	waitpoint(f, 0);
	if(f->failwait) return VcFault; /* native note/error normalized by adapter */
	if(f->autostop) must(vcwstop(&f->worker) == 0);
	return 0;
}

static int
yieldwork(void *aux)
{
	WorkerFixture *f;
	f = aux; f->yields++;
	return f->failyield ? VcFault : 0;
}

static void
joinwake(void *aux)
{
	WorkerFixture *f;
	f = aux; f->wakes++;
}

static WorkerFixture*
workerfixture(void)
{
	static const VcWorkerOps ops = {workergatelock, workergateunlock, spawn, waitwork, yieldwork, joinwake, nil, nil};
	WorkerFixture *f;
	f = malloc(sizeof *f); check(f != nil, "heap worker fixture");
	memset(f, 0, sizeof *f);
#ifdef QGA_HOST_TEST
	must(pthread_mutex_init(&f->state, nil) == 0 && pthread_mutex_init(&f->wait, nil) == 0 &&
		pthread_cond_init(&f->cond, nil) == 0);
#endif
	f->bus = bus(16, 16); f->autostop = 1;
	check(vccprepare(&f->bus->c, 256, VcPumpBudgetMax) == 0 &&
		vccstart(&f->bus->c, irq(f->bus, 0)) == 0, "running real controller for worker");
	check(vcwinit(&f->worker, &f->bus->c, &f->bus->irq[0], &ops, f, 1, 100) == 0,
		"fresh worker and bounded iteration/health deadline");
	return f;
}

static int
service(void *aux, const VcCtlRef *ref, int budget)
{
	WorkerFixture *f;
	f = aux;
	must(vccworkerref(&f->bus->c, ref) && budget == f->worker.budget);
	f->services++;
	return f->failservice ? VcFault : 0;
}

static void
shutdownservice(void *aux)
{
	WorkerFixture *f;
	f = aux;
	/* Finish closes admission BEFORE this hook; exact worker is still held. */
	must(!vccworkerref(&f->bus->c, &f->worker.ref) && f->bus->c.active[0]);
	f->shutdowns++;
}

static void
workerdestroy(WorkerFixture *f)
{
	int result;
	check(vcwjoined(&f->worker, &result), "worker published actual last controller access");
	check(f->wakes == 1, "one completion wake, even if last stopper finished it");
	destroy(f->bus);
#ifdef QGA_HOST_TEST
	must(pthread_cond_destroy(&f->cond) == 0 && pthread_mutex_destroy(&f->wait) == 0 &&
		pthread_mutex_destroy(&f->state) == 0);
#endif
	free(f);
}

static void
backlog(WorkerFixture *f)
{
	Vcu8 msg[8];
	int i;
	for(i = 1; i < 16; i++){
		put32(msg, i); put16(msg+4, VcAdd); put16(msg+6, 1);
		complete(f->bus, 2, msg, sizeof msg);
	}
}

static void
basic(void)
{
	WorkerFixture *f;
	VcWorker duplicate;
	VcCtlRef other;
	int i, result;
	for(i = 0; i < 5; i++){
		f = workerfixture();
		if(i == 1) f->failspawn = 1;
		if(i == 2) f->failwait = 1;
		if(i == 3) backlog(f);
		if(i == 4){ backlog(f); f->failyield = 1; }
		check(vcwstart(&f->worker) == (i == 1 ? VcFault : 0), "spawn result distinct from later worker result");
		check(vcwjoined(&f->worker, &result) && result ==
			(i == 1 || i == 2 || i == 4 ? VcFault : VcStreamClosed), "fault/note/cooperative cleanup");
		check(vccjoined(&f->bus->c) && f->bus->dmafrees == 0 &&
			vcchold(&f->bus->c, VcCtlWorker, &other) == VcStreamClosed,
			"worker failure closes controller but never frees DMA itself");
		check(vcwstart(&f->worker) == VcInvalid && vcwrun(&f->worker) == VcInvalid &&
			vcwstop(&f->worker) == 0 && f->launches == 1, "one-shot runner, no double release/restart");
		if(i >= 3) check(f->yields > 0, "bounded runnable burst yields for fairness");
		workerdestroy(f);
	}
	f = workerfixture();
	check(vcwstop(&f->worker) == 0 && vcwstart(&f->worker) == VcStreamClosed &&
		f->launches == 0 && vcwjoined(&f->worker, &result), "pre-start stop acquires no lease or kproc");
	check(vcchold(&f->bus->c, VcCtlWorker, &other) == 0 && vccput(&f->bus->c, &other) == 0,
		"unbound stopped context cannot close unrelated controller");
	workerdestroy(f);
	f = workerfixture();
	check(vcchold(&f->bus->c, VcCtlWorker, &other) == 0 && vcwstart(&f->worker) == VcStreamWait &&
		f->launches == 0 && vccstep(&f->bus->c, &other, 1) >= 0,
		"second worker cannot spawn or stop actual owner");
	check(vccput(&f->bus->c, &other) == 0, "real owner last access");
	workerdestroy(f);
	f = workerfixture();
	irq(f->bus, 1);
	f->worker.irq = &f->bus->irq[1]; /* isolated unpublished negative fixture */
	check(vcwstart(&f->worker) == VcInvalid && f->launches == 0 &&
		vcchold(&f->bus->c, VcCtlWorker, &other) == 0, "same-epoch wrong IRQ cannot select sleep predicate");
	check(vccput(&f->bus->c, &other) == 0, "invalid mailbox worker lease safely returned");
	workerdestroy(f);
	memset(&duplicate, 0, sizeof duplicate);
	check(vcwstart(nil) == VcInvalid && !vcwjoined(nil, &result) &&
		vcwrun(&duplicate) == VcInvalid, "invalid/null worker rejected");
	f = workerfixture();
	check(vcwstart(&f->worker) == 0 && vcwjoined(&f->worker, &result) &&
		vccreset(&f->bus->c) == 0 && vccprepare(&f->bus->c, 256, VcPumpBudgetMax) == 0 &&
		vccstart(&f->bus->c, irq(f->bus, 1)) == 0, "prepare new epoch after old worker joined");
	check(vcwstop(&f->worker) == 0 && vcchold(&f->bus->c, VcCtlWorker, &other) == 0 &&
		vccstep(&f->bus->c, &other, 1) >= 0, "late old-worker stop cannot close newly reopened epoch");
	check(vccput(&f->bus->c, &other) == 0, "new owner last access");
	workerdestroy(f);
}

static void
hooks(void)
{
	WorkerFixture *f;
	VcWorker unused;
	VcWorkerOps ops;
	int i, result;
	for(i = 0; i < 3; i++){
		f = workerfixture();
		/* Configure immutable optional hooks BEFORE publishing this worker. */
		f->worker.ops.service = service; f->worker.ops.shutdown = shutdownservice;
		if(i == 1) f->failservice = 1;
		if(i == 2) f->failspawn = 1;
		check(vcwstart(&f->worker) == (i == 2 ? VcFault : 0) &&
			vcwjoined(&f->worker, &result) && f->shutdowns == 1 &&
			f->services == (i == 2 ? 0 : 1), "service and finish run once with exact worker lifetime");
		check(result == (i == 0 ? VcStreamClosed : VcFault), "request service failure propagates to worker result");
		workerdestroy(f);
	}
	f = workerfixture(); ops = f->worker.ops; ops.service = service;
	check(vcwinit(&unused, &f->bus->c, &f->bus->irq[0], &ops, f, 1, 100) == VcInvalid,
		"service without finish hook rejected before publication");
	check(vcwstart(&f->worker) == 0, "finish negative-hook fixture"); workerdestroy(f);
}

#ifdef QGA_HOST_TEST
static void
awaitworker(WorkerFixture *f)
{
	must(pthread_mutex_lock(&f->wait) == 0);
	while(!f->entered) must(pthread_cond_wait(&f->cond, &f->wait) == 0);
	must(pthread_mutex_unlock(&f->wait) == 0);
}

static void
releaseworker(WorkerFixture *f)
{
	must(pthread_mutex_lock(&f->wait) == 0); f->released = 1;
	must(pthread_cond_broadcast(&f->cond) == 0 && pthread_mutex_unlock(&f->wait) == 0);
}

static void*
startthread(void *aux)
{
	WorkerFixture *f;
	f = aux; f->startresult = vcwstart(&f->worker); return nil;
}

static void*
stopthread(void *aux)
{
	WorkerFixture *f;
	f = aux; must(vcwstop(&f->worker) == 0); return nil;
}

static void
waitblocked(Bus *b)
{
	must(pthread_mutex_lock(&b->wait) == 0);
	while(!b->entered) must(pthread_cond_wait(&b->cond, &b->wait) == 0);
	must(pthread_mutex_unlock(&b->wait) == 0);
}

static void
unblock(Bus *b)
{
	must(pthread_mutex_lock(&b->wait) == 0); b->released = 1; b->block = 0;
	must(pthread_cond_broadcast(&b->cond) == 0 && pthread_mutex_unlock(&b->wait) == 0);
}

static void
threads(void)
{
	WorkerFixture *f;
	pthread_t launch, stop;
	int result, i;
	for(i = 0; i < 20; i++){
		f = workerfixture(); f->async = 1; f->holdwait = 1; f->autostop = 0;
		check(vcwstart(&f->worker) == 0, "actual pthread runner publication");
		awaitworker(f); result = 77;
		check(!vcwjoined(&f->worker, &result) && result == 77 &&
			vccreset(&f->bus->c) == VcStreamWait && f->bus->dmafrees == 0,
			"blocked predicate/worker keeps lease, output and DMA intact");
		check(vcwstop(&f->worker) == 0 && vcwrun(&f->worker) == VcInvalid,
			"duplicate runner cannot release blocked worker lease");
		releaseworker(f); must(pthread_join(f->runner, nil) == 0);
		check(vcwjoined(&f->worker, &result) && result == VcStreamClosed,
			"join only after sleep predicate actually returned");
		workerdestroy(f);
	}
	f = workerfixture(); f->async = 1; f->holdspawn = 1;
	must(pthread_create(&launch, nil, startthread, f) == 0); awaitworker(f);
	check(vcwstop(&f->worker) == 0 && vccreset(&f->bus->c) == VcStreamWait &&
		!vcwjoined(&f->worker, &result), "stop during resource-blocked spawn does not falsely join");
	releaseworker(f); must(pthread_join(launch, nil) == 0 && pthread_join(f->runner, nil) == 0);
	check(f->startresult == 0 && vcwjoined(&f->worker, &result) && result == VcStreamClosed,
		"delayed spawned worker observes stop before first controller operation");
	workerdestroy(f);
	f = workerfixture(); f->async = 1; f->holdwait = 1; f->autostop = 0;
	check(vcwstart(&f->worker) == 0, "prepare blocked external stop callback"); awaitworker(f);
	f->bus->block = 3; /* runner is blocked; no wake callout reads this yet */
	must(pthread_create(&stop, nil, stopthread, f) == 0); waitblocked(f->bus);
	releaseworker(f); must(pthread_join(f->runner, nil) == 0);
	check(!vcwjoined(&f->worker, &result) && vccreset(&f->bus->c) == VcStreamWait,
		"runner return does not drop lease still borrowed by stop callback");
	unblock(f->bus); must(pthread_join(stop, nil) == 0);
	check(vcwjoined(&f->worker, &result) && result == VcStreamClosed && vccjoined(&f->bus->c),
		"last stopper completes one-shot lease release and join");
	workerdestroy(f);
}
#endif

#ifdef QGA_HOST_TEST
int main(void)
#else
void main(void)
#endif
{
	basic(); hooks();
#ifdef QGA_HOST_TEST
	threads(); printf("%d virtio worker checks passed (real threaded last-access joins)\n", checks); return 0;
#else
	print("%d virtio worker checks passed\n", checks); exits(nil);
#endif
}
