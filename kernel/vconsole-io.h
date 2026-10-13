/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_IO_H
#define QGA9_VCONSOLE_IO_H
#include "vconsole-controller.h"
enum {
	VcIoSlots = 8,
	VcIoFree = 0, VcIoReserved, VcIoQueued, VcIoBusy, VcIoDone, VcIoTaking,
	VcIoOpen = 0, VcIoRead, VcIoWrite,
	VcIoUnused = 0, VcIoOpening, VcIoLive, VcIoClosing, VcIoRetired,
};
typedef struct VcIoOps VcIoOps;
typedef struct VcIoHandle VcIoHandle;
typedef struct VcIoToken VcIoToken;
typedef struct VcIoRequest VcIoRequest;
typedef struct VcIo VcIo;
typedef struct VcIoStatus VcIoStatus;
struct VcIoStatus {
	Vcu64 identity, epoch, generation;
	int owner, stopped, claimed, held, phase[VcIoTaking+1];
};
struct VcIoOps {
	void (*lock)(void*);
	void (*unlock)(void*);
	void (*wake)(void*, int); /* slot completion; outside lock, may be delayed */
};
struct VcIoHandle { Vcu64 identity, epoch, generation; };
struct VcIoToken { Vcu64 identity, epoch, serial; int slot; };
struct VcIoRequest {
	Vcu64 serial, generation;
	VcCtlRef ref;
	int phase, op, length, result, abandoned;
	Vcu8 bytes[VcBufferMax];
};
struct VcIo {
	/* Immutable after fresh init, boot-pinned together with ops/aux/controller.
	 * One broker per stream epoch; no recycling after a published claim. */
	VcController *controller;
	VcIoOps ops;
	void *aux;
	Vcu64 identity, epoch;
	/* Under ops.lock, except Busy payload belongs exclusively to sole worker;
	 * Reserved/Taking payload belongs exclusively to the request's caller. */
	VcIoRequest request[VcIoSlots];
	VcCtlRef ownerref;
	VcStreamToken port;
	Vcu64 generation;
	int owner, ownerheld, gone, claimed, stopped, cursor;
};
/* Fresh unpublished, nonoverlapping objects. Caller supplies the freshly
 * prepared epoch under its supervisor serialization. Every acquired client
 * lease is checked against it: an old broker cannot enter a new controller.
 * Short IRQ-safe locks, no allocation/MMIO/wake under lock, callbacks return
 * normally, no reentry. Deadline polling by worker notices submissions: no
 * hidden IRQ event consumption or fabricated interrupt to wake the worker.
 * All token/handle/output arguments are private immutable CPU memory. */
int vcioinit(VcIo*, VcController*, Vcu64, const VcIoOps*, void*);
int vcioopen(VcIo*, VcIoHandle*, VcIoToken*); /* asynchronous exclusive open */
/* Reserve private staging storage BEFORE any possibly faulting user copy.
 * Caller alone uses bytes until commit/abandon. Never retain user pointers.
 * One caller per token, at most one sleeping waiter per slot. */
int vcioreserve(VcIo*, const VcIoHandle*, int, int, VcIoToken*, Vcu8**);
int vciocommit(VcIo*, const VcIoToken*); /* LAST input-buffer access */
int vcioready(VcIo*, const VcIoToken*); /* sleep predicate, stale means ready */
int vcioresult(VcIo*, const VcIoToken*, int*, Vcu8**); /* Done -> Taking */
int vcioreap(VcIo*, const VcIoToken*); /* LAST output/predicate access */
int vcioabandon(VcIo*, const VcIoToken*); /* caller's LAST access, note cleanup */
int vcioclose(VcIo*, const VcIoHandle*); /* handle's LAST access, never MMIO */
int vciostatus(VcIo*, VcIoStatus*); /* locked CPU snapshot, no port/user payload */
/* Sole worker only, exact slot-zero lease, after controller step. Bounded fair
 * service. Only its private buffers enter runtime methods; clients never do.
 * A cancelled Busy request may already have consumed RX or posted TX; no
 * rollback or blind replay is promised. Accepted TX is not delivered TX. */
int vciostep(VcIo*, const VcCtlRef*, int);
/* Sole worker finish hook, AFTER controller admission closes, BEFORE returning
 * its worker lease. No concurrent step. Completes queued clients, retains
 * Reserved/Taking buffers until their last access. Live handles keep their
 * lifetime lease until clunk, so teardown can legitimately remain waiting. */
int vcioshutdown(VcIo*);
#endif
