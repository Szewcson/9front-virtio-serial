/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_IRQ_H
#define QGA9_VCONSOLE_IRQ_H
#include "vconsole.h"
enum {
	VcIrqQueue = 1, VcIrqConfig = 2, VcIrqWork = 4,
	VcIrqStop = 8, VcIrqFault = 16, VcIrqWakersMax = 65535,
};
typedef struct VcIrqOps VcIrqOps;
typedef struct VcIrq VcIrq;
struct VcIrqOps {
	void (*lock)(void*);
	void (*unlock)(void*);
	int (*read)(void*, Vcu32*); /* exactly one pinned ISR byte, read-to-clear */
	void (*fence)(void*);
	void (*wake)(void*); /* OUTSIDE mailbox lock, IRQ-safe, serialized by adapter */
};
struct VcIrq {
	VcIrqOps ops; /* immutable after fresh init */
	void *aux;   /* immutable; outlives callbacks AND late IRQ dispatch */
	Vcu64 epoch; /* immutable controller generation, never changed/rearmed */
	int pending, closed, failed, wakers; /* ALL access under ops.lock */
};
/* Fresh/unpublished init only. Callbacks return normally, no longjmp/error.
 * lock is an IRQ-safe synchronization primitive, not a process-only mutex.
 * read/fence run under that lock and must not reenter; wake runs outside it
 * with a tracked reference. Lock/unlock callbacks must not recursively call us.
 * Worker waits must be bounded: wake-reference exhaustion latches a fault
 * without adding an untracked wake callback.
 * No caller may directly access mutable fields or share/alias output with state.
 * This does NOT mutate queue/MMIO/core state, register IRQs or reset a device. */
int vcirqinit(VcIrq*, const VcIrqOps*, void*, Vcu64);
int vcirqinterrupt(VcIrq*);
int vcirqraise(VcIrq*, int);
int vcirqready(VcIrq*); /* predicate only; does not consume pending bits */
int vcirqtake(VcIrq*, int*); /* serialized worker consumes coalesced event bits */
int vcirqclose(VcIrq*); /* irrevocable gate: no later ISR mapping reads */
int vcirqdrained(VcIrq*); /* closed and no outstanding wake callbacks */
/* Closed/drained permits ISR mapping release AFTER hardware reset and worker
 * join, not freeing the mailbox itself. A pre-dispatched IRQ can still call it.
 * Retain mailbox/locks/aux for boot lifetime unless the platform separately
 * proves all dispatch/predicate/caller references gone. Never rearm in place. */
#endif
