/* SPDX-License-Identifier: MIT */
#include "vconsole-controller-fixture.h"

static VcStreamToken
openstream(Bus *b, const VcCtlRef *work)
{
	static const char name[] = "org.qemu.guest_agent.0";
	Vcu8 message[VcControlMax];
	VcStreamToken token;
	int i;
	put32(message, 1); put16(message+4, VcAdd); put16(message+6, 1);
	complete(b, 2, message, 8);
	for(i = 0; i < 4; i++) check(vccstep(&b->c, work, 256) >= 0, "process ADD");
	complete(b, 3, nil, 0); complete(b, 3, nil, 0);
	check(vccstep(&b->c, work, 256) >= 0, "reap DEVICE_READY and PORT_READY");
	put16(message+4, VcName); put16(message+6, 0);
	memmove(message+8, (void*)name, sizeof name-1);
	complete(b, 2, message, 8+sizeof name-1);
	check(vccstep(&b->c, work, 256) >= 0, "process exact name");
	put16(message+4, VcOpen); put16(message+6, 1);
	complete(b, 2, message, 8);
	check(vccstep(&b->c, work, 256) >= 0 && vccopen(&b->c, work, &token) == 0,
		"publish first stream ownership");
	check(vccstep(&b->c, work, 256) >= 0, "send guest OPEN");
	return token;
}

static void
lifecycle(void)
{
	Bus *b;
	VcCtlRef work, client, old, refs[VcCtlRefs];
	VcStreamToken token, newtoken;
	Vcu8 bytes[8] = {0, 255, 9, 13}, out[8];
	Vcu64 epoch, total;
	int i;
	b = bus(3, 4);
	check(vcchold(&b->c, VcCtlWorker, &work) == VcStreamClosed, "no admission before ready");
	check(vccprepare(&b->c, 256, VcPumpBudgetMax) == 0, "acquire and bind complete session");
	total = b->c.budget.total;
	check(b->c.budget.metadata == sizeof(VcController)+sizeof(VcMmio)+
		sizeof(VcCtlSession)+(Vcu64)b->count*sizeof(VcDmaQueue), "complete exact metadata budget");
	check(b->metacalls == 2 && b->dmacalls == b->count && b->c.allocated == b->count,
		"all allocation planned and acquired before exposure");
	check(vccstart(&b->c, irq(b, 0)) == 0 && vcchold(&b->c, VcCtlWorker, &work) == 0,
		"start enables exact one worker lease");
	check(vcchold(&b->c, VcCtlWorker, &old) == VcStreamWait, "no second worker");
	for(i = 1; i < VcCtlRefs; i++) check(vcchold(&b->c, VcCtlClient, &refs[i]) == 0, "bounded client hold");
	check(vcchold(&b->c, VcCtlClient, &old) == VcStreamWait, "bounded client exhaustion");
	client = refs[1];
	check(vccstep(&b->c, &client, 1) == VcStreamClosed, "client lifetime is not worker authority");
	for(i = 2; i < VcCtlRefs; i++) check(vccput(&b->c, &refs[i]) == 0, "release client slots");
	token = openstream(b, &work);
	complete(b, 4, bytes, 4);
	check(vccstep(&b->c, &work, 256) >= 0 && vccread(&b->c, &work, &token, out, 8) == 4 &&
		memcmp(out, bytes, 4) == 0, "literal binary stream via worker guard");
	check(vccwrite(&b->c, &work, &token, bytes, 4) == 4, "bounded guarded TX");
	check(vccclose(&b->c, &work, &token) == 0 && vccopen(&b->c, &work, &newtoken) == VcStreamReset,
		"reopen requires whole-device epoch");
	epoch = b->mm.epoch; old = work;
	check(vccreset(&b->c) == VcStreamWait && !vccjoined(&b->c) && b->dmafrees == 0 &&
		b->mm.epoch == epoch, "reset cannot race leased worker or clients");
	check(vccstep(&b->c, &work, 1) == VcStreamClosed &&
		vcchold(&b->c, VcCtlClient, &refs[2]) == VcStreamClosed, "stop closes new operations and refs");
	check(vccput(&b->c, &work) == 0 && vccreset(&b->c) == VcStreamWait,
		"worker exit alone does not join client");
	check(vccput(&b->c, &client) == 0 && vccput(&b->c, &client) == VcStreamClosed &&
		vccjoined(&b->c), "exact once lease release and real joined state");
	check(vccreset(&b->c) == 0 && b->dmafrees == b->count && b->mm.epoch > epoch,
		"joined confirmed reset releases DMA and metadata");
	check(vccprepare(&b->c, 256, total) == 0 && vccstart(&b->c, &b->irq[0]) == VcInvalid,
		"old closed mailbox cannot attach to new epoch");
	check(vccstart(&b->c, irq(b, 1)) == 0 &&
		vcchold(&b->c, VcCtlWorker, &work) == 0, "fresh exact-budget reopen");
	check(vccput(&b->c, &old) == VcStreamClosed && vccstep(&b->c, &old, 1) == VcStreamClosed,
		"old epoch cannot release or operate new worker");
	newtoken = openstream(b, &work);
	check(vccread(&b->c, &work, &token, out, 8) == VcStreamClosed &&
		vccclose(&b->c, &work, &token) == VcStreamClosed && newtoken.epoch > token.epoch,
		"old stream token cannot see or close reopened port");
	check(vccput(&b->c, &work) == 0, "final worker joined");
	destroy(b);
	b = bus(3, 4);
	check(vccprepare(&b->c, 256, total-1) == VcFault && b->metacalls == 0 &&
		b->dmaattempts == 0 && b->bound == 0, "reject one byte under budget before any allocation");
	destroy(b);
}

static void
failures(void)
{
	Bus *b;
	VcCtlRef work, old;
	Vcu64 epoch;
	int i, reads, writes;
	b = bus(3, 4);
	check(vccprepare(&b->c, 256, VcPumpBudgetMax) == 0, "measure initialization fault sites");
	reads = b->reads; writes = b->writes; destroy(b);
	for(i = 1; i <= reads; i++){
		b = bus(3, 4); b->failread = i;
		check(vccprepare(&b->c, 256, VcPumpBudgetMax) == VcFault, "every initialization read failure");
		destroy(b);
	}
	for(i = 1; i <= writes; i++){
		b = bus(3, 4); b->failwrite = i;
		check(vccprepare(&b->c, 256, VcPumpBudgetMax) == VcFault, "every initialization write failure");
		destroy(b);
	}
	for(i = 1; i <= 10; i++){
		b = bus(3, 4);
		if(i <= 2) b->failmeta = i; else b->faildma = i-2;
		check(vccprepare(&b->c, 256, VcPumpBudgetMax) == VcFault && b->bound == 0 &&
			b->dmafrees == b->dmacalls && b->metadata[0] == nil && b->metadata[1] == nil,
			"every partial allocation failure safely unwound before exposure");
		destroy(b);
	}
	b = bus(3, 4); b->badmeta = 1;
	check(vccprepare(&b->c, 256, VcPumpBudgetMax) == VcFault && b->dmaattempts == 0 &&
		b->metadata[0] == nil, "invalid metadata alignment rejected before access");
	destroy(b);
	for(i = 1; i <= 8; i++){
		b = bus(3, 4); b->failbind = i;
		check(vccprepare(&b->c, 256, VcPumpBudgetMax) == VcFault && b->c.state == VcCtlRetained &&
			b->dmafrees == 0 && b->c.allocated == b->count, "failed bind and reset retain ALL arenas");
		check(vccreset(&b->c) == VcFault && b->dmafrees == 0 && vccdetach(&b->c) == VcInvalid,
			"failed reset never permits freeing or unmapping");
		destroy(b);
	}
	b = bus(3, 4);
	check(vccprepare(&b->c, 256, VcPumpBudgetMax) == 0, "prepare startup failure fixture");
	b->failstart = 1;
	check(vccstart(&b->c, irq(b, 0)) == VcFault && b->dmafrees == 0 &&
		vcchold(&b->c, VcCtlWorker, &work) == VcStreamClosed && vcirqdrained(&b->irq[0]),
		"failed DRIVER_OK closes gate without freeing exposed memory");
	destroy(b);
	b = bus(1, 1);
	check(vccprepare(&b->c, 1, VcPumpBudgetMax) == 0 && vccstart(&b->c, irq(b, 0)) == 0,
		"single-descriptor controller supported");
	check(vcchold(&b->c, VcCtlWorker, &work) == 0 && vccput(&b->c, &work) == 0,
		"worker lease recyclable without reset");
	old = work;
	check(vcchold(&b->c, VcCtlWorker, &work) == 0 && work.serial > old.serial &&
		vccput(&b->c, &old) == VcStreamClosed, "ABA lease protection within same epoch");
	check(vccput(&b->c, &work) == 0, "release serial fixture");
	b->c.serial[0] = ~(Vcu64)0; /* isolated supervisor fault injection */
	check(vcchold(&b->c, VcCtlWorker, &work) == VcStreamWait, "lease serial never wraps");
	epoch = b->mm.epoch; b->failreset = 1;
	check(vccreset(&b->c) == VcFault && b->dmafrees == 0 && b->mm.epoch == epoch,
		"failed runtime reset retains old epoch and buffers");
	destroy(b);
	b = bus(64, 256);
	check(vccprepare(&b->c, 4096, VcPumpBudgetMax) == VcFault && b->metacalls == 0,
		"max-layout oversized aggregate budget rejected before acquisition");
	destroy(b);
	b = bus(64, 256);
	check(vccprepare(&b->c, 256, VcPumpBudgetMax) == 0 && b->c.count == VcQueuesMax &&
		b->c.budget.total <= VcPumpBudgetMax && vccstart(&b->c, irq(b, 0)) == 0,
		"maximum port/descriptor layout fits default capacity budget");
	destroy(b);
	b = bus(3, 4);
	check(vccprepare(&b->c, 256, VcPumpBudgetMax) == 0 && vccstart(&b->c, irq(b, 0)) == 0 &&
		vcchold(&b->c, VcCtlWorker, &work) == 0, "prepare runtime fault gate");
	old = work; old.identity++;
	check(vccstep(&b->c, &old, 1) == VcStreamClosed && vccput(&b->c, &old) == VcStreamClosed,
		"wrong identity does not grant mutation or release");
	old = work; old.slot = VcCtlRefs;
	check(vccstep(&b->c, &old, 1) == VcStreamClosed && vccput(&b->c, &old) == VcStreamClosed,
		"out-of-range slot rejected before indexing");
	b->failread = b->reads+1;
	check(vccstep(&b->c, &work, 1) == VcFault && b->c.state == VcCtlRetained &&
		vcchold(&b->c, VcCtlClient, &old) == VcStreamClosed && b->dmafrees == 0,
		"runtime fault closes admission before relinquishing operation guard");
	check(vccput(&b->c, &work) == 0, "faulted worker still owes its final reference");
	destroy(b);
	b = bus(3, 4);
	check(vccprepare(&b->c, 256, VcPumpBudgetMax) == 0 && vccstart(&b->c, irq(b, 0)) == 0 &&
		vcchold(&b->c, VcCtlWorker, &work) == 0, "prepare isolated ISR fault");
	b->badirq = 1;
	check(vcirqinterrupt(&b->irq[0]) == VcFault && b->status == 15 &&
		vccstep(&b->c, &work, 1) == VcFault && b->dmafrees == 0 &&
		vcchold(&b->c, VcCtlClient, &old) == VcStreamClosed,
		"latched IRQ fault closes controller even with healthy common status");
	check(vccput(&b->c, &work) == 0, "ISR-fault worker joined");
	destroy(b);
}

#ifdef QGA_HOST_TEST
static void*
runstep(void *aux)
{
	Bus *b;
	b = aux; b->threadresult = vccstep(&b->c, &b->work, 1); return nil;
}

static void*
runstart(void *aux)
{
	Bus *b;
	b = aux; b->threadresult = vccstart(&b->c, &b->irq[0]); return nil;
}

static void*
runirq(void *aux)
{
	Bus *b;
	b = aux; b->threadresult = vcirqinterrupt(&b->irq[0]); return nil;
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
	must(pthread_mutex_lock(&b->wait) == 0);
	b->released = 1; b->block = 0;
	must(pthread_cond_broadcast(&b->cond) == 0 && pthread_mutex_unlock(&b->wait) == 0);
}

static void
threaded(void)
{
	Bus *b;
	pthread_t thread;
	VcCtlRef ref;
	int i;
	for(i = 0; i < 3; i++){
		b = bus(3, 4);
		check(vccprepare(&b->c, 256, VcPumpBudgetMax) == 0, "prepare concurrent teardown");
		irq(b, 0);
		if(i != 1) check(vccstart(&b->c, &b->irq[0]) == 0, "start concurrency fixture");
		if(i == 0) check(vcchold(&b->c, VcCtlWorker, &b->work) == 0, "thread worker acquired");
		b->block = i+1;
		must(pthread_create(&thread, nil, i == 0 ? runstep : i == 1 ? runstart : runirq, b) == 0);
		waitblocked(b);
		if(i == 0){
			check(vccstep(&b->c, &b->work, 1) == VcStreamWait, "no overlapping worker mutation");
			check(vccput(&b->c, &b->work) == 0, "early lease put cannot bypass in-flight guard");
		}
		check(vccstop(&b->c) == 0 && vccreset(&b->c) == VcStreamWait &&
			vcchold(&b->c, VcCtlClient, &ref) == VcStreamClosed, "stop/reset waits on real outstanding work");
		/* Only gate/IRQ queries while other thread owns mutable MMIO/session. */
		check(i == 2 ? !vcirqdrained(&b->irq[0]) : !vccjoined(&b->c), "join cannot report premature completion");
		unblock(b); must(pthread_join(thread, nil) == 0);
		if(i == 1) check(b->threadresult == VcStreamClosed &&
			vcchold(&b->c, VcCtlWorker, &ref) == VcStreamClosed, "stop wins concurrent DRIVER_OK publication");
		check(b->dmafrees == 0 && vccreset(&b->c) == 0, "free only after operation/waker actually joined");
		destroy(b);
	}
}

static void*
holdstress(void *aux)
{
	Bus *b;
	VcCtlRef client, work, stale;
	int i, result;
	b = aux;
	for(i = 0; i < 10000; i++){
		must(vcchold(&b->c, VcCtlClient, &client) == 0);
		stale = client; must(vccput(&b->c, &client) == 0);
		must(vccput(&b->c, &stale) == VcStreamClosed);
		result = vcchold(&b->c, VcCtlWorker, &work);
		must(result == 0 || result == VcStreamWait);
		if(result == 0){
			must(vccstep(&b->c, &work, 1) >= 0);
			must(vccput(&b->c, &work) == 0);
		}
	}
	return nil;
}

static void
stress(void)
{
	Bus *b;
	pthread_t threads[4];
	int i;
	b = bus(3, 4);
	check(vccprepare(&b->c, 256, VcPumpBudgetMax) == 0 && vccstart(&b->c, irq(b, 0)) == 0,
		"prepare concurrent reference stress");
	for(i = 0; i < 4; i++) must(pthread_create(&threads[i], nil, holdstress, b) == 0);
	for(i = 0; i < 4; i++) must(pthread_join(threads[i], nil) == 0);
	check(vccstop(&b->c) == 0 && vccjoined(&b->c), "40000 concurrent lifetime/worker cycles joined");
	destroy(b);
}
#endif

#ifdef QGA_HOST_TEST
int main(void)
#else
void main(void)
#endif
{
	lifecycle(); failures();
#ifdef QGA_HOST_TEST
	threaded(); stress();
	printf("%d virtio controller lifecycle checks passed (threaded join, 40000 concurrent ref cycles)\n", checks);
	return 0;
#else
	print("%d virtio controller lifecycle checks passed\n", checks); exits(nil);
#endif
}
