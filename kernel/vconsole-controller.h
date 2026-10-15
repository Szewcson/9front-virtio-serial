/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_CONTROLLER_H
#define QGA9_VCONSOLE_CONTROLLER_H
#include "vconsole-stream.h"
#include "vconsole-irq.h"
enum {
	VcCtlIdle = 0, VcCtlPrepared, VcCtlRunning, VcCtlRetained, VcCtlDetached,
	VcCtlWorker = 0, VcCtlClient = 1, VcCtlRefs = 65,
};
typedef struct VcCtlOps VcCtlOps;
typedef struct VcCtlRef VcCtlRef;
typedef struct VcCtlSession VcCtlSession;
typedef struct VcController VcController;
struct VcCtlOps {
	void (*lock)(void*);
	void (*unlock)(void*);
	void *(*alloc)(void*, int); /* owned, naturally aligned CPU metadata */
	void (*free)(void*, void*, int);
};
struct VcCtlRef { Vcu64 identity, epoch, serial; int slot; };
struct VcCtlSession { VcPump pump; VcStream stream; VcDmaQueue *queue[VcQueuesMax]; };
struct VcController {
	/* Immutable, boot-pinned after init. MMIO is exclusively lent to us. */
	VcCtlOps ops;
	VcDmaOps dmaops;
	void *aux, *dmaaux;
	VcMmio *mmio;
	Vcu64 identity;
	/* Only under ops.lock: lifetime admission and exclusive operation guard. */
	Vcu64 epoch, serial[VcCtlRefs];
	Vcu8 active[VcCtlRefs];
	int owner, closed, admit;
	VcIrq *irq; /* old mailboxes/aux stay boot-pinned, never rearmed */
	/* Only while owning operation guard: never inspected by IRQ/ref callers. */
	VcCtlSession *session;
	VcDmaQueue *queue;
	VcQueueInfo info[VcQueuesMax];
	VcPumpBudget budget;
	int state, count, allocated;
};
/* No kproc, Dev, IRQ registration or PCI ownership/bus-master changes hidden
 * here. Platform must claim the device before init; authorize bus mastering
 * and register the pinned, current-epoch IRQ mailbox before start. Callbacks
 * return normally (no error/longjmp), do not reenter, lock is IRQ-safe. Locks
 * are NEVER held across allocation/MMIO/wake. All objects/outputs disjoint.
 * Controller/ops/aux remain boot-pinned; put means the caller's LAST access,
 * including sleep predicates. A reference is lifetime tracking, not authority.
 * Client refs do not permit direct stream access; the Dev sends requests
 * to the one worker. All runtime methods require its exact slot-zero lease. */
int vccinit(VcController*, VcMmio*, const VcCtlOps*, void*,
	const VcDmaOps*, void*, Vcu64);
/* Serialized supervisor: reset/negotiate/discover, check complete <=16 MiB
 * budget BEFORE acquiring ANY metadata/DMA; allocate all before any bind.
 * Failure unwinds unexposed memory, or confirms a NEW reset before freeing
 * exposed memory. Failed reset retains all exposed memory for a later retry.
 * Budget includes MMIO/controller/session/exact queue array/DMA, not platform
 * allocator overhead, separately pinned IRQ mailboxes or future Dev objects. */
int vccprepare(VcController*, int, Vcu64);
/* Fresh IRQ mailbox matching the epoch, already registered/pinned by platform.
 * No admission until DRIVER_OK and DEVICE_READY succeed. Concurrent stop wins. */
int vccstart(VcController*, VcIrq*);
int vcchold(VcController*, int, VcCtlRef*);
int vccput(VcController*, const VcCtlRef*);
/* Locked validation of exact worker lease AND bound IRQ object; same epoch
 * alone is insufficient to select a sleep predicate for the live worker. */
int vccworkerirq(VcController*, const VcCtlRef*, const VcIrq*);
int vccworkerref(VcController*, const VcCtlRef*); /* locked exact runtime admission */
/* Concurrent stop closes admission and IRQ gate, never touches mutable rings.
 * Existing worker/client leases must be returned explicitly; they are not
 * forcibly cancelled. joined checks real lease slots AND operation guard. */
int vccstop(VcController*);
int vccjoined(VcController*);
/* Supervisor, nonblocking join: Wait until all leases/operations and IRQ wake
 * callbacks have drained. Then reset, logically stop, drop DMA/metadata exactly
 * once. Failure is sticky-retained, never evidence of quiescence. Success leaves
 * Idle; prepare creates a fresh session/epoch, old refs/tokens cannot reenter.
 * Old IRQ registration/mailbox still needs platform handling before reattach. */
int vccreset(VcController*);
int vccdetach(VcController*); /* Idle only; closes gate and releases MMIO maps */
/* Consumes latched IRQ events before ring polling; IRQ fault/stop closes the
 * controller even when MMIO health still looks valid. */
int vccstep(VcController*, const VcCtlRef*, int);
int vccopen(VcController*, const VcCtlRef*, VcStreamToken*);
int vccread(VcController*, const VcCtlRef*, const VcStreamToken*, Vcu8*, int);
int vccwrite(VcController*, const VcCtlRef*, const VcStreamToken*, const Vcu8*, int);
int vccclose(VcController*, const VcCtlRef*, const VcStreamToken*);
#endif
