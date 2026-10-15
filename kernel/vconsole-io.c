/* SPDX-License-Identifier: MIT */
#ifndef QGA_HOST_TEST
#ifndef VCONSOLE_USER_TEST
#ifndef QGA_VCONSOLE_KERNEL
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#define QGA_VCONSOLE_KERNEL
#endif
#endif
#endif
#include "vconsole-io.h"

static int
valid(const VcIo *io)
{
	return io != nil && io->controller != nil && io->identity != 0 && io->epoch != 0 &&
		io->ops.lock != nil && io->ops.unlock != nil && io->ops.wake != nil;
}

int
vcioinit(VcIo *io, VcController *c, Vcu64 epoch, const VcIoOps *ops, void *aux)
{
	if(io == nil || c == nil || c->identity == 0 || epoch == 0 || ops == nil ||
	   ops->lock == nil || ops->unlock == nil || ops->wake == nil) return VcInvalid;
	memset(io, 0, sizeof *io);
	io->controller = c; io->identity = c->identity; io->epoch = epoch;
	io->ops = *ops; io->aux = aux;
	return 0;
}

static int
hold(VcIo *io, VcCtlRef *ref)
{
	int result;
	result = vcchold(io->controller, VcCtlClient, ref);
	if(result < 0) return result;
	if(ref->identity == io->identity && ref->epoch == io->epoch) return 0;
	vccput(io->controller, ref);
	return VcStreamClosed;
}

/* Locked. Serial exhaustion permanently retires just that slot. */
static int
slot(VcIo *io)
{
	int i;
	for(i = 0; i < VcIoSlots; i++)
		if(io->request[i].phase == VcIoFree && io->request[i].serial != ~(Vcu64)0) return i;
	return -1;
}

static void
reserve(VcIo *io, int i, const VcCtlRef *ref, int op, int length, VcIoToken *out)
{
	VcIoRequest *r;
	r = &io->request[i];
	r->serial++; r->generation = io->generation; r->ref = *ref;
	r->op = op; r->length = length; r->result = VcStreamWait; r->abandoned = 0;
	r->phase = VcIoReserved;
	out->identity = io->identity; out->epoch = io->epoch; out->slot = i; out->serial = r->serial;
}

static VcIoRequest*
request(VcIo *io, const VcIoToken *t)
{
	VcIoRequest *r;
	if(t == nil || t->identity != io->identity || t->epoch != io->epoch ||
	   t->slot < 0 || t->slot >= VcIoSlots) return nil;
	r = &io->request[t->slot];
	return r->phase != VcIoFree && t->serial == r->serial ? r : nil;
}

static int
handle(VcIo *io, const VcIoHandle *h)
{
	return h != nil && h->identity == io->identity && h->epoch == io->epoch &&
		h->generation == io->generation && io->ownerheld && !io->gone;
}

/* The caller/worker has surrendered ALL payload/predicate access. Keep serial
 * monotonic, zero the bounded private payload before admitting another caller. */
static void
release(VcIoRequest *r, VcCtlRef *ref)
{
	*ref = r->ref;
	memset(r->bytes, 0, sizeof r->bytes);
	r->phase = VcIoFree;
}

int
vcioopen(VcIo *io, VcIoHandle *h, VcIoToken *t)
{
	VcCtlRef owner, ref;
	int i, result;
	if(!valid(io) || h == nil || t == nil) return VcInvalid;
	if((result = hold(io, &owner)) < 0) return result;
	if((result = hold(io, &ref)) < 0){ vccput(io->controller, &owner); return result; }
	io->ops.lock(io->aux);
	i = slot(io);
	if(io->stopped || io->owner != VcIoUnused || io->generation == ~(Vcu64)0 || i < 0){
		result = io->stopped || io->owner == VcIoRetired ? VcStreamClosed : VcStreamWait;
		io->ops.unlock(io->aux);
		vccput(io->controller, &ref); vccput(io->controller, &owner);
		return result;
	}
	io->generation++; io->owner = VcIoOpening; io->gone = io->claimed = 0;
	io->ownerheld = 1; io->ownerref = owner;
	h->identity = io->identity; h->epoch = io->epoch; h->generation = io->generation;
	reserve(io, i, &ref, VcIoOpen, 0, t); io->request[i].phase = VcIoQueued;
	io->ops.unlock(io->aux);
	return 0;
}

int
vcioreserve(VcIo *io, const VcIoHandle *h, int op, int length, VcIoToken *t, Vcu8 **out)
{
	VcCtlRef ref;
	int i, result;
	if(!valid(io) || t == nil || out == nil || (op != VcIoRead && op != VcIoWrite) ||
	   length < 0 || length > VcBufferMax) return VcInvalid;
	if((result = hold(io, &ref)) < 0) return result;
	io->ops.lock(io->aux);
	i = slot(io);
	if(io->stopped || !handle(io, h) || io->owner != VcIoLive || i < 0){
		result = i < 0 && !io->stopped && handle(io, h) && io->owner == VcIoLive ?
			VcStreamWait : VcStreamClosed;
		io->ops.unlock(io->aux); vccput(io->controller, &ref); return result;
	}
	reserve(io, i, &ref, op, length, t); *out = io->request[i].bytes;
	io->ops.unlock(io->aux);
	return 0;
}

int
vciocommit(VcIo *io, const VcIoToken *t)
{
	VcIoRequest *r;
	int done;
	if(!valid(io)) return VcInvalid;
	io->ops.lock(io->aux); r = request(io, t);
	if(r == nil || r->phase != VcIoReserved){ io->ops.unlock(io->aux); return VcStreamClosed; }
	done = io->stopped || io->owner != VcIoLive || io->gone || r->generation != io->generation;
	if(done){ r->phase = VcIoDone; r->result = VcStreamClosed; }
	else r->phase = VcIoQueued;
	io->ops.unlock(io->aux);
	if(done) io->ops.wake(io->aux, t->slot);
	return 0;
}

int
vcioready(VcIo *io, const VcIoToken *t)
{
	VcIoRequest *r;
	int ready;
	if(!valid(io)) return 1;
	io->ops.lock(io->aux); r = request(io, t);
	ready = r == nil || r->phase == VcIoDone;
	io->ops.unlock(io->aux);
	return ready;
}

int
vcioresult(VcIo *io, const VcIoToken *t, int *result, Vcu8 **out)
{
	VcIoRequest *r;
	if(!valid(io) || result == nil || out == nil) return VcInvalid;
	io->ops.lock(io->aux); r = request(io, t);
	if(r == nil){ io->ops.unlock(io->aux); return VcStreamClosed; }
	if(r->phase != VcIoDone){ io->ops.unlock(io->aux); return VcStreamWait; }
	r->phase = VcIoTaking; *result = r->result; *out = r->bytes;
	io->ops.unlock(io->aux);
	return 0;
}

int
vcioreap(VcIo *io, const VcIoToken *t)
{
	VcIoRequest *r;
	VcCtlRef ref;
	if(!valid(io)) return VcInvalid;
	io->ops.lock(io->aux); r = request(io, t);
	if(r == nil || r->phase != VcIoTaking){ io->ops.unlock(io->aux); return VcStreamClosed; }
	release(r, &ref); io->ops.unlock(io->aux);
	return vccput(io->controller, &ref);
}

int
vcioabandon(VcIo *io, const VcIoToken *t)
{
	VcIoRequest *r;
	VcCtlRef ref;
	int busy;
	if(!valid(io)) return VcInvalid;
	io->ops.lock(io->aux); r = request(io, t);
	if(r == nil){ io->ops.unlock(io->aux); return VcStreamClosed; }
	busy = r->phase == VcIoBusy;
	if(busy) r->abandoned = 1;
	else release(r, &ref);
	io->ops.unlock(io->aux);
	return busy ? 0 : vccput(io->controller, &ref);
}

int
vciostatus(VcIo *io, VcIoStatus *out)
{
	int i;
	if(!valid(io) || out == nil) return VcInvalid;
	io->ops.lock(io->aux);
	memset(out, 0, sizeof *out);
	out->identity = io->identity; out->epoch = io->epoch; out->generation = io->generation;
	out->owner = io->owner; out->stopped = io->stopped;
	out->claimed = io->claimed; out->held = io->ownerheld;
	for(i = 0; i < VcIoSlots; i++) out->phase[io->request[i].phase]++;
	io->ops.unlock(io->aux);
	return 0;
}

int
vcioclose(VcIo *io, const VcIoHandle *h)
{
	VcCtlRef ref;
	int put;
	if(!valid(io)) return VcInvalid;
	io->ops.lock(io->aux);
	if(!handle(io, h)){ io->ops.unlock(io->aux); return VcStreamClosed; }
	io->gone = 1;
	put = io->owner == VcIoRetired;
	if(put){ ref = io->ownerref; io->ownerheld = 0; }
	else io->owner = VcIoClosing;
	io->ops.unlock(io->aux);
	return put ? vccput(io->controller, &ref) : 0;
}

/* Worker only. Closing is serialized against Busy operations, never forced
 * by a client. Reopening without a claim is safe; after a claim it requires a
 * fresh confirmed reset/stream/broker, even if all request slots are free. */
static int
closeowner(VcIo *io, const VcCtlRef *work)
{
	VcStreamToken port;
	VcCtlRef ref;
	int claimed, result, put;
	io->ops.lock(io->aux);
	if(io->owner != VcIoClosing){ io->ops.unlock(io->aux); return 0; }
	claimed = io->claimed; port = io->port;
	io->ops.unlock(io->aux);
	result = claimed ? vccclose(io->controller, work, &port) : 0;
	io->ops.lock(io->aux);
	io->owner = claimed || result < 0 || io->stopped ? VcIoRetired : VcIoUnused;
	put = io->ownerheld && io->gone;
	if(put){ ref = io->ownerref; io->ownerheld = 0; }
	io->ops.unlock(io->aux);
	if(put) vccput(io->controller, &ref);
	return result == VcFault ? VcFault : VcPumpProgress;
}

int
vciostep(VcIo *io, const VcCtlRef *work, int budget)
{
	VcIoRequest *r;
	VcCtlRef ref;
	VcStreamToken port;
	int i, j, op, length, claimed, result, progress, put, done, eligible;
	if(!valid(io) || work == nil || work->slot != VcCtlWorker ||
	   work->identity != io->identity || work->epoch != io->epoch ||
	   budget < 1 || budget > VcIoSlots) return VcInvalid;
	if(!vccworkerref(io->controller, work)) return VcStreamClosed;
	progress = closeowner(io, work);
	if(progress < 0) return progress;
	/* Visit every slot at most once; blocked requests do not spin Again. */
	for(j = 0; j < budget; j++){
		io->ops.lock(io->aux);
		i = io->cursor; io->cursor = (i+1)%VcIoSlots;
		r = &io->request[i];
		if(r->phase != VcIoQueued){ io->ops.unlock(io->aux); continue; }
		r->phase = VcIoBusy; op = r->op; length = r->length;
		eligible = !io->stopped && !io->gone && r->generation == io->generation &&
			io->owner == (op == VcIoOpen ? VcIoOpening : VcIoLive);
		claimed = io->claimed; port = io->port;
		io->ops.unlock(io->aux);
		if(!eligible) result = VcStreamClosed;
		else if(op == VcIoOpen){
			if(!claimed){
				result = vccopen(io->controller, work, &port);
				if(result == 0) claimed = 1;
				/* No matching named guest port yet is not an I/O fault. */
				else if(result == VcInvalid) result = VcStreamWait;
			}else result = 0;
			if(result == 0) result = vccread(io->controller, work, &port, nil, 0);
		}else if(op == VcIoRead) result = vccread(io->controller, work, &port, r->bytes, length);
		else result = vccwrite(io->controller, work, &port, r->bytes, length);
		io->ops.lock(io->aux);
		/* Clients cannot release a Busy slot. Record a successful claim even
		 * if close/cancellation raced it, so its queues never get recycled. */
		if(op == VcIoOpen && claimed){ io->claimed = 1; io->port = port; }
		if(io->gone || io->stopped || r->generation != io->generation || r->abandoned)
			if(result != VcFault) result = VcStreamClosed;
		if(op == VcIoOpen && result == 0 && io->owner == VcIoOpening) io->owner = VcIoLive;
		put = r->abandoned; done = result != VcStreamWait;
		if(put) release(r, &ref);
		else if(done){ r->phase = VcIoDone; r->result = result; }
		else r->phase = VcIoQueued;
		io->ops.unlock(io->aux);
		if(put) vccput(io->controller, &ref);
		else if(done) io->ops.wake(io->aux, i);
		if(done || put) progress |= VcPumpProgress;
		if(result == VcFault) return VcFault;
	}
	return progress;
}

int
vcioshutdown(VcIo *io)
{
	VcCtlRef ref;
	int i, put, wake;
	if(!valid(io)) return VcInvalid;
	io->ops.lock(io->aux); io->stopped = 1; io->owner = VcIoRetired;
	put = io->ownerheld && io->gone;
	if(put){ ref = io->ownerref; io->ownerheld = 0; }
	io->ops.unlock(io->aux);
	if(put) vccput(io->controller, &ref);
	for(i = 0; i < VcIoSlots; i++){
		io->ops.lock(io->aux);
		wake = io->request[i].phase == VcIoQueued;
		if(wake){ io->request[i].phase = VcIoDone; io->request[i].result = VcStreamClosed; }
		/* Reserved/Taking callers still own their private bytes. */
		io->ops.unlock(io->aux);
		if(wake) io->ops.wake(io->aux, i);
	}
	return 0;
}
