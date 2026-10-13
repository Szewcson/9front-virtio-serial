/* SPDX-License-Identifier: MIT */
#include "vconsole-controller-fixture.h"
#include "../kernel/vconsole-io.h"
typedef struct IoFixture IoFixture;
struct IoFixture {
	Bus *bus;
	VcIo io;
	VcCtlRef work;
	VcIoHandle handle;
	int wakes[VcIoSlots], result;
#ifdef QGA_HOST_TEST
	pthread_mutex_t gate;
	int finished;
#endif
};

static void
iolock(void *aux)
{
#ifdef QGA_HOST_TEST
	IoFixture *f;
	f = aux; must(pthread_mutex_lock(&f->gate) == 0);
#else
	USED(aux);
#endif
}

static void
iounlock(void *aux)
{
#ifdef QGA_HOST_TEST
	IoFixture *f;
	f = aux; must(pthread_mutex_unlock(&f->gate) == 0);
#else
	USED(aux);
#endif
}

static void
iowake(void *aux, int slot)
{
	IoFixture *f;
	f = aux; must(slot >= 0 && slot < VcIoSlots);
	iolock(f); f->wakes[slot]++; iounlock(f);
}

static IoFixture*
fixture(void)
{
	static const VcIoOps ops = {iolock, iounlock, iowake};
	IoFixture *f;
	f = malloc(sizeof *f); check(f != nil, "heap request fixture"); memset(f, 0, sizeof *f);
#ifdef QGA_HOST_TEST
	must(pthread_mutex_init(&f->gate, nil) == 0);
#endif
	f->bus = bus(3, 4);
	check(vccprepare(&f->bus->c, 256, VcPumpBudgetMax) == 0 &&
		vccstart(&f->bus->c, irq(f->bus, 0)) == 0 &&
		vcchold(&f->bus->c, VcCtlWorker, &f->work) == 0, "running exclusive worker fixture");
	check(vcioinit(&f->io, &f->bus->c, f->work.epoch, &ops, f) == 0, "fresh boot-pinned broker");
	return f;
}

static void
cleanup(IoFixture *f)
{
	check(vccstop(&f->bus->c) == 0 && vcioshutdown(&f->io) == 0, "close admission before broker finish");
	check(vccput(&f->bus->c, &f->work) == 0 && vccjoined(&f->bus->c), "every caller and worker actually joined");
	destroy(f->bus);
#ifdef QGA_HOST_TEST
	must(pthread_mutex_destroy(&f->gate) == 0);
#endif
	free(f);
}

static void
step(IoFixture *f)
{
	check(vccstep(&f->bus->c, &f->work, 256) >= 0 &&
		vciostep(&f->io, &f->work, VcIoSlots) >= 0, "controller then bounded request service");
}

static int
take(IoFixture *f, const VcIoToken *t, Vcu8 *out, int capacity)
{
	Vcu8 *bytes;
	int result;
	check(vcioready(&f->io, t) && vcioresult(&f->io, t, &result, &bytes) == 0,
		"predicate sees complete request before taking bytes");
	if(result > 0 && out != nil){
		check(result <= capacity, "bounded output copy"); memmove(out, bytes, result);
	}
	check(vcioreap(&f->io, t) == 0 && vcioreap(&f->io, t) == VcStreamClosed,
		"last buffer access releases exactly one lifetime lease");
	return result;
}

static void
advertise(IoFixture *f)
{
	static const char name[] = "org.qemu.guest_agent.0";
	Vcu8 message[VcControlMax];
	int i;
	put32(message, 1); put16(message+4, VcAdd); put16(message+6, 1);
	complete(f->bus, 2, message, 8);
	for(i = 0; i < 4; i++) step(f);
	complete(f->bus, 3, nil, 0); complete(f->bus, 3, nil, 0); step(f);
	put16(message+4, VcName); put16(message+6, 0); memmove(message+8, (void*)name, sizeof name-1);
	complete(f->bus, 2, message, 8+sizeof name-1); step(f);
	put16(message+4, VcOpen); put16(message+6, 1); complete(f->bus, 2, message, 8); step(f);
}

static void
openio(IoFixture *f)
{
	VcIoToken t, other;
	VcIoHandle h;
	check(vcioopen(&f->io, &f->handle, &t) == 0 && !vcioready(&f->io, &t), "open enqueues without touching queues");
	check(vcioopen(&f->io, &h, &other) == VcStreamWait, "single owner across duplicate node opens");
	step(f); check(!vcioready(&f->io, &t), "absent host port waits without busy looping");
	advertise(f);
	check(vcioready(&f->io, &t), "guest OPEN was posted/notified independently of host OPEN");
	step(f); check(take(f, &t, nil, 0) == 0, "open completes after guest OPEN");
}

static VcIoToken
submit(IoFixture *f, int op, const Vcu8 *input, int count)
{
	VcIoToken t;
	Vcu8 *bytes;
	check(vcioreserve(&f->io, &f->handle, op, count, &t, &bytes) == 0, "reserve bounded private staging slot");
	if(op == VcIoWrite && count > 0) memmove(bytes, (void*)input, count);
	check(vciocommit(&f->io, &t) == 0, "publish only after last private input access");
	return t;
}

static void
duplex(void)
{
	IoFixture *f;
	VcIoToken read, write, t, stale;
	VcIoHandle h;
	Vcu8 bytes[4096], out[4096], *private;
	int i;
	f = fixture(); openio(f);
	for(i = 0; i < (int)sizeof bytes; i++) bytes[i] = i*17;
	read = submit(f, VcIoRead, nil, 4096); write = submit(f, VcIoWrite, bytes, 4096);
	step(f);
	check(!vcioready(&f->io, &read) && take(f, &write, nil, 0) == 256,
		"blocked RX does not block short TX on same full-duplex handle");
	complete(f->bus, 4, bytes, 256); step(f);
	check(take(f, &read, out, sizeof out) == 256 && memcmp(bytes, out, 256) == 0,
		"literal NUL/ff byte stream, bounded short read");
	complete(f->bus, 5, nil, 0); step(f);
	for(i = 0; i < 4; i++){
		t = submit(f, VcIoWrite, bytes, 8); step(f); check(take(f, &t, nil, 0) == 8, "fill private TX slots");
	}
	t = submit(f, VcIoWrite, bytes, 8); step(f);
	check(!vcioready(&f->io, &t), "full TX ring holds request, not overwritten/replayed");
	complete(f->bus, 5, nil, 0); step(f); check(take(f, &t, nil, 0) == 8, "exact completion releases TX backpressure");
	t = submit(f, VcIoRead, nil, 0); step(f); check(take(f, &t, nil, 0) == 0, "zero read does not need host bytes");
	stale = t;
	check(vcioreserve(&f->io, &f->handle, VcIoRead, 4097, &t, &private) == VcInvalid &&
		vcioreserve(&f->io, &f->handle, VcIoRead, -1, &t, &private) == VcInvalid,
		"length bounds checked before acquisition or payload exposure");
	t = submit(f, VcIoRead, nil, 0);
	check(vcioabandon(&f->io, &stale) == VcStreamClosed && !vcioready(&f->io, &t),
		"stale serial cannot cancel a recycled request slot");
	step(f); check(take(f, &t, nil, 0) == 0, "fresh request unaffected by stale release");
	h = f->handle; h.generation++;
	check(vcioclose(&f->io, &h) == VcStreamClosed && f->io.owner == VcIoLive, "stale handle cannot close owner");
	check(vcioclose(&f->io, &f->handle) == 0, "clunk transfers final handle access to worker");
	step(f);
	check(f->io.owner == VcIoRetired && vcioopen(&f->io, &h, &t) == VcStreamClosed,
		"published owner never reopens queues without a fresh full reset");
	cleanup(f);
}

static void
hostreconnects(void)
{
	IoFixture *f;
	VcIoToken read, write;
	VcIoStatus status;
	Vcu8 message[8], bytes[3], out[3];
	int i;
	f = fixture(); openio(f);
	put32(message, 1); put16(message+4, VcOpen);
	bytes[0] = 0; bytes[1] = 0xff; bytes[2] = 'q';
	for(i = 0; i < 65; i++){
		put16(message+6, 0); complete(f->bus, 2, message, 8); step(f);
		read = submit(f, VcIoRead, nil, 3); write = submit(f, VcIoWrite, bytes, 3); step(f);
		check(!vcioready(&f->io, &read) && !vcioready(&f->io, &write),
			"backend close waits without EOF or cancelling requests");
		check(vciostatus(&f->io, &status) == 0 && !status.stopped && status.claimed &&
			status.held && status.owner == VcIoLive && status.generation == f->handle.generation &&
			status.epoch == f->handle.epoch && status.identity == f->handle.identity,
			"one real client handle/epoch across per-RPC backend reconnects");
		put16(message+6, 1); complete(f->bus, 2, message, 8); step(f);
		check(take(f, &write, nil, 0) == 3, "queued TX resumes without resetting owner");
		complete(f->bus, 4, bytes, 3); complete(f->bus, 5, nil, 0); step(f);
		check(take(f, &read, out, sizeof out) == 3 && memcmp(bytes, out, sizeof out) == 0,
			"binary response received on same broker after reconnect");
	}
	check(vcioclose(&f->io, &f->handle) == 0, "guest clunk remains terminal"); step(f); cleanup(f);
}

static void
cancel(void)
{
	IoFixture *f;
	VcIoToken t, old, pending[VcIoSlots], extra;
	VcIoHandle oldhandle, h;
	VcCtlRef fake;
	Vcu8 *bytes;
	int i, result;
	f = fixture();
	check(vcioopen(&f->io, &h, &t) == 0, "cancel unpublished opening"); oldhandle = h;
	check(vcioabandon(&f->io, &t) == 0 && vcioclose(&f->io, &h) == 0, "opening note relinquishes request and handle");
	step(f); check(f->io.owner == VcIoUnused, "unclaimed close can safely open a fresh handle generation");
	openio(f);
	check(vcioclose(&f->io, &oldhandle) == VcStreamClosed, "old unclaimed handle cannot close successor");
	for(i = 0; i < VcIoSlots; i++) pending[i] = submit(f, VcIoRead, nil, 8);
	check(vcioreserve(&f->io, &f->handle, VcIoRead, 8, &extra, &bytes) == VcStreamWait,
		"fixed eight-slot staging pool, no unbounded waiter allocation");
	step(f);
	fake = f->work; fake.serial++;
	check(vciostep(&f->io, &fake, VcIoSlots) == VcStreamClosed, "same epoch alone is not worker authority");
	for(i = 0; i < VcIoSlots; i++) check(vcioabandon(&f->io, &pending[i]) == 0, "pending read note releases private lease");
	check(vcioreserve(&f->io, &f->handle, VcIoWrite, 8, &t, &bytes) == 0, "reserve not-yet-copied input");
	memset(bytes, 0xa5, 8); old = t;
	check(vccstop(&f->bus->c) == 0 && vcioshutdown(&f->io) == 0 && bytes[0] == 0xa5,
		"shutdown cannot zero/free Reserved caller's input buffer");
	check(vccreset(&f->bus->c) == VcStreamWait && f->bus->dmafrees == 0, "shutdown is not proof caller buffers joined");
	check(vciocommit(&f->io, &t) == 0 && vcioresult(&f->io, &t, &result, &bytes) == 0 && result == VcStreamClosed,
		"post-stop commit completes closed without publishing to device");
	check(vcioreap(&f->io, &t) == 0 && vcioabandon(&f->io, &old) == VcStreamClosed, "Reserved cancellation cannot double-put");
	check(!vccjoined(&f->bus->c) && vcioclose(&f->io, &f->handle) == 0, "live Chan must clunk even after shutdown");
	cleanup(f);
	f = fixture(); openio(f);
	t = submit(f, VcIoRead, nil, 4); complete(f->bus, 4, (Vcu8*)"test", 4); step(f);
	check(vcioresult(&f->io, &t, &result, &bytes) == 0 && result == 4, "Taking owns completed output");
	check(vcioclose(&f->io, &f->handle) == 0 && vccstop(&f->bus->c) == 0 && vcioshutdown(&f->io) == 0 &&
		memcmp(bytes, "test", 4) == 0, "Taking bytes survive clunk/shutdown until last output copy");
	check(vccreset(&f->bus->c) == VcStreamWait && vcioabandon(&f->io, &t) == 0,
		"note during output copy releases exactly retained request lease");
	cleanup(f);
}

static void
exhaustion(void)
{
	IoFixture *f;
	VcIoToken t;
	VcIoHandle h;
	int i;
	f = fixture();
	for(i = 0; i < VcIoSlots; i++) f->io.request[i].serial = ~(Vcu64)0;
	check(vcioopen(&f->io, &h, &t) == VcStreamWait && !f->io.ownerheld, "request serials never wrap");
	f->io.request[0].serial = 0; f->io.generation = ~(Vcu64)0;
	check(vcioopen(&f->io, &h, &t) == VcStreamWait && !f->io.ownerheld, "owner generations never wrap");
	cleanup(f);
}

static void
epochs(void)
{
	IoFixture *f;
	VcIo *fresh;
	VcIoToken old, t;
	VcIoHandle oldhandle, h;
	VcIoStatus status;
	Vcu8 *bytes;
	int i;
	f = fixture(); openio(f); oldhandle = f->handle;
	old = submit(f, VcIoRead, nil, 0); step(f); check(take(f, &old, nil, 0) == 0, "retain only stale CPU tokens");
	check(vcioclose(&f->io, &f->handle) == 0 && vccstop(&f->bus->c) == 0 && vcioshutdown(&f->io) == 0 &&
		vccput(&f->bus->c, &f->work) == 0 && vccreset(&f->bus->c) == 0 &&
		vccprepare(&f->bus->c, 256, VcPumpBudgetMax) == 0 && vccstart(&f->bus->c, irq(f->bus, 1)) == 0 &&
		vcchold(&f->bus->c, VcCtlWorker, &f->work) == 0, "full confirmed reset creates new actual session epoch");
	check(vcioopen(&f->io, &h, &t) == VcStreamClosed && vcioshutdown(&f->io) == 0 &&
		vccworkerref(&f->bus->c, &f->work), "late old broker cannot hold/release/close fresh controller");
	for(i = 1; i < VcCtlRefs; i++) check(!f->bus->c.active[i], "old broker epoch rejection unwinds temporary new leases");
	fresh = malloc(sizeof *fresh); check(fresh != nil, "fresh epoch broker is distinct boot-pinned object");
	check(vcioinit(fresh, &f->bus->c, f->work.epoch, &f->io.ops, f) == 0 &&
		vcioopen(fresh, &h, &t) == 0, "fresh epoch can accept a new owner");
	check(vcioclose(fresh, &oldhandle) == VcStreamClosed && vcioabandon(fresh, &old) == VcStreamClosed &&
		vcioready(fresh, &old), "old owner and request tokens cannot affect successor epoch");
	old = t; old.slot = -1; check(vcioabandon(fresh, &old) == VcStreamClosed, "negative slot rejected before indexing");
	old.slot = VcIoSlots; check(vcioresult(fresh, &old, &i, &bytes) == VcStreamClosed, "one-past slot rejected before indexing");
	check(vciostatus(fresh, &status) == 0 && status.epoch == f->work.epoch && status.owner == VcIoOpening &&
		status.phase[VcIoQueued] == 1 && status.phase[VcIoFree] == VcIoSlots-1,
		"readable status is a consistent bounded CPU-only snapshot");
	check(vccstop(&f->bus->c) == 0 && vcioshutdown(fresh) == 0 &&
		vcioready(fresh, &t) && vcioresult(fresh, &t, &i, &bytes) == 0 && i == VcStreamClosed &&
		vcioreap(fresh, &t) == 0 && vcioclose(fresh, &h) == 0,
		"shutdown wakes queued opening and waits for caller/handle last access");
	/* All fixture threads/callbacks have actually joined before test-only free. */
	free(fresh); cleanup(f);
}

#ifdef QGA_HOST_TEST
static int
blockwrite(void *aux, void *base, int off, int width, Vcu32 value)
{
	Bus *b;
	b = aux;
	if(base == b->notify) block(b, 4);
	return writereg(aux, base, off, width, value);
}

static void*
servicethread(void *aux)
{
	IoFixture *f;
	f = aux; f->result = vciostep(&f->io, &f->work, VcIoSlots); return nil;
}

static void
busy(void)
{
	IoFixture *f;
	VcIoToken t;
	Vcu8 data[8] = {0,255,1,2,3,4,5,6};
	pthread_t thread;
	f = fixture(); openio(f);
	f->bus->mm.ops.write = blockwrite; f->bus->block = 4;
	t = submit(f, VcIoWrite, data, sizeof data);
	must(pthread_create(&thread, nil, servicethread, f) == 0);
	must(pthread_mutex_lock(&f->bus->wait) == 0);
	while(!f->bus->entered) must(pthread_cond_wait(&f->bus->cond, &f->bus->wait) == 0);
	must(pthread_mutex_unlock(&f->bus->wait) == 0);
	check(vcioabandon(&f->io, &t) == 0 && vcioclose(&f->io, &f->handle) == 0,
		"note and clunk while worker still owns TX buffer");
	check(vccreset(&f->bus->c) == VcStreamWait && f->bus->dmafrees == 0,
		"actual blocked worker/request/owner leases prevent reset/free");
	must(pthread_mutex_lock(&f->bus->wait) == 0);
	f->bus->released = 1; must(pthread_cond_broadcast(&f->bus->cond) == 0);
	must(pthread_mutex_unlock(&f->bus->wait) == 0);
	must(pthread_join(thread, nil) == 0);
	check(f->result >= 0 && vcioabandon(&f->io, &t) == VcStreamClosed,
		"Busy abandonment releases only after worker's last payload access");
	check(f->bus->c.queue[5].core.posted == 1,
		"cancelled in-flight write can already be posted; never silently replayed");
	cleanup(f);
}

static void*
producer(void *aux)
{
	IoFixture *f;
	VcIoToken t;
	Vcu8 *bytes;
	int i, result;
	f = aux;
	for(i = 0; i < 500; i++){
		do result = vcioreserve(&f->io, &f->handle, (i&1) ? VcIoRead : VcIoWrite, 0, &t, &bytes);
		while(result == VcStreamWait);
		must(result == 0 && vciocommit(&f->io, &t) == 0);
		while(!vcioready(&f->io, &t)) ;
		must(vcioresult(&f->io, &t, &result, &bytes) == 0 && result == 0);
		must(vcioreap(&f->io, &t) == 0 && vcioabandon(&f->io, &t) == VcStreamClosed);
	}
	return nil;
}

static void*
consumer(void *aux)
{
	IoFixture *f;
	int done;
	f = aux;
	for(;;){
		iolock(f); done = f->finished; iounlock(f);
		if(done) return nil;
		must(vccstep(&f->bus->c, &f->work, 256) >= 0 && vciostep(&f->io, &f->work, VcIoSlots) >= 0);
	}
}

static void
stress(void)
{
	IoFixture *f;
	pthread_t clients[4], worker;
	int i;
	f = fixture(); openio(f);
	must(pthread_create(&worker, nil, consumer, f) == 0);
	for(i = 0; i < 4; i++) must(pthread_create(&clients[i], nil, producer, f) == 0);
	for(i = 0; i < 4; i++) must(pthread_join(clients[i], nil) == 0);
	iolock(f); f->finished = 1; iounlock(f);
	must(pthread_join(worker, nil) == 0);
	check(vcioclose(&f->io, &f->handle) == 0, "2000 concurrent request cycles release handle exactly once");
	cleanup(f);
}
#endif

#ifdef QGA_HOST_TEST
int main(void)
#else
void main(void)
#endif
{
	duplex(); hostreconnects(); cancel(); exhaustion(); epochs();
#ifdef QGA_HOST_TEST
	busy(); stress(); printf("%d virtio request broker checks passed (blocked TX cancellation, 2000 concurrent requests)\n", checks); return 0;
#else
	print("%d virtio request broker checks passed\n", checks); exits(nil);
#endif
}
