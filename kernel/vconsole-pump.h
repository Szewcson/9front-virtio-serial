/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_PUMP_H
#define QGA9_VCONSOLE_PUMP_H
#include "vconsole-dma.h"

enum {
	VcPumpEmpty = 0, VcPumpReady, VcPumpRunning, VcPumpFault, VcPumpStopped,
	VcPumpReplies = 16, VcPumpBudgetMax = 16*1024*1024,
	VcPumpDataCapacity = 256,
	VcPumpProgress = 1, VcPumpAgain = 2,
};
typedef struct VcPumpBudget VcPumpBudget;
typedef struct VcPumpReply VcPumpReply;
typedef struct VcPump VcPump;
struct VcPumpBudget {
	int ports, count, capacity[VcQueuesMax];
	Vcu64 dma, metadata, total;
};
struct VcPumpReply {
	Vcu32 id;
	int event, value;
	Vcu64 incarnation, generation;
};
struct VcPump {
	VcMmio *mmio;
	VcDmaQueue *queue[VcQueuesMax];
	VcQueueInfo info[VcQueuesMax];
	VcPumpBudget budget;
	VcPorts ports;
	Vcu64 epoch, incarnation[VcPortMax], advertised[VcPortMax];
	VcDmaDone rx[VcQueueMax], tx[VcQueueMax];
	int rxhead, rxcount, txhead, txcount;
	VcPumpReply reply[VcPumpReplies];
	int head, count, state, deferred;
	Vcu8 frame[VcControlMax];
};

/* Fresh/unpublished objects only. One serialized process-context owner of
 * pump, every queue, MMIO and ports; IRQs use the separate mailbox only.
 * Inputs are immutable, objects/outputs nonoverlapping. No allocation, IRQ
 * registration, PCI bus-master write, kproc or Dev is hidden here. All borrowed
 * objects/ops/aux outlive the pump. Never reinitialize a published pump.
 * Budget includes all DMA arenas plus queue/pump/MMIO objects, not platform
 * allocator overhead, future Dev/Chan state or registry/reference machinery. */
int vcpumpbudget(VcPumpBudget*, const VcQueueInfo*, int, int, int, Vcu64);
/* All queues are already bound, empty and unused, ordered by queue ID. The
 * allocator must use the budget planner BEFORE acquiring/binding any queue.
 * This second budget check verifies actual capacities/footprint, not authority
 * to allocate first and exceed a limit temporarily. */
int vcpumpinit(VcPump*, VcMmio*, VcDmaQueue *const*, int, Vcu64);
/* Ready state only: a data adapter will populate each data RX queue before its
 * guest OPEN, rather than publishing unowned data buffers during startup.
 * All queues are still configured before DRIVER_OK; control RX stays primed. */
int vcpumpdeferdata(VcPump*);
/* Caller pins IRQ mailbox and exclusively authorizes PCI bus mastering first.
 * Posts every RX queue before DRIVER_OK, then notifies and sends DEVICE_READY.
 * Initial data leases have cookie zero (no stream owner), control uses epoch.
 * Future data adapter must drain/repost/filter leases before delivering bytes;
 * control-plane alive alone does not prove data belongs to a new stream owner.
 * Any error is fail-stop; even partially exposed queues stay held for reset. */
int vcpumpstart(VcPump*);
/* A bounded worker iteration: at most budget (1..256) reap/send/receive actions,
 * plus at most one whole bounded snapshot per control queue and a health check.
 * Progress/Again bits: Again means runnable CPU work, NOT TX backpressure;
 * otherwise wait on IRQ with a bounded health deadline. No data I/O yet. */
int vcpumpstep(VcPump*, int);
/* Worker-only stream ownership API. Claim accepts ownership and queues
 * OPEN; alive stays false until that OPEN is posted and successfully notified.
 * Returns VcInvalid on unavailable port or full reply FIFO, no partial claim.
 * Release also accepts a still-pending claim. Tokens never cancel a new owner. */
int vcpumpclaim(VcPump*, VcToken*);
int vcpumpalive(const VcPump*, const VcToken*);
int vcpumprelease(VcPump*, const VcToken*);
/* Irrevocable logical stop only. No free/reset or IRQ/worker/Chan drainage.
 * Future controller must close IRQ gate, confirm reset and join references. */
int vcpumpstop(VcPump*);
/* Serialized owner propagates a data-plane fault; no reset/free is implicit. */
int vcpumpfail(VcPump*);
#endif
