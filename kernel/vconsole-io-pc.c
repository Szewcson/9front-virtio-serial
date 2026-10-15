/* SPDX-License-Identifier: MIT */
#ifdef QGA_HOST_TEST
#include "../tests/vconsole-dev-shim.h"
#else
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#define QGA_VCONSOLE_KERNEL
#endif
#include "vconsole-io-pc.h"
typedef struct IoWait IoWait;
struct IoWait { VcPcIo *pc; VcIoToken token; };

static void
iolock(void *aux)
{
	VcPcIo *p;
	p = aux; ilock(&p->lock);
}

static void
iounlock(void *aux)
{
	VcPcIo *p;
	p = aux; iunlock(&p->lock);
}

static void
iowake(void *aux, int slot)
{
	VcPcIo *p;
	p = aux;
	if(slot < 0 || slot >= VcIoSlots) return;
	/* Native wakeup expects only one simultaneous waker per Rendez. Never
	 * hold the broker lock here: sleep holds Rendez then checks its predicate. */
	ilock(&p->wakelock[slot]); wakeup(&p->rendez[slot]); iunlock(&p->wakelock[slot]);
}

static int
ioready(void *aux)
{
	IoWait *w;
	w = aux;
	return vcioready(&w->pc->io, &w->token);
}

static void
ioerror(int result)
{
	if(result == VcStreamWait) error("vconsole request pool or owner busy");
	if(result == VcStreamReset) error("vconsole needs a fresh reset epoch");
	if(result == VcInvalid) error(Ebadarg);
	error("vconsole disconnected");
}

static void
iowait(IoWait *w)
{
	/* Stock tsleep calls predicates only in this sleeping process; its timer
	 * retains the boot-pinned Rendez, NOT arg. It cancels the timer on notes.
	 * The immutable stack token remains alive until return/error cleanup. */
	while(!ioready(w)) tsleep(&w->pc->rendez[w->token.slot], ioready, w, 100);
}

int
vcpcioinit(VcPcIo *p, VcController *c, Vcu64 epoch)
{
	static const VcIoOps ops = {iolock, iounlock, iowake};
	if(p == nil) return VcInvalid;
	memset(p, 0, sizeof *p);
	return vcioinit(&p->io, c, epoch, &ops, p);
}

int
vcpcioopen(VcPcIo *p, VcIoHandle *out)
{
	IoWait w;
	VcIoHandle h;
	Vcu8 *bytes;
	int result;
	if(up == nil) return VcInvalid; /* cannot throw/install error frames without Proc */
	if(p == nil || out == nil) error(Ebadarg);
	w.pc = p;
	result = vcioopen(&p->io, &h, &w.token);
	if(result < 0) ioerror(result);
	/* h/token are immutable BEFORE this native error frame. No modified
	 * automatic ownership flags across setlabel/nonlocal error returns. */
	if(waserror()){
		vcioabandon(&p->io, &w.token); vcioclose(&p->io, &h);
		nexterror();
	}
	iowait(&w);
	if(vcioresult(&p->io, &w.token, &result, &bytes) < 0) error("vconsole stale open request");
	if(result < 0) ioerror(result);
	poperror();
	vcioreap(&p->io, &w.token);
	*out = h;
	return 0;
}

static long
iotransfer(VcPcIo *p, const VcIoHandle *h, void *data, long length, int op)
{
	IoWait w;
	Vcu8 *bytes;
	int result, count;
	if(up == nil) return VcInvalid;
	if(p == nil || h == nil || length < 0 || (length > 0 && data == nil)) error(Ebadarg);
	count = length < VcBufferMax ? length : VcBufferMax;
	w.pc = p;
	result = vcioreserve(&p->io, h, op, count, &w.token, &bytes);
	if(result < 0) ioerror(result);
	if(waserror()){
		vcioabandon(&p->io, &w.token); /* last user-copy/predicate access */
		nexterror();
	}
	if(op == VcIoWrite && count > 0) memmove(bytes, data, count);
	if(vciocommit(&p->io, &w.token) < 0) error("vconsole stale submission");
	iowait(&w);
	if(vcioresult(&p->io, &w.token, &result, &bytes) < 0) error("vconsole stale completion");
	if(result < 0) ioerror(result);
	if(result > count) error("vconsole invalid completion length");
	if(op == VcIoRead && result > 0) memmove(data, bytes, result);
	poperror();
	vcioreap(&p->io, &w.token); /* no access to bytes after returning lease */
	return result;
}

long
vcpcioread(VcPcIo *p, const VcIoHandle *h, void *data, long length)
{
	return iotransfer(p, h, data, length, VcIoRead);
}

long
vcpciowrite(VcPcIo *p, const VcIoHandle *h, const void *data, long length)
{
	return iotransfer(p, h, (void*)data, length, VcIoWrite);
}
