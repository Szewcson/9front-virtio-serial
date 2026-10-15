/* SPDX-License-Identifier: MIT */
#if !defined(QGA_HOST_TEST) && !defined(VCONSOLE_USER_TEST) && !defined(QGA_VCONSOLE_KERNEL)
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#define QGA_VCONSOLE_KERNEL
#endif
#include "vconsole-worker.h"

static int
valid(const VcWorker *w)
{
	return w != nil && w->controller != nil && w->irq != nil &&
		w->ops.lock != nil && w->ops.unlock != nil;
}

int
vcwinit(VcWorker *w, VcController *c, VcIrq *irq, const VcWorkerOps *ops,
	void *aux, int budget, int deadline)
{
	if(w == nil || c == nil || irq == nil || ops == nil || ops->lock == nil ||
	   ops->unlock == nil || ops->spawn == nil || ops->wait == nil ||
	   ops->yield == nil || ops->wakejoin == nil || budget < 1 || budget > VcQueueMax ||
	   deadline < 1 || deadline > 1000 ||
	   ((ops->service == nil) != (ops->shutdown == nil))) return VcInvalid;
	memset(w, 0, sizeof *w);
	w->controller = c; w->irq = irq; w->ops = *ops; w->aux = aux;
	w->budget = budget; w->deadline = deadline;
	return 0;
}

/* Finishing prevents new stop callouts. Last stopper can finish a runner which
 * has surrendered ALL controller/predicate access, without a spin/sleep join.
 * Claim the one release under lock, return lease outside, THEN publish Done. */
static void
complete(VcWorker *w)
{
	VcCtlRef ref;
	int held, result;
	w->ops.lock(w->aux);
	if(w->phase != VcWorkerFinishing || !w->ready || w->stoppers || w->releasing){
		w->ops.unlock(w->aux); return;
	}
	w->releasing = 1; held = w->held; ref = w->ref;
	w->ops.unlock(w->aux);
	result = held ? vccput(w->controller, &ref) : 0;
	w->ops.lock(w->aux);
	if(result < 0) w->result = VcFault;
	w->held = 0; w->phase = VcWorkerDone;
	w->ops.unlock(w->aux);
	w->ops.wakejoin(w->aux); /* worker/aux remain pinned after publication */
}

static int
finish(VcWorker *w, int result)
{
	int bound;
	w->ops.lock(w->aux);
	w->phase = VcWorkerFinishing; w->stop = 1; w->result = result;
	bound = w->bound;
	w->ops.unlock(w->aux);
	if(bound){
		vccstop(w->controller);
		if(w->ops.shutdown != nil) w->ops.shutdown(w->aux);
	}
	/* No runner controller/IRQ/sleep-predicate access after ready publication. */
	w->ops.lock(w->aux); w->ready = 1; w->ops.unlock(w->aux);
	complete(w);
	return result;
}

int
vcwstart(VcWorker *w)
{
	VcCtlRef ref;
	int result, stopped;
	if(!valid(w)) return VcInvalid;
	w->ops.lock(w->aux);
	if(w->phase != VcWorkerEmpty){ w->ops.unlock(w->aux); return VcInvalid; }
	w->phase = VcWorkerStarting;
	stopped = w->stop;
	w->ops.unlock(w->aux);
	if(stopped) return finish(w, VcStreamClosed);
	if((result = vcchold(w->controller, VcCtlWorker, &ref)) < 0)
		return finish(w, result); /* never stop someone else's worker */
	w->ops.lock(w->aux); w->ref = ref; w->held = 1; w->ops.unlock(w->aux);
	if(!vccworkerirq(w->controller, &ref, w->irq)) return finish(w, VcInvalid);
	w->ops.lock(w->aux);
	w->bound = 1; stopped = w->stop;
	if(!stopped) w->phase = VcWorkerLaunched;
	w->ops.unlock(w->aux);
	if(stopped) return finish(w, VcStreamClosed);
	result = w->ops.spawn(w->aux, w);
	/* Negative spawn promises no runner, so this path owns final cleanup.
	 * On success a runner may ALREADY be done; do not rewrite its state. */
	if(result != 0) return finish(w, VcFault);
	return 0;
}

int
vcwrun(VcWorker *w)
{
	VcCtlRef ref;
	int result, serviced, stopped, burst;
	if(!valid(w)) return VcInvalid;
	w->ops.lock(w->aux);
	if(w->phase != VcWorkerLaunched || !w->held || !w->bound){
		w->ops.unlock(w->aux); return VcInvalid;
	}
	w->phase = VcWorkerRunning; ref = w->ref;
	w->ops.unlock(w->aux);
	burst = 0;
	for(;;){
		w->ops.lock(w->aux); stopped = w->stop; w->ops.unlock(w->aux);
		if(stopped) return finish(w, VcStreamClosed);
		result = vccstep(w->controller, &ref, w->budget);
		if(result < 0 && result != VcStreamWait) return finish(w, result);
		if(w->ops.service != nil){
			serviced = w->ops.service(w->aux, &ref, w->budget);
			if(serviced < 0 && serviced != VcStreamWait) return finish(w, serviced);
			if(serviced >= 0) result = (result < 0 ? 0 : result) | serviced;
		}
		if(result >= 0 && (result&VcPumpAgain) != 0){
			if(++burst < VcWorkerBurst) continue;
			burst = 0;
			if(w->ops.yield(w->aux) != 0) return finish(w, VcFault);
		}else{
			burst = 0;
			/* Keep every sleep-predicate access inside the held worker lease.
			 * Stop may arrive before wait: its IRQ predicate stays ready. */
			if(w->ops.wait(w->aux, w->deadline) != 0) return finish(w, VcFault);
		}
	}
}

int
vcwstop(VcWorker *w)
{
	int call, result;
	if(!valid(w)) return VcInvalid;
	w->ops.lock(w->aux);
	w->stop = 1;
	call = w->held && w->bound && w->phase < VcWorkerFinishing;
	result = 0;
	if(call && w->stoppers == VcWorkerStopsMax){ call = 0; result = VcStreamWait; }
	if(call) w->stoppers++;
	w->ops.unlock(w->aux);
	if(call){
		result = vccstop(w->controller);
		w->ops.lock(w->aux); w->stoppers--; w->ops.unlock(w->aux);
		complete(w);
	}
	return result;
}

int
vcwjoined(VcWorker *w, int *out)
{
	int done;
	if(!valid(w) || out == nil) return 0;
	w->ops.lock(w->aux);
	done = w->phase == VcWorkerDone;
	if(done) *out = w->result;
	w->ops.unlock(w->aux);
	return done;
}
