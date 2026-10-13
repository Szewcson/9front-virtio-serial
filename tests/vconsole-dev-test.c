/* SPDX-License-Identifier: MIT */
/* Actual native Dev + blocking adapter + broker/controller; simulated kernel
 * Chan/error/copy/sleep primitives and coherent bus. NOT kernel execution. */
#include "vconsole-dev-shim.h"
#include "vconsole-controller-fixture.h"
#include "../kernel/vconsole-dev.h"
#include <stdio.h>
typedef struct DeviceFixture DeviceFixture;
struct DeviceFixture {
	Bus *bus;
	VcPcIo io;
	VcPcWorker worker;
	VcPcWake wake;
	VcPcDev dev;
	VcCtlRef ref;
	int sleeps, wakes, result, hold, entered, released, note;
	pthread_mutex_t workgate, state, wait;
	pthread_cond_t cond;
};
typedef struct ErrorState ErrorState;
struct ErrorState { jmp_buf frame[16]; int depth; char text[128]; };
static __thread ErrorState errors;
static __thread int nativeheld;
static __thread void *copyfault;
static __thread Dir lastdir;
__thread Proc *up;
static Proc admin = {"eve"}, stranger = {"other"};
char *eve = "eve";
static pthread_mutex_t nativelock = PTHREAD_MUTEX_INITIALIZER;
static DeviceFixture *fixturelist[VcDevMax];
static int chans, blocks, blockbytes, failattach;

jmp_buf*
testerrorpush(void)
{
	must(errors.depth < 16 && !nativeheld);
	return &errors.frame[errors.depth++];
}

void
testerrorpop(void)
{
	must(errors.depth > 0 && !nativeheld); errors.depth--;
}

_Noreturn void
error(char *s)
{
	must(errors.depth > 0 && !nativeheld);
	snprintf(errors.text, sizeof errors.text, "%s", s);
	longjmp(errors.frame[--errors.depth], 1);
}

_Noreturn void
nexterror(void)
{
	must(errors.depth > 0 && !nativeheld);
	longjmp(errors.frame[--errors.depth], 1);
}

void*
testmemmove(void *out, const void *in, size_t bytes)
{
	if(bytes > 0 && copyfault != nil && (out == copyfault || in == copyfault)){
		copyfault = nil; error("simulated user copy fault");
	}
	/* libc memmove is hidden by the test macro; memcpy suffices for nonoverlap
	 * here except walk snapshots. The builtin preserves overlap semantics. */
	return __builtin_memmove(out, in, bytes);
}

void
ilock(Lock *l)
{
	USED(l); must(nativeheld == 0 && pthread_mutex_lock(&nativelock) == 0); nativeheld = 1;
}

void
iunlock(Lock *l)
{
	USED(l); must(nativeheld == 1); nativeheld = 0; must(pthread_mutex_unlock(&nativelock) == 0);
}

int
iseve(void)
{
	return up != nil && strcmp(up->user, eve) == 0;
}

void
mkqid(Qid *q, vlong path, ulong vers, int type)
{
	q->path = path; q->vers = vers; q->type = type;
}

void
devdir(Chan *c, Qid q, char *name, vlong length, char *user, long mode, Dir *out)
{
	USED(c); out->qid = q; out->name = name; out->length = length; out->uid = user; out->mode = mode;
}

void devreset(void) {}
void devinit(void) {}
void devshutdown(void) {}

Chan*
devattach(int rune, char *spec)
{
	Chan *c;
	USED(spec); must(rune == VcDevRune && !nativeheld);
	if(failattach) error("simulated Chan allocation failure");
	c = calloc(1, sizeof *c); must(c != nil); c->refs = 1; chans++;
	return c;
}

static void
drop(Chan *c)
{
	must(c != nil && c->refs > 0);
	if(--c->refs) return; /* dup shares SAME Chan, not cloned endpoint */
	vconsoledevtab.close(c); free(c); chans--;
}

Walkqid*
devwalk(Chan *c, Chan *nc, char **name, int nname, Dirtab *tab, int ntab, Devgen *gen)
{
	Walkqid *w;
	Dir dir;
	int i, j;
	must(!(c->flag&COPEN) && (nc == nil || !(nc->flag&COPEN)) && nname <= 16);
	w = calloc(1, sizeof *w); must(w != nil);
	if(nc == nil){ nc = devattach(VcDevRune, nil); *nc = *c; nc->refs = 1; }
	w->clone = nc;
	for(j = 0; j < nname; j++){
		if(!(nc->qid.type&QTDIR)) break;
		if(strcmp(name[j], ".") == 0){ w->qid[w->nqid++] = nc->qid; continue; }
		if(strcmp(name[j], "..") == 0) i = DEVDOTDOT;
		else i = 0;
		if(gen(nc, name[j], tab, ntab, i, &dir) != 1) break;
		nc->qid = dir.qid; w->qid[w->nqid++] = nc->qid;
	}
	if(j != nname){ drop(nc); free(w); return nil; }
	return w;
}

int
convD2M(Dir *d, uchar *out, int count)
{
	int n;
	lastdir = *d;
	if(count < BIT16SZ) return 0;
	n = snprintf((char*)out, count, "%s %s %llo %llu\n", d->name, d->uid,
		(unsigned long long)d->mode, d->qid.path);
	return n >= count ? 0 : n;
}

long
devdirread(Chan *c, char *out, long count, Dirtab *tab, int ntab, Devgen *gen)
{
	Dir d;
	char buf[256];
	int i, n;
	long total;
	total = 0;
	for(i = 0; gen(c, nil, tab, ntab, i, &d) == 1; i++){
		n = convD2M(&d, (uchar*)buf, sizeof buf); must(n >= 0);
		if(total+n > count) break;
		if(n > 0) memmove(out+total, buf, n);
		total += n;
	}
	return total;
}

Chan *devcreate(Chan *c, char *name, int mode, ulong perm)
{
	USED(c); USED(name); USED(mode); USED(perm); error(Eperm);
}
void devremove(Chan *c) { USED(c); error(Eperm); }
int devwstat(Chan *c, uchar *buf, int n) { USED(c); USED(buf); USED(n); error(Eperm); }

static Block*
newblock(int count)
{
	Block *b;
	b = calloc(1, sizeof *b); must(b != nil);
	b->base = malloc(count ? count : 1); must(b->base != nil);
	b->rp = b->wp = b->base; b->capacity = count; blocks++; blockbytes += count;
	return b;
}

static void
dropblock(Block *b)
{
	blockbytes -= b->capacity; blocks--; free(b->base); free(b);
}

Block*
devbread(Chan *c, long count, ulong offset)
{
	Block *b;
	must(count >= 0 && count <= VcBufferMax); b = newblock(count);
	if(waserror()){ dropblock(b); nexterror(); }
	b->wp += vconsoledevtab.read(c, b->wp, count, offset);
	poperror(); return b;
}

long
devbwrite(Chan *c, Block *b, ulong offset)
{
	long n;
	if(waserror()){ dropblock(b); nexterror(); }
	n = vconsoledevtab.write(c, b->rp, b->wp-b->rp, offset);
	poperror(); dropblock(b); return n;
}

int
snprint(char *out, int size, const char *fmt, ...)
{
	char converted[512];
	va_list args;
	int i, j, n;
	/* Only the native %llud spelling differs from ISO printf in this fixture. */
	for(i = j = 0; fmt[i] != 0; i++){
		must(j < (int)sizeof converted-1);
		if(strncmp(fmt+i, "%llud", 5) == 0){
			memmove(converted+j, "%llu", 4); j += 4; i += 4;
		}else converted[j++] = fmt[i];
	}
	converted[j] = 0;
	va_start(args, fmt); n = vsnprintf(out, size, converted, args); va_end(args);
	return n;
}

static DeviceFixture*
fromrendez(Rendez *r)
{
	int i, j;
	for(i = 0; i < VcDevMax; i++) if(fixturelist[i] != nil)
		for(j = 0; j < VcIoSlots; j++) if(&fixturelist[i]->io.rendez[j] == r) return fixturelist[i];
	abort();
}

static void
progress(DeviceFixture *f)
{
	must(pthread_mutex_lock(&f->workgate) == 0);
	must(vccstep(&f->bus->c, &f->ref, 256) >= 0 && vciostep(&f->io.io, &f->ref, VcIoSlots) >= 0);
	must(pthread_mutex_unlock(&f->workgate) == 0);
}

void
tsleep(Rendez *r, int (*fn)(void*), void *arg, ulong ms)
{
	DeviceFixture *f;
	int note;
	must(!nativeheld && ms == 100 && up != nil); f = fromrendez(r);
	if(fn(arg)) return;
	must(pthread_mutex_lock(&f->wait) == 0); f->sleeps++;
	if(f->hold){
		f->entered = 1; must(pthread_cond_broadcast(&f->cond) == 0);
		while(!f->released) must(pthread_cond_wait(&f->cond, &f->wait) == 0);
	}
	note = f->note;
	must(pthread_mutex_unlock(&f->wait) == 0);
	if(note) error("simulated sleep note");
	progress(f);
	/* No predicate/arg retention: actual native source was inspected separately. */
}

void
wakeup(Rendez *r)
{
	DeviceFixture *f;
	f = fromrendez(r); must(pthread_mutex_lock(&f->wait) == 0);
	f->wakes++; must(pthread_cond_broadcast(&f->cond) == 0);
	must(pthread_mutex_unlock(&f->wait) == 0);
}

static void
workerlock(void *aux)
{
	DeviceFixture *f;
	f = aux; must(pthread_mutex_lock(&f->state) == 0);
}

static void
workerunlock(void *aux)
{
	DeviceFixture *f;
	f = aux; must(pthread_mutex_unlock(&f->state) == 0);
}

static DeviceFixture*
fixture(int slot)
{
	DeviceFixture *f;
	f = calloc(1, sizeof *f); check(f != nil, "heap Dev fixture");
	must(pthread_mutex_init(&f->workgate, nil) == 0 && pthread_mutex_init(&f->state, nil) == 0 &&
		pthread_mutex_init(&f->wait, nil) == 0 && pthread_cond_init(&f->cond, nil) == 0);
	f->bus = bus(3, 4);
	check(vccprepare(&f->bus->c, 256, VcPumpBudgetMax) == 0, "prepare Dev fixture before IRQ publication");
	/* Copy only fresh unpublished portable mailbox state, before start; the
	 * controller and simulated native worker bind the SAME embedded object. */
	f->wake.irq = *irq(f->bus, 0); f->wake.isr = f->bus->mm.map[VcMmIsr];
	check(vccstart(&f->bus->c, &f->wake.irq) == 0 && vcchold(&f->bus->c, VcCtlWorker, &f->ref) == 0,
		"real controller admission/worker lease for simulated native scheduling");
	check(vcpcioinit(&f->io, &f->bus->c, f->ref.epoch) == 0, "actual native blocking adapter with fake primitives");
	/* Model a running native worker, not actual kproc creation. Its exact lease
	 * and IRQ are real; tsleep drives the sole serialized service in fixtures. */
	f->worker.io = &f->io; f->worker.wake = &f->wake;
	f->worker.worker.controller = &f->bus->c; f->worker.worker.irq = &f->wake.irq;
	f->worker.worker.ops.lock = workerlock; f->worker.worker.ops.unlock = workerunlock;
	f->worker.worker.aux = f; f->worker.worker.ref = f->ref;
	f->worker.worker.held = f->worker.worker.bound = 1; f->worker.worker.phase = VcWorkerRunning;
	check(vcpcdevinit(&f->dev, &f->io, &f->worker, slot) == 0, "fresh immutable endpoint binding");
	fixturelist[slot] = f;
	return f;
}

static void
stop(DeviceFixture *f)
{
	check(vccstop(&f->bus->c) == 0 && vcioshutdown(&f->io.io) == 0, "admission closes before broker shutdown");
	check(vccput(&f->bus->c, &f->ref) == 0, "sole simulated worker surrenders last controller access");
	workerlock(f); f->worker.worker.held = 0; f->worker.worker.phase = VcWorkerDone; workerunlock(f);
}

static void
cleanup(DeviceFixture *f)
{
	check((f->dev.qid == 0 || vcpcdevwithdraw(&f->dev) == 0) && vccjoined(&f->bus->c),
		"actual client drainage required before withdrawal or unpublished cleanup");
	destroy(f->bus); fixturelist[f->dev.slot] = nil;
	must(pthread_cond_destroy(&f->cond) == 0 && pthread_mutex_destroy(&f->wait) == 0 &&
		pthread_mutex_destroy(&f->state) == 0 && pthread_mutex_destroy(&f->workgate) == 0);
	free(f);
}

static void
advertise(DeviceFixture *f)
{
	static const char name[] = "org.qemu.guest_agent.0";
	Vcu8 msg[VcControlMax];
	int i;
	put32(msg, 1); put16(msg+4, VcAdd); put16(msg+6, 1); complete(f->bus, 2, msg, 8);
	for(i = 0; i < 4; i++) progress(f);
	complete(f->bus, 3, nil, 0); complete(f->bus, 3, nil, 0); progress(f);
	put16(msg+4, VcName); put16(msg+6, 0); memmove(msg+8, name, sizeof name-1);
	complete(f->bus, 2, msg, 8+sizeof name-1); progress(f);
	put16(msg+4, VcOpen); put16(msg+6, 1); complete(f->bus, 2, msg, 8); progress(f);
}

static Chan*
walk(Chan *root, char *name)
{
	Walkqid *w;
	Chan *c;
	w = vconsoledevtab.walk(root, nil, &name, 1); check(w != nil && w->clone != nil && w->nqid == 1, "native-shaped walk captures endpoint");
	c = w->clone; free(w); return c;
}

#define DENIED(statement, expected) do { \
	if(waserror()){ check(strcmp(errors.text, expected) == 0, "expected fixed native error"); } \
	else { statement; poperror(); check(0, "operation must fail"); } \
} while(0)

static void
namespace(void)
{
	DeviceFixture *f;
	Chan *root, *data, *alias, *status, *dir;
	Walkqid *w;
	Block *b;
	uchar buf[4096], bytes[256];
	int i;
	f = fixture(0);
	f->worker.worker.phase = VcWorkerStarting;
	check(vcpcdevpublish(&f->dev) == VcStreamClosed, "no publication before worker lease/launch");
	f->worker.worker.phase = VcWorkerLaunched;
	check(vcpcdevpublish(&f->dev) == VcStreamClosed, "Launched-before-spawn is not runner entry proof");
	f->worker.worker.phase = VcWorkerRunning;
	check(vcpcdevpublish(&f->dev) == 0 && vcpcdevpublish(&f->dev) == VcInvalid, "one namespace serial/publication per endpoint");
	DENIED(vconsoledevtab.attach("4"), Ebadarg);
	DENIED(vconsoledevtab.attach("00"), Ebadarg);
	DENIED(vconsoledevtab.attach("../0"), Ebadarg);
	DENIED(vconsoledevtab.attach("1"), Enonexist);
	failattach = 1; DENIED(vconsoledevtab.attach("0"), "simulated Chan allocation failure"); failattach = 0;
	root = vconsoledevtab.attach(""); data = walk(root, "data"); alias = walk(root, "data"); status = walk(root, "status");
	check(data->aux == &f->dev && alias->aux == &f->dev && data != alias &&
		data->qid.type == QTEXCL, "aliases retain same broker, distinct unopened Chans");
	w = vconsoledevtab.walk(root, nil, nil, 0); check(w != nil && w->clone->aux == root->aux, "unopened zero-component clone shares pinned endpoint");
	dir = w->clone; free(w);
	w = vconsoledevtab.walk(root, dir, nil, 0); check(w != nil && w->clone == dir, "matching supplied unopened clone is supported"); free(w);
	dir->qid.path ^= 4;
	DENIED(vconsoledevtab.walk(root, dir, nil, 0), Ebadarg);
	dir->qid.path ^= 4; drop(dir);
	check(vconsoledevtab.stat(root, buf, sizeof buf) > 0 && lastdir.mode == (long)(DMDIR|0500), "directory mode does not use devstat's generic 0555 fallback");
	check(vconsoledevtab.stat(data, buf, sizeof buf) > 0 && lastdir.mode == (long)(DMEXCL|0600) &&
		strcmp(lastdir.uid, eve) == 0, "data metadata is eve-owned 0600/exclusive");
	check(vconsoledevtab.stat(status, buf, sizeof buf) > 0 && lastdir.mode == 0400, "status metadata is eve-only read-only");
	DENIED(vconsoledevtab.stat(data, buf, 1), Eshortstat);
	DENIED(vconsoledevtab.stat(data, nil, 8), Ebadarg);
	DENIED(vconsoledevtab.stat(data, buf, -1), Ebadarg);
	DENIED(vconsoledevtab.stat(data, nil, 0), Eshortstat);
	DENIED(vconsoledevtab.open(data, OREAD), Eperm);
	DENIED(vconsoledevtab.open(data, ORDWR|OTRUNC), Eperm);
	DENIED(vconsoledevtab.open(data, ORDWR|ORCLOSE), Eperm);
	DENIED(vconsoledevtab.open(status, OWRITE), Eperm);
	DENIED(vconsoledevtab.read(data, buf, 1, 0), Eperm);
	advertise(f); check(vconsoledevtab.open(data, ORDWR) == data && data->iounit == VcBufferMax, "actual native blocking open waits for notified guest OPEN");
	DENIED(vconsoledevtab.open(alias, ORDWR), "vconsole already opening or open");
	DENIED(vconsoledevtab.walk(data, nil, nil, 0), Ebadarg);
	DENIED(vconsoledevtab.walk(root, data, nil, 0), Ebadarg);
	DENIED(vconsoledevtab.walk(root, nil, nil, 1), Ebadarg);
	DENIED(vconsoledevtab.walk(root, nil, nil, VcDevWalkMax+1), Ebadarg);
	{
		char *bad = nil;
		DENIED(vconsoledevtab.walk(root, nil, &bad, 1), Ebadarg);
	}
	/* Even a forged COPEN alias cannot use/clunk the exact owner Chan. */
	alias->flag = COPEN; alias->mode = ORDWR;
	DENIED(vconsoledevtab.write(alias, buf, 1, 0), "vconsole data not open");
	vconsoledevtab.close(alias); alias->flag = 0;
	check(!f->dev.closed && !f->io.io.gone, "alias clunk cannot release actual owner's handle");
	check(vconsoledevtab.open(status, OREAD) == status && vconsoledevtab.read(status, buf, sizeof buf-1, 0) > 0,
		"status reads CPU snapshot without serial data or MMIO");
	DENIED(vconsoledevtab.write(status, buf, 1, 0), Eperm);
	check(vconsoledevtab.read(status, buf, sizeof buf, 1LL<<40) == 0, "large status offset cannot truncate/wrap to beginning");
	DENIED(vconsoledevtab.read(status, buf, 1, -1), Ebadarg);
	DENIED(vconsoledevtab.create(root, "ctl", ORDWR, 0600), Eperm);
	DENIED(vconsoledevtab.wstat(data, buf, 8), Eperm);
	DENIED(vconsoledevtab.remove(data), Eperm);
	for(i = 0; i < 256; i++) bytes[i] = i;
	check(vconsoledevtab.write(data, bytes, sizeof bytes, 99) == 256, "literal native blocking byte write, stream ignores positive offset");
	complete(f->bus, 5, nil, 0); progress(f);
	complete(f->bus, 4, bytes, sizeof bytes);
	check(vconsoledevtab.read(data, buf, sizeof buf, 999) == 256 && memcmp(buf, bytes, 256) == 0, "binary NUL/ff short read across actual blocking adapter");
	complete(f->bus, 4, bytes, sizeof bytes);
	b = vconsoledevtab.bread(data, 1L<<30, 0);
	check(b->capacity == VcBufferMax && b->wp-b->rp == 256 && memcmp(b->rp, bytes, 256) == 0,
		"block read clamps before allocation and preserves binary short transfer"); dropblock(b);
	b = newblock(8); b->wp += 8;
	DENIED(vconsoledevtab.bwrite(status, b, 0), Eperm); check(blocks == 0 && blockbytes == 0, "default bwrite frees block on denied write");
	up = &stranger;
	DENIED(vconsoledevtab.attach("0"), Eperm);
	DENIED(vconsoledevtab.walk(root, nil, nil, 0), Eperm);
	DENIED(vconsoledevtab.stat(data, buf, sizeof buf), Eperm);
	DENIED(vconsoledevtab.read(data, buf, 1, 0), Eperm);
	DENIED(vconsoledevtab.write(data, bytes, 1, 0), Eperm);
	DENIED(vconsoledevtab.read(status, buf, 1, 0), Eperm);
	DENIED(vconsoledevtab.bread(data, 1, 0), Eperm);
	up = &admin;
	data->refs++; drop(data); check(!f->dev.closed && !f->io.io.gone, "first dup FD close does not call native Dev close");
	stop(f); check(vcpcdevwithdraw(&f->dev) == VcStreamWait && f->bus->dmafrees == 0, "live Chan still blocks withdrawal/reset after worker Done");
	up = &stranger; drop(data); up = &admin;
	check(f->dev.closed && vccjoined(&f->bus->c), "last clunk releases exact handle even from non-eve process");
	DENIED(vconsoledevtab.open(alias, ORDWR), "vconsole needs a fresh endpoint after reset");
	drop(alias); drop(status); drop(root); cleanup(f);
}

static void
faults(void)
{
	DeviceFixture *f;
	Chan *root, *data;
	uchar input[8] = {0,255,1,2,3,4,5,6}, out[16];
	VcIoStatus status;
	f = fixture(0); check(vcpcdevpublish(&f->dev) == 0, "publish note fixture");
	root = vconsoledevtab.attach("0"); data = walk(root, "data");
	f->note = 1;
	DENIED(vconsoledevtab.open(data, ORDWR), "simulated sleep note");
	check(!f->dev.opening && !f->dev.opened && !(data->flag&COPEN), "failed blocking open clears only its reservation, never publishes COPEN");
	f->note = 0; progress(f); advertise(f);
	check(vconsoledevtab.open(data, ORDWR) == data, "unclaimed note allows safe retry after worker closes pending owner");
	copyfault = input;
	DENIED(vconsoledevtab.write(data, input, sizeof input, 0), "simulated user copy fault");
	check(vciostatus(&f->io.io, &status) == 0 && status.phase[VcIoFree] == VcIoSlots &&
		f->bus->c.queue[5].core.posted == 0, "write copy fault releases Reserved lease before publication");
	complete(f->bus, 4, input, sizeof input); copyfault = out;
	DENIED(vconsoledevtab.read(data, out, sizeof out, 0), "simulated user copy fault");
	check(vciostatus(&f->io.io, &status) == 0 && status.phase[VcIoFree] == VcIoSlots,
		"read copy fault returns Taking lease only after last output access");
	f->note = 1;
	DENIED(vconsoledevtab.read(data, out, sizeof out, 0), "simulated sleep note");
	f->note = 0;
	check(vciostatus(&f->io.io, &status) == 0 && status.phase[VcIoFree] == VcIoSlots && status.held,
		"read note releases request, not the live Chan owner");
	up = nil;
	check(vcpcioread(&f->io, &f->dev.handle, out, 1) == VcInvalid &&
		vcpcioopen(&f->io, &f->dev.handle) == VcInvalid, "no native error frame without process context");
	up = &admin;
	drop(data); progress(f); drop(root); stop(f); cleanup(f);
}

static Chan *openingchan;
static void*
openworker(void *aux)
{
	DeviceFixture *f;
	f = aux; up = &admin;
	if(waserror()){ f->result = -1; return nil; }
	vconsoledevtab.open(openingchan, ORDWR); poperror(); f->result = 0; return nil;
}

static void
opening(void)
{
	DeviceFixture *f;
	Chan *root, *data, *alias;
	pthread_t thread;
	f = fixture(0); advertise(f); check(vcpcdevpublish(&f->dev) == 0, "publish concurrent opening fixture");
	root = vconsoledevtab.attach("0"); data = walk(root, "data"); alias = walk(root, "data");
	f->hold = 1; f->note = 1; openingchan = data;
	must(pthread_create(&thread, nil, openworker, f) == 0);
	must(pthread_mutex_lock(&f->wait) == 0);
	while(!f->entered) must(pthread_cond_wait(&f->cond, &f->wait) == 0);
	must(pthread_mutex_unlock(&f->wait) == 0);
	DENIED(vconsoledevtab.open(alias, ORDWR), "vconsole already opening or open");
	check(vcpcdevwithdraw(&f->dev) == VcStreamWait && vccreset(&f->bus->c) == VcStreamWait,
		"blocked opening reservation and real broker leases prevent withdrawal/reset");
	must(pthread_mutex_lock(&f->wait) == 0); f->released = 1;
	must(pthread_cond_broadcast(&f->cond) == 0 && pthread_mutex_unlock(&f->wait) == 0);
	must(pthread_join(thread, nil) == 0);
	check(f->result == -1 && !f->dev.opening && !(data->flag&COPEN), "real threaded opening note clears reservation after caller's last access");
	drop(alias); drop(data); drop(root); stop(f); cleanup(f);
}

static void
replacement(void)
{
	DeviceFixture *old, *fresh;
	Chan *oldroot, *oldstatus, *newroot, *newdata;
	uchar buf[512];
	uvlong oldqid;
	old = fixture(0); check(vcpcdevpublish(&old->dev) == 0, "publish old endpoint");
	oldroot = vconsoledevtab.attach("0"); oldstatus = walk(oldroot, "status"); vconsoledevtab.open(oldstatus, OREAD);
	oldqid = oldroot->qid.path;
	stop(old); check(vcpcdevwithdraw(&old->dev) == 0, "withdraw stops future attachments, not old CPU status views");
	fresh = fixture(0); check(vcpcdevpublish(&fresh->dev) == 0, "same slot publishes a distinct fresh pinned endpoint");
	newroot = vconsoledevtab.attach("0"); newdata = walk(newroot, "data");
	check(newroot->aux == &fresh->dev && oldroot->aux == &old->dev && newroot->qid.path != oldqid,
		"replacement uses unique Qid serial and never redirects old Chan.aux");
	check(vconsoledevtab.read(oldstatus, buf, sizeof buf-1, 0) > 0 &&
		vcpcdevwithdraw(&old->dev) == VcStreamClosed, "old status remains CPU-readable and late withdrawal cannot remove replacement");
	/* An old unopened data walk still captures old stopped broker. */
	{
		Chan *olddata;
		olddata = walk(oldroot, "data");
		DENIED(vconsoledevtab.open(olddata, ORDWR), "vconsole disconnected"); drop(olddata);
	}
	advertise(fresh); vconsoledevtab.open(newdata, ORDWR);
	drop(oldroot); drop(oldstatus); drop(newdata); progress(fresh); drop(newroot);
	stop(fresh); cleanup(fresh);
	/* old has already been withdrawn; all test-owned Chans/callbacks joined. */
	destroy(old->bus);
	must(pthread_cond_destroy(&old->cond) == 0 && pthread_mutex_destroy(&old->wait) == 0 &&
		pthread_mutex_destroy(&old->state) == 0 && pthread_mutex_destroy(&old->workgate) == 0); free(old);
}

static void
capacity(void)
{
	DeviceFixture *f[VcDevMax], *failed;
	VcPcDev alias;
	VcDevRegistry *r;
	Chan *c;
	char spec[2] = {0,0};
	int i;
	for(i = 0; i < VcDevMax; i++){
		f[i] = fixture(i); check(vcpcdevpublish(&f[i]->dev) == 0, "bounded four-slot namespace");
		spec[0] = '0'+i; c = vconsoledevtab.attach(spec);
		check(c->aux == &f[i]->dev && c->dev == i, "attach suffix selects only exact assigned slot"); drop(c);
	}
	check(vcpcdevinit(&alias, &f[0]->io, &f[0]->worker, VcDevMax) == VcInvalid,
		"endpoint slot one past registry rejected before initialization");
	for(i = 0; i < VcDevMax; i++){ stop(f[i]); cleanup(f[i]); }
	failed = fixture(0);
	check(vcpcdevpublish(&failed->dev) == 0 && vcpcdevinit(&alias, &failed->io, &failed->worker, 1) == 0 &&
		vcpcdevpublish(&alias) == VcInvalid, "same broker/controller epoch cannot occupy a second namespace slot");
	stop(failed); cleanup(failed);
	failed = fixture(0); r = vcdevtestregistry();
	/* Test-only deliberate fault, no published endpoints/callers. Kernel has no
	 * accessor or control path for resetting/exhausting its serial counter. */
	ilock(&r->lock); r->issued = 0xfffffffeULL; iunlock(&r->lock);
	check(vcpcdevpublish(&failed->dev) == 0 && failed->dev.qid == 0xffffffffULL,
		"last representable namespace serial remains valid");
	c = vconsoledevtab.attach("0");
	check(c->qid.path == (0xffffffffULL<<2) && c->qid.path>>2 == failed->dev.qid,
		"large Qid prefix is preserved without narrowing"); drop(c);
	stop(failed); cleanup(failed);
	failed = fixture(0);
	check(vcpcdevpublish(&failed->dev) == VcInvalid && failed->dev.qid == 0,
		"namespace serial exhaustion never wraps or publishes aliased Qids");
	stop(failed); cleanup(failed);
}

int
main(void)
{
	up = &admin;
	check(vconsoledevtab.dc == VcDevRune && strcmp(vconsoledevtab.name, "vconsole") == 0, "actual native Dev table");
	namespace(); faults(); opening(); replacement(); capacity();
	check(chans == 0 && blocks == 0 && blockbytes == 0 && errors.depth == 0 && !nativeheld, "every mock Chan/block/error frame/lock released");
	printf("%d native Dev/blocking adapter checks passed (simulated kernel primitives, real broker/controller)\n", checks);
	return 0;
}
