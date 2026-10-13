/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_MMIO_H
#define QGA9_VCONSOLE_MMIO_H
#include "vconsole.h"

enum {
	VcMmCommon = 0, VcMmNotify, VcMmIsr, VcMmDevice,
	VcMmEmpty = 0, VcMmMapped, VcMmReset, VcMmFeatures, VcMmRunning, VcMmFault,
	VcMmMapMax = 4096, VcMmResetPolls = 1000, VcMmConfigTries = 8,
	VcRegDevSelect = 0, VcRegDevFeature = 4,
	VcRegDrvSelect = 8, VcRegDrvFeature = 12,
	VcRegConfigVector = 16, VcRegQueues = 18,
	VcRegStatus = 20, VcRegGeneration = 21,
	VcRegQueueSelect = 22, VcRegQueueSize = 24,
	VcRegQueueVector = 26,
	VcRegQueueEnable = 28, VcRegQueueNotify = 30,
	VcRegQueueDesc = 32, VcRegQueueAvail = 40, VcRegQueueUsed = 48,
	VcAck = 1, VcDriver = 2, VcDriverOk = 4, VcFeaturesOk = 8,
	VcNeedsReset = 64, VcFailed = 128,
};

typedef struct VcMmOps VcMmOps;
typedef struct VcMmio VcMmio;
typedef struct VcQueueInfo VcQueueInfo;
struct VcMmOps {
	void *(*map)(void*, Vcu64, int);
	void (*unmap)(void*, void*, int);
	int (*read)(void*, void*, int, int, Vcu32*);
	int (*write)(void*, void*, int, int, Vcu32);
	void (*fence)(void*);
	void (*pause)(void*); /* one millisecond, process context only */
};
struct VcQueueInfo { int id, size; Vcu32 notify; Vcu64 epoch; };
struct VcMmio {
	VcPci pci;
	VcMmOps ops;
	void *aux, *map[VcRegionMax];
	int length[VcRegionMax], state, ports, queues;
	Vcu64 epoch;
	Vcu8 enabled[VcQueuesMax];
	VcQueueInfo configured[VcQueuesMax];
};

/* Init fresh objects only. One process-context owner, no concurrent IRQ/Chan
 * access or recursive callbacks. Copied ops and borrowed aux outlive the object;
 * callbacks return normally, never longjmp/error. Map returns an owned reference
 * or nil, unmap consumes it once. They must implement actual register widths.
 * Caller exclusively owns the PCI device; mapping must not claim a live driver.
 * All state/input/output objects must be nonoverlapping. */
int vcmminit(VcMmio*, const VcMmOps*, void*);
int vcmmmap(VcMmio*, const Vcu8*, int, const VcBar*);
/* Confirm status zero before reinitializing. Failure retains every mapping and
 * faults the object; FAILED is not proof that DMA stopped. Reset is not IRQ/Chan
 * reference draining. Epochs do not wrap. Reset is the only supported means of
 * disabling configured queues. Bus-master activation is not supplied. */
int vcmmreset(VcMmio*);
int vcmmnegotiate(VcMmio*);
int vcmmqueue(VcMmio*, int, VcQueueInfo*);
/* Configure the exact rediscovered queue with owned, aligned, nonoverlapping
 * coherent DMA ring addresses. Once called, retain ALL backing storage even on
 * error until a later confirmed full-device reset; FAILED is insufficient.
 * Does not set DRIVER_OK, notify, register IRQs or enable PCI bus mastering. */
int vcmmenable(VcMmio*, const VcQueueInfo*, Vcu64, Vcu64, Vcu64);
/* Worker-only runtime operations. Start requires every data/control queue
 * configured in this epoch; caller has pinned IRQ mailbox, ready RX buffers
 * and authorized PCI bus mastering. No IRQ/worker/Dev registration is hidden.
 * Health checks status and stable max_nr_ports; a changed layout requires reset.
 * Kick is a checked 16-bit queue-ID write, never before confirmed DRIVER_OK. */
int vcmmstart(VcMmio*);
int vcmmhealth(VcMmio*);
int vcmmkick(VcMmio*, const VcQueueInfo*);
/* Only after confirmed reset AND caller-drained IRQ/worker/Chan references.
 * The drained argument is an assertion, not a reference-management mechanism. */
int vcmmdrop(VcMmio*, int);

#endif
