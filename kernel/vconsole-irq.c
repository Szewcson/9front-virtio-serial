/* SPDX-License-Identifier: MIT */
#if !defined(QGA_HOST_TEST) && !defined(VCONSOLE_USER_TEST) && !defined(QGA_VCONSOLE_KERNEL)
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#define QGA_VCONSOLE_KERNEL
#endif
#include "vconsole-irq.h"

static int
valid(const VcIrq *irq)
{
	return irq != nil && irq->epoch != 0 && irq->ops.lock != nil &&
		irq->ops.unlock != nil && irq->ops.read != nil &&
		irq->ops.fence != nil && irq->ops.wake != nil;
}

int
vcirqinit(VcIrq *irq, const VcIrqOps *ops, void *aux, Vcu64 epoch)
{
	if(irq == nil || epoch == 0 || ops == nil || ops->lock == nil ||
	   ops->unlock == nil || ops->read == nil || ops->fence == nil || ops->wake == nil)
		return VcInvalid;
	memset(irq, 0, sizeof *irq);
	irq->ops = *ops;
	irq->aux = aux;
	irq->epoch = epoch;
	irq->vector = -1;
	return 0;
}

/* Mailbox lock held. Wake only on transition from no pending work, but every
 * event is latched. A ready predicate prevents the wake-before-sleep race. */
static int
signal(VcIrq *irq, int bits)
{
	int wake;

	wake = irq->pending == 0;
	irq->pending |= bits;
	if(!wake) return 0;
	if(irq->wakers >= VcIrqWakersMax){
		irq->failed = 1;
		irq->pending |= VcIrqFault;
		/* Do not wrap a lifetime counter. A bounded worker wait must also
		 * observe this latched fault if earlier wakeups already returned. */
		return 0;
	}
	irq->wakers++;
	return 1;
}

static void
wake(VcIrq *irq, int needed)
{
	if(!needed) return;
	irq->ops.wake(irq->aux); /* no mailbox lock / Rendez lock-order inversion */
	irq->ops.lock(irq->aux);
	irq->wakers--;
	irq->ops.unlock(irq->aux);
}

int
vcirqinterrupt(VcIrq *irq)
{
	Vcu32 status;
	int result, needed;

	if(!valid(irq)) return VcInvalid;
	irq->ops.lock(irq->aux);
	if(irq->closed){ irq->ops.unlock(irq->aux); return 0; }
	status = 0;
	irq->ops.fence(irq->aux);
	result = irq->ops.read(irq->aux, &status);
	irq->ops.fence(irq->aux);
	if(result != 0 || status > 255 || status == 255){
		irq->failed = 1;
		needed = signal(irq, VcIrqFault);
		result = VcFault;
	}else{
		status &= VcIrqQueue|VcIrqConfig; /* ignore reserved bits */
		if(status != 0 && irq->deliveries != ~(Vcu64)0) irq->deliveries++;
		needed = status != 0 ? signal(irq, status) : 0;
		result = status != 0;
	}
	irq->ops.unlock(irq->aux);
	wake(irq, needed);
	return result;
}

int
vcirqraise(VcIrq *irq, int bits)
{
	int needed;

	if(!valid(irq) || bits == 0 || (bits&~(VcIrqQueue|VcIrqConfig|VcIrqWork)) != 0)
		return VcInvalid;
	irq->ops.lock(irq->aux);
	if(irq->closed){ irq->ops.unlock(irq->aux); return VcInvalid; }
	needed = signal(irq, bits);
	irq->ops.unlock(irq->aux);
	wake(irq, needed);
	return 0;
}

int
vcirqready(VcIrq *irq)
{
	int ready;

	if(!valid(irq)) return 0;
	irq->ops.lock(irq->aux);
	ready = irq->pending != 0 || irq->closed || irq->failed;
	irq->ops.unlock(irq->aux);
	return ready;
}

int
vcirqtake(VcIrq *irq, int *out)
{
	if(!valid(irq) || out == nil) return VcInvalid;
	irq->ops.lock(irq->aux);
	*out = irq->pending | (irq->closed ? VcIrqStop : 0) | (irq->failed ? VcIrqFault : 0);
	irq->pending = 0;
	irq->ops.unlock(irq->aux);
	return 0;
}

int
vcirqclose(VcIrq *irq)
{
	int needed;

	if(!valid(irq)) return VcInvalid;
	irq->ops.lock(irq->aux);
	if(irq->closed){ irq->ops.unlock(irq->aux); return 0; }
	irq->closed = 1;
	needed = signal(irq, VcIrqStop);
	irq->ops.unlock(irq->aux);
	wake(irq, needed);
	return 0;
}

int
vcirqdrained(VcIrq *irq)
{
	int drained;

	if(!valid(irq)) return 0;
	irq->ops.lock(irq->aux);
	drained = irq->closed && irq->wakers == 0;
	irq->ops.unlock(irq->aux);
	return drained;
}

int
vcirqdeliveries(VcIrq *irq, Vcu64 *out)
{
	if(!valid(irq) || out == nil) return VcInvalid;
	irq->ops.lock(irq->aux); *out=irq->deliveries; irq->ops.unlock(irq->aux);
	return 0;
}

int
vcirqroute(VcIrq *irq, int vector)
{
	int result;
	if(!valid(irq) || vector<32 || vector>=256) return VcInvalid;
	irq->ops.lock(irq->aux);
	result=irq->closed || irq->vector!=-1 ? VcInvalid : 0;
	if(result==0) irq->vector=vector;
	irq->ops.unlock(irq->aux); return result;
}

int
vcirqvector(VcIrq *irq)
{
	int vector;
	if(!valid(irq)) return -1;
	irq->ops.lock(irq->aux); vector=irq->vector; irq->ops.unlock(irq->aux);
	return vector;
}

int
vcirqdispatch(VcIrq *irq, int vector)
{
	if(vector<32 || vector>=256 || vcirqvector(irq)!=vector) return 0;
	return vcirqinterrupt(irq);
}
