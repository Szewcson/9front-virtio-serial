/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_STREAM_H
#define QGA9_VCONSOLE_STREAM_H
#include "vconsole-pump.h"
enum {
	VcStreamEmpty = 0, VcStreamPrepared, VcStreamOpening, VcStreamOpen,
	VcStreamDisconnected, VcStreamFault, VcStreamStopped,
	VcStreamWait = -3, VcStreamClosed = -4, VcStreamReset = -5,
	VcStreamFifo = 4096,
};
typedef struct VcStreamToken VcStreamToken;
typedef struct VcStream VcStream;
struct VcStreamToken { Vcu64 identity, epoch; VcToken port; };
struct VcStream {
	VcPump *pump;
	Vcu64 identity, epoch;
	VcStreamToken token;
	int state, claimed, rxqueue, txqueue, rxhead, rxcount, txhead, txcount;
	int head, count, nextport, turn, dataturn;
	int nextslot[VcPortMax];
	Vcu8 sink[VcPortMax];
	VcDmaDone rx[VcQueueMax], tx[VcQueueMax], scratch[VcQueueMax];
	Vcu8 bytes[VcStreamFifo], frame[VcBufferMax];
};
/* One serialized worker owns stream/pump/all queues/MMIO. No concurrent Chan
 * calls, callbacks, IRQs or reentrant access. Borrowed objects outlive this
 * object and every token. Inputs/outputs/state never overlap. Fresh init only;
 * never reinitialize while any token/worker/Chan caller can still exist.
 * identity is a nonzero boot-unique controller identifier supplied by the
 * future owner, NOT authentication or an OS permission/capability check. */
int vcsbudget(VcPumpBudget*, const VcQueueInfo*, int, int, int, Vcu64);
/* Fresh pump before startup, not already deferred/attached to another adapter.
 * Selects deferred data RX publication; allocation
 * must have used vcsbudget BEFORE acquisition. No allocation is hidden here. */
int vcsinit(VcStream*, VcPump*, Vcu64, Vcu64);
/* First exact-QGA claim primes owner-tagged RX before OPEN can be sent.
 * One published guest owner per stream/controller epoch. Host backend socket
 * changes preserve this owner and its DMA cookies; open does not need host OPEN.
 * Reopen after guest close or port removal requires full reset/reference-drained
 * pump/stream: posted split-queue leases cannot be individually cancelled. */
int vcsopen(VcStream*, VcStreamToken*);
int vcsalive(VcStream*, const VcStreamToken*);
/* Control first, then bounded fair data actions, at most one used snapshot per
 * active/sink queue. Returns Progress/Again bits or a sticky fault. Closed sinks
 * are discarded, never forwarded. Again excludes full RX FIFO/TX saturation.
 * Future worker waits on IRQ with a bounded health deadline and joins on stop. */
int vcsstep(VcStream*, int);
/* Nonblocking worker API, NOT a Dev/Chan yet. 0..4096 bytes, short writes allowed.
 * Wait means pending guest OPEN/empty RX/full TX/host-closed TX; Closed rejects
 * stale/wrong tokens. Host close does not cancel RX/FD/job/file ownership.
 * Writes acknowledge queue acceptance, not delivery. Binary bytes are literal. */
int vcsread(VcStream*, const VcStreamToken*, Vcu8*, int);
int vcswrite(VcStream*, const VcStreamToken*, const Vcu8*, int);
int vcsclose(VcStream*, const VcStreamToken*);
/* Logical stop only. No free, reset, IRQ deregistration or reference draining. */
int vcsstop(VcStream*);
#endif
