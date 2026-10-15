/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_OWNER_PC_H
#define QGA9_VCONSOLE_OWNER_PC_H
#include "vconsole-pc.h"
#include "vconsole-controller.h"
#include "vconsole-wake.h"
#include "vconsole-worker-pc.h"
#include "vconsole-controller-pc.h"
#include "vconsole-dev.h"
enum { VcPcOwnersMax = 4, VcPcIntDisable = 1<<10 };
typedef struct VcPcOwners VcPcOwners;
typedef struct VcPcOwner VcPcOwner;
struct VcPcOwners { Lock lock; Vcu64 issued; VcPcOwner *slot[VcPcOwnersMax]; };
struct VcPcOwner {
	VcPc pc;
	VcPcOwners *registry;
	Pcidev *device;
	Vcu64 identity;
	int slot, claimed, saved, prepared, irqtried, detached, wantirq;
	VcController *controller;
	VcPcWake *wake;
};
/* ONE boot-pinned registry for this driver, fresh init before publication.
 * No kernel-global PCI claim primitive exists here: the owning Dev/config must
 * exclude competing drivers. Registry prevents duplicate TBDF within THIS
 * driver, not privileged raw PCI accesses or another driver ignoring it.
 * One serialized process-context supervisor owns each claimed owner. Objects,
 * device, IRQ mailbox/aux and registry remain boot-pinned. IRQ never touches
 * mutable owner/controller state. No PCI scan or automatic startup hidden. */
int vcpcownersinit(VcPcOwners*);
/* Fresh owner only. Reject existing BME, active MSI/MSI-X and ANY MSI capability
 * (native intrenable may auto-select MSI). Assigned BARs/power D0 required.
 * Reserve identity/TBDF before changes; set only memory decode/INTx mask with
 * checked 16-bit command writes, then map validated capability ranges.
 * Failure can retain the claim after an ambiguous write; call stop to retry. */
int vcpcownerclaim(VcPcOwners*, VcPcOwner*, Pcidev*);
/* Bind a fresh Idle manager BEFORE prepare: even a failed prepare can retain
 * DMA, which stop must know about. Never operate an unbound manager on these
 * mappings. Pure manager init uses &owner.pc.mmio and owner.identity. */
int vcpcownerbind(VcPcOwner*, VcController*);
/* Required acquisition entry: subtract native owner/worker/wake/gate/I/O/Dev
 * and ENTIRE registry overhead BEFORE vccprepare allocates. Base controller
 * budget already counts MMIO/controller. One fresh context of each; extra
 * retained epochs and allocator overhead need separate budget.
 * A smaller caller limit may reserve those extras too. */
int vcpcownerprepare(VcPcOwner*, int, Vcu64);
/* Opt in before start. Requires the checked native IRQ API build; unsupported
 * builds refuse, never silently unmask an unchecked route. */
int vcpcownerirq(VcPcOwner*);
/* Bound controller already prepared using &owner.pc.mmio and owner.identity.
 * Fresh wake initialized from same epoch, boot-pinned. Attempt INTx registration,
 * then enable BME with readback and masked INTx, then DRIVER_OK via vccstart.
 * Default intrenable returns void: registration is attempted, not proven;
 * INTx remains masked and bounded worker deadlines poll queues. A checked-API
 * build with explicit owner opt-in binds the installed vector under mailbox
 * lock and unmasks only after DRIVER_OK. This does not prove actual delivery.
 * Legacy intrenable can panic on allocation failure. Failed/ambiguous PCI
 * operations retain ownership; no full platform fault-safety claim follows. */
int vcpcownerstart(VcPcOwner*, VcController*, VcPcWake*);
/* Supervisor-only first half of stop: close admission, mask INTx/clear BME,
 * deregister once. No reset/free; safe while waiting for worker/Chan leases. */
int vcpcownerquiesce(VcPcOwner*);
/* Cooperative: close controller/IRQ gate, mask INTx/clear BME, deregister exact
 * attempted handler, wait for leases/operations/wakers, confirm reset before
 * freeing/detaching maps. Restore saved command only after reset (saved BME was
 * clear), then release registry. Any failed check retains claim/resources.
 * intrdisable is NOT a dispatch join: old mailbox/aux remain boot-pinned.
 * Never invoke concurrently with start/claim/another stop; worker may run. */
int vcpcownerstop(VcPcOwner*);
#endif
