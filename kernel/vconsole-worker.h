/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_WORKER_H
#define QGA9_VCONSOLE_WORKER_H
#include "vconsole-controller.h"
enum {
	VcWorkerEmpty = 0, VcWorkerStarting, VcWorkerLaunched, VcWorkerRunning,
	VcWorkerFinishing, VcWorkerDone, VcWorkerStopsMax = 65535,
	VcWorkerBurst = 8,
};
typedef struct VcWorkerOps VcWorkerOps;
typedef struct VcWorker VcWorker;
struct VcWorkerOps {
	void (*lock)(void*);
	void (*unlock)(void*);
	/* 0 publishes EXACTLY one runner; any error guarantees NO runner exists.
	 * May block acquiring platform resources. Never ambiguous success/error. */
	int (*spawn)(void*, VcWorker*);
	/* Bounded sleep on IRQ pending/closed predicate, DOES NOT consume events.
	 * Return normally: native adapters catch notes/errors locally. */
	int (*wait)(void*, int);
	int (*yield)(void*);
	void (*wakejoin)(void*);
	/* Optional paired worker-owned request service/finish hooks. Normal returns,
	 * no extra runner; shutdown AFTER admission closes, BEFORE lease release. */
	int (*service)(void*, const VcCtlRef*, int);
	void (*shutdown)(void*);
};
struct VcWorker {
	/* Immutable after fresh init, controller/IRQ/ops/aux boot-pinned. */
	VcWorkerOps ops;
	void *aux;
	VcController *controller;
	VcIrq *irq;
	int budget, deadline;
	/* All mutable fields under ops.lock. No lock across any callout. */
	VcCtlRef ref;
	int phase, stop, held, bound, stoppers, ready, releasing, result;
};
/* Fresh/unpublished, nonoverlapping objects. Worker/ops/aux stay boot-pinned
 * even after Done: wakejoin or native kproc return/late join queries may still
 * access them. Done means LAST controller/IRQ predicate access, not OS process
 * reap or permission to free worker memory. No allocation, PCI or Dev hidden. */
int vcwinit(VcWorker*, VcController*, VcIrq*, const VcWorkerOps*, void*, int, int);
int vcwstart(VcWorker*); /* one shot; reserves worker lease BEFORE publication */
int vcwrun(VcWorker*); /* spawn adapter's sole runner entry point */
int vcwstop(VcWorker*); /* concurrent/cooperative, never force-drops a lease */
int vcwjoined(VcWorker*, int*); /* immutable result output only after true Done */
#endif
