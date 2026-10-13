/* SPDX-License-Identifier: MIT */
#include <u.h>
#include "/sys/src/9/port/lib.h"
#include "/sys/src/9/pc64/mem.h"
#include "/sys/src/9/pc64/dat.h"
#include "/sys/src/9/pc64/fns.h"
#define QGA_VCONSOLE_KERNEL
#include "vconsole-worker-pc.h"

static void
workerlock(void *aux)
{
	VcPcWorker *p;
	p = aux; ilock(&p->lock);
}

static void
workerunlock(void *aux)
{
	VcPcWorker *p;
	p = aux; iunlock(&p->lock);
}

static void
workerproc(void *aux)
{
	vcwrun(aux);
}

static int
workerspawn(void *aux, VcWorker *w)
{
	USED(aux);
	if(up == nil) return VcInvalid; /* error frames require process context */
	/* Native kproc publishes ready last, with no throwing call afterwards.
	 * Resource-wait notes occur BEFORE any runner exists. It may wait for a
	 * process slot; no bounded startup/deadline is claimed for this native API. */
	if(waserror()) return VcFault;
	kproc("vconsole", workerproc, w);
	poperror();
	return 0;
}

static int
workerwait(void *aux, int deadline)
{
	VcPcWorker *p;
	int result;
	p = aux;
	if(up == nil) return VcInvalid;
	if(waserror()) return VcFault;
	result = vcpcwakepause(p->wake, deadline);
	poperror();
	return result;
}

static int
workeryield(void *aux)
{
	USED(aux);
	if(up == nil) return VcInvalid;
	if(waserror()) return VcFault;
	sched();
	poperror();
	return 0;
}

static void
workerjoinwake(void *aux)
{
	VcPcWorker *p;
	p = aux;
	/* Exactly one completion publisher. No worker/gate lock across Rendez. */
	wakeup(&p->join);
}

static int
workerfinished(void *aux)
{
	VcPcWorker *p;
	int result;
	p = aux;
	return vcwjoined(&p->worker, &result);
}

static int
workerservice(void *aux, const VcCtlRef *ref, int budget)
{
	VcPcWorker *p;
	p = aux;
	return p->io == nil ? 0 : vciostep(&p->io->io, ref, budget < VcIoSlots ? budget : VcIoSlots);
}

static void
workershutdown(void *aux)
{
	VcPcWorker *p;
	p = aux;
	if(p->io != nil) vcioshutdown(&p->io->io);
}

int
vcpcworkerinit(VcPcWorker *p, VcController *c, VcPcWake *wake, int budget, int deadline)
{
	static const VcWorkerOps ops = {workerlock, workerunlock, workerspawn,
		workerwait, workeryield, workerjoinwake, workerservice, workershutdown};
	if(p == nil || wake == nil) return VcInvalid;
	memset(p, 0, sizeof *p);
	p->wake = wake;
	return vcwinit(&p->worker, c, &wake->irq, &ops, p, budget, deadline);
}

int
vcpcworkerbindio(VcPcWorker *p, VcPcIo *io)
{
	if(p == nil || io == nil || p->wake == nil || p->worker.controller == nil ||
	   io->io.identity == 0 || io->io.epoch == 0 || p->worker.phase != VcWorkerEmpty || p->io != nil ||
	   io->io.controller != p->worker.controller || io->io.epoch != p->wake->irq.epoch ||
	   io->io.identity != p->worker.controller->identity) return VcInvalid;
	p->io = io;
	return 0;
}

int
vcpcworkerjoin(VcPcWorker *p, int deadline, int *out)
{
	if(p == nil || out == nil || up == nil || deadline < 1 || deadline > 1000) return VcInvalid;
	if(waserror()) return VcFault;
	tsleep(&p->join, workerfinished, p, deadline);
	poperror();
	return vcwjoined(&p->worker, out) ? 0 : VcStreamWait;
}
