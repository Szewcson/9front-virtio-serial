/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_WORKER_PC_H
#define QGA9_VCONSOLE_WORKER_PC_H
#include "vconsole-worker.h"
#include "vconsole-wake.h"
#include "vconsole-io-pc.h"
/* Fresh zeroed boot-pinned native context as worker ops aux. No reinit/reuse
 * while a kproc/wakeup/late join query may exist. Spawn/wait/yield/join require
 * process context (never IRQ); null up is rejected before native error frames.
 * Join has one supervisor. */
typedef struct VcPcWorker VcPcWorker;
struct VcPcWorker { Lock lock; Rendez join; VcPcWake *wake; VcPcIo *io; VcWorker worker; };
int vcpcworkerinit(VcPcWorker*, VcController*, VcPcWake*, int, int);
/* Serialized supervisor only, fresh pre-launch worker. Broker context is
 * immutable/boot-pinned and belongs to this controller+mailbox epoch. */
int vcpcworkerbindio(VcPcWorker*, VcPcIo*);
/* Bounded join, no forced kill. Catches native sleep notes/errors locally.
 * Done covers controller/predicate last access, not native process reap. */
int vcpcworkerjoin(VcPcWorker*, int, int*);
#endif
