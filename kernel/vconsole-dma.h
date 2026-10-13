/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_DMA_H
#define QGA9_VCONSOLE_DMA_H
#include "vconsole-mmio.h"
#ifdef QGA_HOST_TEST
typedef uintptr_t Vcuptr;
#else
typedef uintptr Vcuptr;
#endif

enum {
	VcDmaEmpty = 0, VcDmaAllocated, VcDmaBound, VcDmaFault,
	VcDmaFree = 0, VcDmaPosted, VcDmaReady,
	VcDmaAlign = 16,
	VcDmaMax = VcQueueMax*(VcBufferMax+26)+32,
};
typedef struct VcDmaMemory VcDmaMemory;
typedef struct VcDmaOps VcDmaOps;
typedef struct VcDmaLayout VcDmaLayout;
typedef struct VcDmaDone VcDmaDone;
typedef struct VcDmaQueue VcDmaQueue;
struct VcDmaMemory { void *base; Vcu64 physical; int bytes; };
struct VcDmaOps {
	int (*alloc)(void*, int, int, VcDmaMemory*);
	void (*free)(void*, const VcDmaMemory*);
	void (*fence)(void*);
};
struct VcDmaLayout { int size, capacity, stride, avail, used, buffers, bytes; };
/* serial prevents a duplicate receipt reaping a later use of the same slot,
 * even when its port-generation cookie and length have not changed. */
struct VcDmaDone { VcUsed used; Vcu64 serial; };
struct VcDmaQueue {
	VcDmaOps ops;
	void *aux;
	VcDmaMemory memory;
	VcDmaLayout layout;
	VcQueueInfo info;
	VcQueue core;
	VcMmio *mmio;
	int state, exposed;
	Vcu16 avail;
	Vcu8 life[VcQueueMax], snapshot[VcQueueMax*8];
	Vcu64 serial[VcQueueMax];
	VcUsed completed[VcQueueMax];
	VcDmaDone ready[VcQueueMax];
};

/* PC64/coherent little-endian DMA only, not an IOMMU/cache-maintenance API.
 * Fresh init; one process-context owner for the queue AND its MMIO selectors.
 * No IRQ callback/reentrant access. Ops/aux/MMIO outlive all queues referencing
 * them. Objects/inputs/outputs do not overlap. Tokens are scoped to this object
 * lifetime. Callbacks return normally; alloc returns a private, pinned,
 * contiguous, 16-byte-aligned coherent RAM extent, never MMIO or shared state.
 * Any nonnil returned base is owned even on callback error and freed once.
 * CPU and physical extents must both cover exactly the requested byte count. */
int vcdmalayout(VcDmaLayout*, int, int);
int vcdmainit(VcDmaQueue*, const VcDmaOps*, void*);
int vcdmaalloc(VcDmaQueue*, const VcQueueInfo*, int, int);
/* Expose only owned storage. After an attempted bind, failure retains it until
 * a later confirmed full-device reset; never disable a live queue with zero. */
int vcdmabind(VcDmaQueue*, VcMmio*);
/* post publishes avail.idx after a barrier but DOES NOT notify or set DRIVER_OK.
 * RX takes nil input; TX copies literal bytes into its private DMA slot. */
int vcdmapost(VcDmaQueue*, int, const Vcu8*, int, Vcu64);
/* poll makes a bounded CPU-owned used snapshot before whole-batch validation.
 * Completion does not recycle the payload; take copies RX bytes (TX returns 0)
 * and consumes the exact one-shot receipt before the slot may be reposted. */
int vcdmapoll(VcDmaQueue*, VcDmaDone*, int);
int vcdmatake(VcDmaQueue*, const VcDmaDone*, Vcu8*, int);
/* Unexposed allocations may unwind directly. Exposed storage requires a newer
 * confirmed-reset MMIO epoch AND drained IRQ/worker/Chan references. Drop DMA
 * queues before dropping MMIO. drained is an assertion, not reference counting. */
int vcdmadrop(VcDmaQueue*, int);
#endif
