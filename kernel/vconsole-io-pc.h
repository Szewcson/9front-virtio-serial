/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_IO_PC_H
#define QGA9_VCONSOLE_IO_PC_H
#include "vconsole-io.h"
typedef struct VcPcIo VcPcIo;
struct VcPcIo {
	Lock lock, wakelock[VcIoSlots];
	Rendez rendez[VcIoSlots];
	VcIo io;
};
/* Fresh boot-pinned context, one waiter per private request slot. Blocking
 * calls require process context; null up returns Invalid before error frames,
 * otherwise throw native errors/notes after cleanup.
 * No request holds a user buffer, or copies while an ilock is held. */
int vcpcioinit(VcPcIo*, VcController*, Vcu64);
int vcpcioopen(VcPcIo*, VcIoHandle*);
long vcpcioread(VcPcIo*, const VcIoHandle*, void*, long);
long vcpciowrite(VcPcIo*, const VcIoHandle*, const void*, long);
#endif
