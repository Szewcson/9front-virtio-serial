/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_DEV_H
#define QGA9_VCONSOLE_DEV_H
#include "vconsole-worker-pc.h"
enum { VcDevMax = 4, VcDevRune = 'Y', VcDevWalkMax = 16 };
typedef struct VcPcDev VcPcDev;
typedef struct VcDevRegistry VcDevRegistry;
struct VcPcDev {
	/* Immutable after publication; old endpoint/IO/worker stay boot-pinned even
	 * after withdrawal. Chan.aux captures this pointer, never a mutable slot. */
	VcPcIo *io;
	VcPcWorker *worker;
	int slot;
	Vcu64 qid; /* one nonwrapping namespace serial, assigned once under registry */
	/* Under lock: one opening reservation, one permanently claimed data Chan.
	 * Handle is written once before COPEN and copied under lock for I/O/clunk. */
	Lock lock;
	VcIoHandle handle;
	void *channel; /* exact data Chan identity; compare only, never dereference */
	int opening, opened, closed;
};
struct VcDevRegistry { Lock lock; Vcu64 issued; VcPcDev *slot[VcDevMax]; };
/* Fresh unpublished endpoint, nonoverlapping contexts; no reinit/reuse of any
 * published/withdrawn object. One process-context supervisor per PCI owner.
 * bind IO to the fresh worker BEFORE init. No scan/spawn/PCI/IRQ changes here.
 * The Dev registry is one static boot-pinned instance, never reset/reinitialized.
 * #Y[0..3]/data is ORDWR-only, #Y[0..3]/status is read-only; #Y defaults to 0.
 * All namespace/I/O entry points require eve; close always permits cleanup.
 * No per-open endpoint/handle allocation; native Chan allocation is external. */
int vcpcdevinit(VcPcDev*, VcPcIo*, VcPcWorker*, int);
/* Wait for actual runner entry after successful start; Launched alone is NOT
 * spawn proof. Caller must bound its wait/retries and clean up failed startup. */
int vcpcdevpublish(VcPcDev*); /* exact Running worker, matching live epoch */
/* Supervisor, BEFORE controller reprepare: worker Done + all actual client
 * leases/operations drained + broker stopped + no opening or live data Chan.
 * Removes only the exact endpoint, never a replacement. DOES NOT free it.
 * Existing directory/status Chans still refer to the old pinned endpoint. */
int vcpcdevwithdraw(VcPcDev*);
#endif
