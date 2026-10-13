/* SPDX-License-Identifier: MIT */
#include "vconsole-controller.h"

static int
valid(const VcController *c)
{
	return c != nil && c->identity != 0 && c->mmio != nil &&
		c->ops.lock != nil && c->ops.unlock != nil;
}

/* Gate lock held. Slots are bounded; serials never wrap or get reinitialized. */
static int
joined(const VcController *c)
{
	int i;
	if(c->owner) return 0;
	for(i = 0; i < VcCtlRefs; i++) if(c->active[i]) return 0;
	return 1;
}

static int
matches(const VcController *c, const VcCtlRef *r)
{
	return r != nil && r->identity == c->identity && r->epoch == c->epoch &&
		r->slot >= 0 && r->slot < VcCtlRefs && c->active[r->slot] &&
		r->serial != 0 && r->serial == c->serial[r->slot];
}

static int
admin(VcController *c)
{
	int result;
	if(!valid(c)) return VcInvalid;
	c->ops.lock(c->aux);
	result = !joined(c) ? VcStreamWait : 0;
	if(result == 0) c->owner = 1;
	c->ops.unlock(c->aux);
	return result;
}

static int
worker(VcController *c, const VcCtlRef *r)
{
	int result;
	if(!valid(c)) return VcInvalid;
	c->ops.lock(c->aux);
	if(!matches(c, r) || r->slot != VcCtlWorker || c->closed || !c->admit)
		result = VcStreamClosed;
	else if(c->owner) result = VcStreamWait;
	else { c->owner = 1; result = 0; }
	c->ops.unlock(c->aux);
	return result;
}

static void
leave(VcController *c)
{
	c->ops.lock(c->aux);
	c->owner = 0;
	c->ops.unlock(c->aux);
}

int
vccinit(VcController *c, VcMmio *m, const VcCtlOps *ops, void *aux,
	const VcDmaOps *dmaops, void *dmaaux, Vcu64 identity)
{
	if(c == nil || m == nil || identity == 0 || m->state != VcMmMapped ||
	   ops == nil || ops->lock == nil || ops->unlock == nil ||
	   ops->alloc == nil || ops->free == nil || dmaops == nil ||
	   dmaops->alloc == nil || dmaops->free == nil || dmaops->fence == nil)
		return VcInvalid;
	memset(c, 0, sizeof *c);
	c->ops = *ops; c->aux = aux;
	c->dmaops = *dmaops; c->dmaaux = dmaaux;
	c->mmio = m; c->identity = identity;
	return 0;
}

static int
plan(VcController *c, int capacity, Vcu64 limit)
{
	Vcu64 extra;
	int i;
	c->count = 2*c->mmio->ports+2;
	for(i = 0; i < c->count; i++)
		if(vcmmqueue(c->mmio, i, &c->info[i]) < 0) return VcFault;
	extra = sizeof *c + sizeof(VcCtlSession)-sizeof(VcPump)-sizeof(VcStream);
	if(limit <= extra || limit > VcPumpBudgetMax ||
	   vcsbudget(&c->budget, c->info, c->count, c->mmio->ports,
		capacity, limit-extra) < 0) return VcFault;
	c->budget.metadata += extra;
	c->budget.total += extra;
	return 0;
}

/* Operation guard and joined leases required. Do not call after a failed reset
 * if even ONE queue may have been exposed. free callbacks consume exact blocks. */
static int
release(VcController *c)
{
	int i;
	while(c->allocated > 0){
		i = c->allocated-1;
		if(vcdmadrop(&c->queue[i], 1) < 0) return VcFault;
		c->allocated--;
	}
	if(c->queue != nil) c->ops.free(c->aux, c->queue, c->count*sizeof *c->queue);
	if(c->session != nil) c->ops.free(c->aux, c->session, sizeof *c->session);
	c->queue = nil; c->session = nil; c->count = 0;
	memset(&c->budget, 0, sizeof c->budget);
	memset(c->info, 0, sizeof c->info);
	c->state = VcCtlIdle;
	return 0;
}

static int
unwind(VcController *c)
{
	int i, exposed;
	exposed = 0;
	for(i = 0; i < c->allocated; i++) exposed |= c->queue[i].exposed;
	c->state = VcCtlRetained;
	if(exposed && vcmmreset(c->mmio) < 0) return VcFault;
	return release(c);
}

int
vccprepare(VcController *c, int capacity, Vcu64 limit)
{
	int i, result;
	if(capacity < 1 || capacity > VcBufferMax || limit == 0 || limit > VcPumpBudgetMax)
		return VcInvalid;
	if((result = admin(c)) < 0) return result;
	if(c->state != VcCtlIdle){ leave(c); return VcInvalid; }
	c->ops.lock(c->aux);
	c->admit = 0; c->closed = 0; /* explicit fresh attempt, never rearm old IRQ */
	c->ops.unlock(c->aux);
	result = VcFault;
	if(vcmmreset(c->mmio) < 0 || vcmmnegotiate(c->mmio) < 0 || plan(c, capacity, limit) < 0)
		goto fail;
	c->session = c->ops.alloc(c->aux, sizeof *c->session);
	if(c->session == nil || ((Vcuptr)c->session&7) != 0 ||
	   (Vcuptr)c->session > ~(Vcuptr)0-sizeof *c->session) goto fail;
	memset(c->session, 0, sizeof *c->session);
	c->queue = c->ops.alloc(c->aux, c->count*sizeof *c->queue);
	if(c->queue == nil || ((Vcuptr)c->queue&7) != 0 ||
	   (Vcuptr)c->queue > ~(Vcuptr)0-(Vcu32)(c->count*sizeof *c->queue)) goto fail;
	memset(c->queue, 0, c->count*sizeof *c->queue);
	for(i = 0; i < c->count; i++){
		if(vcdmainit(&c->queue[i], &c->dmaops, c->dmaaux) < 0 ||
		   vcdmaalloc(&c->queue[i], &c->info[i], (i&1) == 0, c->budget.capacity[i]) < 0)
			goto fail;
		c->allocated++;
		c->session->queue[i] = &c->queue[i];
	}
	for(i = 0; i < c->count; i++)
		if(vcdmabind(&c->queue[i], c->mmio) < 0) goto fail;
	if(vcpumpinit(&c->session->pump, c->mmio, c->session->queue, c->count, limit) < 0 ||
	   vcsinit(&c->session->stream, &c->session->pump, c->identity, limit) < 0)
		goto fail;
	c->state = VcCtlPrepared;
	c->ops.lock(c->aux);
	c->epoch = c->mmio->epoch;
	c->ops.unlock(c->aux);
	result = 0;
	goto out;
fail:
	unwind(c); /* failure to prove reset intentionally leaves retained resources */
out:
	leave(c);
	return result;
}

int
vccstart(VcController *c, VcIrq *irq)
{
	int result, bits;
	if(irq == nil) return VcInvalid;
	if((result = admin(c)) < 0) return result;
	if(c->state != VcCtlPrepared || irq->epoch != c->mmio->epoch ||
	   vcirqtake(irq, &bits) < 0 || (bits&(VcIrqStop|VcIrqFault)) != 0){
		leave(c); return VcInvalid;
	}
	c->ops.lock(c->aux);
	if(c->closed){ c->ops.unlock(c->aux); leave(c); return VcStreamClosed; }
	c->irq = irq;
	c->ops.unlock(c->aux);
	result = vcpumpstart(&c->session->pump);
	if(result == 0){
		if(vcirqtake(irq, &bits) < 0 || (bits&VcIrqFault) != 0) result = VcFault;
		else if((bits&VcIrqStop) != 0) result = VcStreamClosed;
	}
	c->ops.lock(c->aux);
	if(result == 0 && !c->closed){ c->admit = 1; c->state = VcCtlRunning; }
	else { c->state = VcCtlRetained; result = result < 0 ? result : VcStreamClosed; }
	c->ops.unlock(c->aux);
	if(result < 0) vccstop(c);
	leave(c);
	return result;
}

int
vcchold(VcController *c, int role, VcCtlRef *out)
{
	VcCtlRef r;
	int i, first, last, result;
	if(!valid(c) || out == nil || (role != VcCtlWorker && role != VcCtlClient))
		return VcInvalid;
	c->ops.lock(c->aux);
	result = VcStreamClosed;
	if(!c->closed && c->admit){
		result = VcStreamWait;
		first = role == VcCtlWorker ? 0 : 1;
		last = role == VcCtlWorker ? 1 : VcCtlRefs;
		for(i = first; i < last; i++)
			if(!c->active[i] && c->serial[i] != ~(Vcu64)0){
				memset(&r, 0, sizeof r);
				r.identity = c->identity; r.epoch = c->epoch; r.slot = i;
				r.serial = ++c->serial[i]; c->active[i] = 1;
				*out = r; result = 0; break;
			}
	}
	c->ops.unlock(c->aux);
	return result;
}

int
vccput(VcController *c, const VcCtlRef *r)
{
	int result;
	if(!valid(c)) return VcInvalid;
	c->ops.lock(c->aux);
	result = matches(c, r) ? 0 : VcStreamClosed;
	if(result == 0) c->active[r->slot] = 0;
	c->ops.unlock(c->aux);
	return result;
}

int
vccworkerref(VcController *c, const VcCtlRef *r)
{
	int result;
	if(!valid(c)) return 0;
	c->ops.lock(c->aux);
	result = matches(c, r) && r->slot == VcCtlWorker && !c->closed && c->admit;
	c->ops.unlock(c->aux);
	return result;
}

int
vccworkerirq(VcController *c, const VcCtlRef *r, const VcIrq *irq)
{
	int result;
	if(!valid(c) || irq == nil) return 0;
	c->ops.lock(c->aux);
	result = matches(c, r) && r->slot == VcCtlWorker && c->irq == irq &&
		irq->epoch == c->epoch && !c->closed && c->admit;
	c->ops.unlock(c->aux);
	return result;
}

int
vccstop(VcController *c)
{
	VcIrq *irq;
	if(!valid(c)) return VcInvalid;
	c->ops.lock(c->aux);
	c->closed = 1; c->admit = 0; irq = c->irq;
	c->ops.unlock(c->aux);
	return irq == nil ? 0 : vcirqclose(irq);
}

int
vccjoined(VcController *c)
{
	int result;
	if(!valid(c)) return 0;
	c->ops.lock(c->aux);
	result = c->closed && joined(c);
	c->ops.unlock(c->aux);
	return result;
}

int
vccreset(VcController *c)
{
	VcIrq *irq;
	int result;
	if(vccstop(c) < 0) return VcInvalid;
	if((result = admin(c)) < 0) return result;
	if(c->state == VcCtlDetached){ leave(c); return VcInvalid; }
	c->ops.lock(c->aux); irq = c->irq; c->ops.unlock(c->aux);
	if(irq != nil && !vcirqdrained(irq)){ leave(c); return VcStreamWait; }
	if(c->session != nil && c->session->stream.state != VcStreamEmpty)
		vcsstop(&c->session->stream);
	c->state = VcCtlRetained;
	result = vcmmreset(c->mmio);
	if(result == 0) result = release(c);
	if(result == 0){
		c->ops.lock(c->aux); c->irq = nil; c->ops.unlock(c->aux);
	}
	leave(c);
	return result;
}

int
vccdetach(VcController *c)
{
	int result;
	if(vccstop(c) < 0) return VcInvalid;
	if((result = admin(c)) < 0) return result;
	if(c->state != VcCtlIdle || c->mmio->state != VcMmReset){ leave(c); return VcInvalid; }
	result = vcmmdrop(c->mmio, 1);
	if(result == 0) c->state = VcCtlDetached;
	leave(c);
	return result;
}

static int
finish(VcController *c, int result)
{
	/* Close before surrendering the guard: a late fault must never close a
	 * newly prepared session after a racing supervisor has reset/reopened. */
	if(result == VcFault){ c->state = VcCtlRetained; vccstop(c); }
	leave(c);
	return result;
}

int
vccstep(VcController *c, const VcCtlRef *r, int budget)
{
	VcIrq *irq;
	int result, events;
	if((result = worker(c, r)) < 0) return result;
	c->ops.lock(c->aux); irq = c->irq; c->ops.unlock(c->aux);
	/* Consume before polling all relevant rings. New events after the snapshots
	 * stay pending for the sleep predicate; an ISR fault must not be ignored
	 * merely because the common/device status registers still look healthy. */
	if(irq == nil || vcirqtake(irq, &events) < 0 || (events&(VcIrqStop|VcIrqFault)) != 0)
		result = VcFault;
	else result = vcsstep(&c->session->stream, budget);
	return finish(c, result);
}

int
vccopen(VcController *c, const VcCtlRef *r, VcStreamToken *out)
{
	int result;
	if((result = worker(c, r)) < 0) return result;
	result = vcsopen(&c->session->stream, out);
	return finish(c, result);
}

int
vccread(VcController *c, const VcCtlRef *r, const VcStreamToken *token, Vcu8 *out, int bytes)
{
	int result;
	if((result = worker(c, r)) < 0) return result;
	result = vcsread(&c->session->stream, token, out, bytes);
	return finish(c, result);
}

int
vccwrite(VcController *c, const VcCtlRef *r, const VcStreamToken *token, const Vcu8 *in, int bytes)
{
	int result;
	if((result = worker(c, r)) < 0) return result;
	result = vcswrite(&c->session->stream, token, in, bytes);
	return finish(c, result);
}

int
vccclose(VcController *c, const VcCtlRef *r, const VcStreamToken *token)
{
	int result;
	if((result = worker(c, r)) < 0) return result;
	result = vcsclose(&c->session->stream, token);
	return finish(c, result);
}
