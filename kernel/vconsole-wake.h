/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_WAKE_H
#define QGA9_VCONSOLE_WAKE_H
#include "vconsole-irq.h"
#include "vconsole-mmio.h"
/* Actual PC64 kernel context: Lock/Rendez/Ureg must already be declared.
 * Pin this object/aux for boot lifetime once registered with IRQ dispatch.
 * IRQ reads only this immutable ISR pointer, never worker-owned MMIO state. */
typedef struct VcPcWake VcPcWake;
struct VcPcWake {
	VcIrq irq;
	Lock lock, wakelock;
	Rendez rendez;
	volatile Vcu8 *isr;
};
int vcpcwakeinit(VcPcWake*, const VcMmio*);
void vcpcwakeintr(Ureg*, void*);
/* One worker only. Bounded health-check deadline 1..1000 ms, no lock held.
 * tsleep may throw a native note/error: the worker kproc owns that error frame. */
int vcpcwakewait(VcPcWake*, int, int*);
/* Nonconsuming variant for vccstep's sole ownership of latched IRQ events.
 * May throw; worker adapter catches the note/error outside controller guards. */
int vcpcwakepause(VcPcWake*, int);
#endif
