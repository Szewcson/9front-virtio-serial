/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_CONTROLLER_PC_H
#define QGA9_VCONSOLE_CONTROLLER_PC_H
#include "vconsole-controller.h"
/* Native PC64 context only. Fresh zeroed boot-pinned lock as controller aux.
 * This is synchronization/allocation glue, NOT PCI ownership or a kproc. */
typedef struct VcPcCtlLock VcPcCtlLock;
struct VcPcCtlLock { Lock lock; };
int vcpcctlops(VcCtlOps*);
#endif
