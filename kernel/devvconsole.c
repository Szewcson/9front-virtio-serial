/* SPDX-License-Identifier: MIT */
#ifdef QGA_HOST_TEST
#include "../tests/vconsole-dev-shim.h"
#else
#include <u.h>
#include "/sys/src/9/port/lib.h"
#include "/sys/src/9/pc64/mem.h"
#include "/sys/src/9/pc64/dat.h"
#include "/sys/src/9/pc64/fns.h"
#include "/sys/src/9/port/error.h"
#define QGA_VCONSOLE_KERNEL
#endif
#include "vconsole-dev.h"

enum { Qdir = 0, Qdata, Qstatus, Qcount, StatusMax = 512 };
static VcDevRegistry registry;
static char *names[Qcount] = {".", "data", "status"};

#ifdef QGA_HOST_TEST
/* Explicit host-only fault injection, not a kernel/user control interface. */
void *vcdevtestregistry(void) { return &registry; }
#endif

int
vcpcdevinit(VcPcDev *d, VcPcIo *io, VcPcWorker *w, int slot)
{
	if(d == nil || io == nil || w == nil || slot < 0 || slot >= VcDevMax ||
	   io->io.identity == 0 || io->io.epoch == 0 || w->io != io || w->wake == nil ||
	   w->worker.controller != io->io.controller || io->io.epoch != w->wake->irq.epoch ||
	   w->worker.irq != &w->wake->irq)
		return VcInvalid;
	memset(d, 0, sizeof *d);
	d->io = io; d->worker = w; d->slot = slot;
	return 0;
}

int
vcpcdevpublish(VcPcDev *d)
{
	VcWorker *w;
	VcCtlRef ref;
	VcIoStatus status;
	int live, i;
	if(d == nil || d->io == nil || d->worker == nil || d->slot < 0 || d->slot >= VcDevMax)
		return VcInvalid;
	w = &d->worker->worker;
	if(w->ops.lock == nil || w->ops.unlock == nil || w->controller != d->io->io.controller ||
	   d->worker->io != d->io || d->worker->wake == nil || w->irq != &d->worker->wake->irq ||
	   w->irq->epoch != d->io->io.epoch)
		return VcInvalid;
	w->ops.lock(w->aux);
	/* Launched is set BEFORE spawn; it cannot prove a kproc has entered. */
	live = w->phase == VcWorkerRunning && w->held && w->bound;
	ref = w->ref;
	w->ops.unlock(w->aux);
	if(!live || !vccworkerirq(w->controller, &ref, w->irq) ||
	   vciostatus(&d->io->io, &status) < 0 || status.stopped || ref.epoch != status.epoch ||
	   ref.identity != status.identity) return VcStreamClosed;
	ilock(&registry.lock);
	/* Same-object and namespace-serial checks precede any modification. A
	 * racing stop can publish a closed endpoint, never a dangling one. */
	live = d->qid == 0 && registry.slot[d->slot] == nil && registry.issued < 0xffffffffULL;
	for(i = 0; i < VcDevMax; i++){
		if(registry.slot[i] == nil) continue;
		if(registry.slot[i] == d || registry.slot[i]->io == d->io ||
		   (registry.slot[i]->io->io.controller == d->io->io.controller &&
		    registry.slot[i]->io->io.epoch == d->io->io.epoch)) live = 0;
	}
	if(live){ d->qid = ++registry.issued; registry.slot[d->slot] = d; }
	iunlock(&registry.lock);
	return live ? 0 : VcInvalid;
}

int
vcpcdevwithdraw(VcPcDev *d)
{
	VcIoStatus status;
	int done, result;
	if(d == nil || d->io == nil || d->worker == nil || d->slot < 0 || d->slot >= VcDevMax)
		return VcInvalid;
	if(!vcwjoined(&d->worker->worker, &result) || !vccjoined(d->io->io.controller) ||
	   vciostatus(&d->io->io, &status) < 0 || !status.stopped) return VcStreamWait;
	ilock(&d->lock); done = !d->opening && (!d->opened || d->closed); iunlock(&d->lock);
	if(!done) return VcStreamWait;
	ilock(&registry.lock);
	done = registry.slot[d->slot] == d;
	if(done) registry.slot[d->slot] = nil;
	iunlock(&registry.lock);
	return done ? 0 : VcStreamClosed;
}

static void
allowed(void)
{
	/* Dev entry points are syscall/process context. Close is deliberately
	 * exempt: clunk workers or a recipient of a delegated FD must release it. */
	if(!iseve()) error(Eperm);
}

static Qid
qid(VcPcDev *d, int kind)
{
	Qid q;
	mkqid(&q, (d->qid<<2)|kind, 0, kind == Qdir ? QTDIR : kind == Qdata ? QTEXCL : QTFILE);
	return q;
}

static VcPcDev*
endpoint(Chan *c)
{
	VcPcDev *d;
	Qid expected;
	int kind;
	if(c == nil || c->aux == nil) error(Ebadarg);
	d = c->aux; kind = c->qid.path&3;
	if(kind >= Qcount || d->qid == 0 || c->dev != d->slot || c->qid.path>>2 != d->qid) error(Ebadarg);
	expected = qid(d, kind);
	if(c->qid.type != expected.type || c->qid.vers != expected.vers) error(Ebadarg);
	return d;
}

static void
entry(Chan *c, VcPcDev *d, int kind, Dir *dir)
{
	devdir(c, qid(d, kind), names[kind], 0, eve,
		kind == Qdir ? DMDIR|0500 : kind == Qdata ? DMEXCL|0600 : 0400, dir);
}

static int
vconsolegen(Chan *c, char *name, Dirtab*, int, int i, Dir *dir)
{
	VcPcDev *d;
	int kind;
	d = endpoint(c);
	if(i == DEVDOTDOT) kind = Qdir;
	else if(name != nil){
		for(kind = Qdata; kind < Qcount; kind++) if(strcmp(name, names[kind]) == 0) break;
		if(kind == Qcount) return -1;
	}else{
		if(i < 0 || i >= Qcount-1) return -1;
		kind = i+1;
	}
	entry(c, d, kind, dir);
	return 1;
}

static Chan*
vconsoleattach(char *spec)
{
	VcPcDev *d;
	Chan *c;
	int slot;
	allowed(); slot = 0;
	if(spec != nil && spec[0] != 0){
		if(spec[0] < '0' || spec[0] >= '0'+VcDevMax || spec[1] != 0) error(Ebadarg);
		slot = spec[0]-'0';
	}
	ilock(&registry.lock); d = registry.slot[slot]; iunlock(&registry.lock);
	if(d == nil) error(Enonexist);
	/* d is boot-pinned even if withdrawal races native Chan allocation. */
	c = devattach(VcDevRune, spec);
	c->dev = slot; c->aux = d; c->qid = qid(d, Qdir);
	return c;
}

static Walkqid*
vconsolewalk(Chan *c, Chan *nc, char **name, int nname)
{
	int i;
	allowed(); endpoint(c);
	/* Native devclone panics on COPEN. Return a normal error instead; dup
	 * already-open FDs uses the SAME refcounted Chan, not a new endpoint. */
	if((c->flag&COPEN) != 0 || (nc != nil && (nc->flag&COPEN) != 0) ||
	   nname < 0 || nname > VcDevWalkMax || (nname > 0 && name == nil)) error(Ebadarg);
	if(nc != nil && (nc->aux != c->aux || nc->dev != c->dev || nc->qid.path != c->qid.path ||
	   nc->qid.vers != c->qid.vers || nc->qid.type != c->qid.type)) error(Ebadarg);
	for(i = 0; i < nname; i++) if(name[i] == nil) error(Ebadarg);
	return devwalk(c, nc, name, nname, nil, 0, vconsolegen);
}

static int
vconsolestat(Chan *c, uchar *buf, int count)
{
	VcPcDev *d;
	Dir dir;
	int n;
	allowed(); d = endpoint(c);
	if(count < 0 || (count > 0 && buf == nil)) error(Ebadarg);
	if(count < BIT16SZ) error(Eshortstat);
	entry(c, d, c->qid.path&3, &dir);
	n = convD2M(&dir, buf, count);
	if(n < BIT16SZ) error(Eshortstat);
	return n;
}

static Chan*
vconsoleopen(Chan *c, int mode)
{
	VcPcDev *d;
	VcIoHandle handle;
	int kind, busy, closed;
	allowed(); d = endpoint(c); kind = c->qid.path&3;
	if((c->flag&COPEN) != 0 || mode != (kind == Qdata ? ORDWR : OREAD)) error(Eperm);
	if(kind == Qdata){
		ilock(&d->lock); closed = d->closed; busy = d->opening || d->opened;
		if(!closed && !busy) d->opening = 1;
		iunlock(&d->lock);
		if(closed) error("vconsole needs a fresh endpoint after reset");
		if(busy) error("vconsole already opening or open");
		/* Immutable d exists BEFORE error frame. Native blocking open cleans
		 * its own broker handle/request on notes; this layer clears reservation.
		 * No throw/allocation/user copy between successful return and COPEN. */
		if(waserror()){
			ilock(&d->lock); d->opening = 0; iunlock(&d->lock);
			nexterror();
		}
		if(vcpcioopen(d->io, &handle) < 0) error(Ebadarg);
		poperror();
		ilock(&d->lock);
		d->handle = handle; d->channel = c; d->opened = 1; d->opening = 0;
		iunlock(&d->lock);
		c->iounit = VcBufferMax;
	}
	c->mode = mode; c->offset = 0; c->flag |= COPEN;
	return c;
}

static void
vconsoleclose(Chan *c)
{
	VcPcDev *d;
	VcIoHandle handle;
	int close;
	/* Native close can run in a different clunk process. No permission check,
	 * sleep, native error or hardware access. Unopened clones close NOTHING. */
	if(c == nil || c->aux == nil || (c->flag&COPEN) == 0 || (c->qid.path&3) != Qdata) return;
	d = c->aux;
	if(c->qid.path>>2 != d->qid || c->dev != d->slot) return;
	ilock(&d->lock);
	close = d->opened && !d->closed && d->channel == c; handle = d->handle;
	if(close) d->closed = 1;
	iunlock(&d->lock);
	if(close) vcioclose(&d->io->io, &handle);
}

static VcIoHandle
opened(VcPcDev *d, Chan *c)
{
	VcIoHandle handle;
	int live;
	ilock(&d->lock);
	live = d->opened && !d->closed && d->channel == c; handle = d->handle;
	iunlock(&d->lock);
	if(!live || (c->flag&COPEN) == 0 || c->mode != ORDWR) error("vconsole data not open");
	return handle;
}

static long
statusread(VcPcDev *d, void *out, long count, vlong offset)
{
	VcIoStatus status;
	char buf[StatusMax];
	int n;
	if(vciostatus(&d->io->io, &status) < 0) error("vconsole status unavailable");
	n = snprint(buf, sizeof buf, "identity %llud\nepoch %llud\nowner-generation %llud\n"
		"owner %d\nstopped %d\nclaimed %d\nhandle-held %d\n"
		"requests free %d reserved %d queued %d busy %d done %d taking %d\n",
		(uvlong)status.identity, (uvlong)status.epoch, (uvlong)status.generation,
		status.owner, status.stopped, status.claimed, status.held,
		status.phase[VcIoFree], status.phase[VcIoReserved], status.phase[VcIoQueued],
		status.phase[VcIoBusy], status.phase[VcIoDone], status.phase[VcIoTaking]);
	if(n < 0 || n >= (int)sizeof buf-1) error("vconsole status overflow");
	if(offset >= n) return 0; /* before vlong narrowing, never wrap large offsets */
	if(count > n-offset) count = n-offset;
	if(count > 0) memmove(out, buf+(int)offset, count);
	return count;
}

static long
vconsoleread(Chan *c, void *out, long count, vlong offset)
{
	VcPcDev *d;
	VcIoHandle handle;
	int kind;
	allowed(); d = endpoint(c); kind = c->qid.path&3;
	if(count < 0 || offset < 0 || (count > 0 && out == nil)) error(Ebadarg);
	if((c->flag&COPEN) == 0 || (c->mode != OREAD && c->mode != ORDWR)) error(Eperm);
	switch(kind){
	case Qdir: return devdirread(c, out, count, nil, 0, vconsolegen);
	case Qstatus: return statusread(d, out, count, offset);
	case Qdata:
		handle = opened(d, c);
		return vcpcioread(d->io, &handle, out, count);
	}
	error(Ebadarg);
}

static long
vconsolewrite(Chan *c, void *in, long count, vlong offset)
{
	VcPcDev *d;
	VcIoHandle handle;
	allowed(); d = endpoint(c);
	if((c->qid.path&3) != Qdata) error(Eperm);
	if(count < 0 || offset < 0 || (count > 0 && in == nil)) error(Ebadarg);
	handle = opened(d, c);
	return vcpciowrite(d->io, &handle, in, count);
}

static Block*
vconsolebread(Chan *c, long count, ulong offset)
{
	allowed();
	if(count < 0) error(Ebadarg);
	if(count > VcBufferMax) count = VcBufferMax; /* bound BEFORE allocb */
	return devbread(c, count, offset);
}

#ifdef QGA_HOST_TEST
#define vconsoleinit devinit
#else
extern void vcpcbootinit(void);
static void vconsoleinit(void) { vcpcbootinit(); }
#endif

Dev vconsoledevtab = {
	VcDevRune, "vconsole",
	devreset, vconsoleinit, devshutdown,
	vconsoleattach, vconsolewalk, vconsolestat, vconsoleopen, devcreate,
	vconsoleclose, vconsoleread, vconsolebread, vconsolewrite, devbwrite,
	devremove, devwstat,
};
