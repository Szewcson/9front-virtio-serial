/* SPDX-License-Identifier: MIT */
/* Native PC64 wake adapter; IRQ registration belongs to the owner adapter. */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "ureg.h"
#define QGA_VCONSOLE_KERNEL
#include "vconsole-wake.h"

static void
mailboxlock(void *aux)
{
	VcPcWake *w;
	w = aux;
	ilock(&w->lock);
}

static void
mailboxunlock(void *aux)
{
	VcPcWake *w;
	w = aux;
	iunlock(&w->lock);
}

static int
isrread(void *aux, Vcu32 *out)
{
	VcPcWake *w;
	w = aux;
	*out = *w->isr;
	return 0;
}

static void
barrier(void *aux)
{
	USED(aux);
	coherence();
}

static void
workerwake(void *aux)
{
	VcPcWake *w;
	w = aux;
	/* Native wakeup expects serialized callers. Never take wakelock while
	 * holding mailbox lock: sleep holds Rendez lock while testing readiness. */
	ilock(&w->wakelock);
	wakeup(&w->rendez);
	iunlock(&w->wakelock);
}

static int
workerready(void *aux)
{
	VcPcWake *w;
	w = aux;
	return vcirqready(&w->irq);
}

int
vcpcwakeinit(VcPcWake *w, const VcMmio *m)
{
	static const VcIrqOps ops = {mailboxlock, mailboxunlock, isrread, barrier, workerwake};

	if(w == nil || m == nil || coherence == nil || m->epoch == 0 ||
	   m->state != VcMmFeatures || m->map[VcMmIsr] == nil ||
	   m->length[VcMmIsr] < 1 || m->length[VcMmIsr] > VcMmMapMax)
		return VcInvalid;
	memset(w, 0, sizeof *w);
	w->isr = m->map[VcMmIsr];
	return vcirqinit(&w->irq, &ops, w, m->epoch);
}

void
vcpcwakeintr(Ureg *ureg, void *aux)
{
	VcPcWake *w;
	w = aux;
	/* Native spurious dispatch also calls unrelated handlers. Such a call is
	 * NOT proof of routing and must not consume this device's read-clear ISR. */
	if(w != nil && ureg != nil && ureg->type >= 32 && ureg->type < 256)
		vcirqdispatch(&w->irq,ureg->type);
}

int
vcpcwakepause(VcPcWake *w, int deadline)
{
	if(w == nil || deadline < 1 || deadline > 1000 ||
	   w->irq.epoch == 0 || w->irq.aux != w || w->irq.ops.lock != mailboxlock ||
	   w->irq.ops.unlock != mailboxunlock)
		return VcInvalid;
	/* Rendez checks workerready under its own lock, closing the lost-wake gap.
	 * The deadline lets the owning worker check health even if an IRQ is lost. */
	tsleep(&w->rendez, workerready, w, deadline);
	return 0;
}

int
vcpcwakewait(VcPcWake *w, int deadline, int *events)
{
	int result;
	if(events == nil) return VcInvalid;
	if((result = vcpcwakepause(w, deadline)) < 0) return result;
	return vcirqtake(&w->irq, events);
}
