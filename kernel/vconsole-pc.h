/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_PC_H
#define QGA9_VCONSOLE_PC_H
#include "vconsole-mmio.h"
#include "vconsole-dma.h"

/* PC64 kernel context only (Pcidev must be declared by the owning platform).
 * Owner must hold exclusive PCI-driver ownership. No Dev registration, IRQ
 * setup, bus-master enable or runtime QGA channel is supplied here. */
typedef struct VcPc VcPc;
struct VcPc { VcMmio mmio; Pcidev *device; };
int vcpcinit(VcPc*);
int vcpcmap(VcPc*, Pcidev*);
/* Native aligned kernel allocation + checked direct physical translation.
 * This supplies callbacks, not permission to use a device or enable bus master. */
int vcpcdmaops(VcDmaOps*);

#endif
