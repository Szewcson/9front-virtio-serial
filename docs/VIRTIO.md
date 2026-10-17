# Native virtio-serial driver contracts

The driver exposes the exact nonconsole `org.qemu.guest_agent.0` port as a native
duplex binary stream. It targets coherent little-endian PC64 RAM and modern PCI
capabilities on virtio console IDs `0x1043` and `0x1003`.
It is QGA-port-specific, not a general arbitrary-port API.

Real PCI I/O, automatic cold boot and operator PVE file/exec smoke tests passed.
Default INTx remains masked and the worker polls at 100 ms deadlines. An opt-in
checked-IRQ kernel passed native builds and local actual IRQ, forced reset,
invalid used-ID and named-port recovery tests. A later Proxmox checked-kernel
warm boot also passed actual IRQ delivery and operator host smoke tests.
Broader platform/fault validation remains open.
See [STATUS](STATUS.md), [INSTALL](INSTALL.md) and [SECURITY](SECURITY.md).

## Native interface

| Interface | Mode | Meaning |
| --- | --- | --- |
| `#Y[0..3]` (`#Y` selects slot 0) | eve 0500 | Published endpoint directory |
| `#Y[0..3]/data` | eve 0600, exclusive ORDWR | Literal binary stream |
| `#Y[0..3]/status` | eve 0400, OREAD | Bounded CPU identity/epoch/owner/request snapshot |

The boot supervisor publishes slot 0; the registry supports four slots.
Attach/walk/stat/open/read/write/block-read check `iseve`, including delegated
FDs. Close permits cleanup by another native clunk process but releases only the
exact owning Chan. Create/remove/wstat and noncanonical open modes are denied.
Status contains no payload, credentials or MMIO read.

Publication requires actual worker Running admission. Attach captures an
immutable endpoint, not a later slot lookup. Nonwrapping Qid serials prevent
replacement from redirecting old directory/status/data Chans. Only the last
framework reference to the exact data Chan releases its broker handle.
Old endpoint views remain pinned after withdrawal.

The separate [QGA daemon](https://github.com/Szewcson/9front-qemu-guest-agent)
consumes an existing, unowned endpoint:

```rc
cat '#Y0/status'
/bin/aux/qga9 -t stream -d '#Y0/data'
```

Do not start a second daemon on an already owned endpoint. No QGA userspace
objects or codecs are linked into this driver.

## Fixed limits and negotiation

Bounds are 64 ports (IDs 0–63), 130 queues, 256 power-of-two descriptors/queue,
4096 bytes/buffer, 127 UTF-8 name bytes plus local NUL and 136-byte control frames.
The broker has eight fixed 4-KiB request slots. The stream RX FIFO is 4096 bytes;
default data queue payload capacity is 256 bytes and control reply FIFO is 16.

Budgeting precedes acquisition. The 16 MiB planner counts exact queue/session/
controller/DMA storage; native preparation additionally counts platform
contexts/registry, and bootstrap reserves the whole retained pool plus 64 KiB
conservative allocator slack. The slack is not a proven allocator maximum;
stock Chan/block allocations are not bounded by DMA budgeting.

Negotiate exactly VERSION_1 and MULTIPORT. Packed/indirect/event-index rings,
IOMMU/platform features, notification-data and per-queue reset are unsupported.
PCI capability/BAR type/index/extent/overflow and overlapping regions are checked.
Map only common 56, ISR 1, device 8 and at most 4096 notify bytes.
Configuration generations and queue metadata must remain consistent.

The native backend rejects mappings outside the deterministic direct VMAP
physical window (1 TiB in the inspected kernel). Stock `vunmap` retains mappings;
dropping driver references does not reclaim mappings/page tables.
DMA accepts aligned, physically contiguous direct KZERO RAM and checks full
CPU/physical extents. This is not portable noncoherent or IOMMU support.

## Queues, ports and binary I/O

`vconsole.[ch]` is the allocation-free core. Its inputs are immutable CPU-owned
snapshots for the entire call, never live DMA memory, with nonoverlapping buffers.
The adapter copies used-ring entries after barriers and validates the whole
batch before releasing a lease. Duplicate/unposted IDs, excessive advancement
or invalid lengths fault atomically. Modern TX used lengths must be zero.

`vconsole-dma.[ch]` owns aligned split-ring arenas, payloads, immutable snapshots
and exact receipts. Descriptors/avail entries precede barrier-ordered 16-bit
avail-index publication. Completed payloads remain held until copy/reap; serial,
epoch and generation checks prevent stale receipt reuse.

`vconsole-pump.[ch]` processes bounded control work and checked notifications.
A full reply FIFO retains RX receipt/payload until a reply can be queued.
Remove/readd and owner generation tags discard stale unsent replies; already
posted control messages cannot be retracted. Accept PORT_NAME with no NUL or
exactly one final NUL, matching the spec and inspected QEMU respectively.

`vconsole-stream.[ch]` posts QGA data RX only after exact-port claim and before
guest OPEN publication. Unclaimed nonconsole ports get no guest OPEN/data
buffers. Console RX is discarded separately and never delivered to QGA.
FIFO saturation retains complete unread payloads; TX copies into owned DMA
buffers and may return short writes. NUL and 0xff are ordinary bytes.
`Again` indicates runnable CPU work, not a busy wait for host/client progress.

Host PORT_OPEN/backend close affects availability only. It preserves guest Chan,
owner generation, RX bytes and leases; positive writes wait for reconnect.
PVE socket closure does not cancel or replay an executed QGA request.
Accepted TX is not proof of host delivery or durable storage.

Actual guest close, port removal or terminal fault retires the owner.
Only one published QGA owner is permitted per stream/controller epoch.
The negotiated queues cannot cancel individual posted leases: reopening requires
joined/drained callers, a newer confirmed full reset and fresh pump/stream/broker.
Retagging live buffers or clearing software indices is forbidden.

## Thread ownership and native blocking

One serialized worker owns runtime queue/control/data MMIO, queues, pump,
stream and port lifecycle. IRQ ISR acknowledgement is separate; the supervisor
owns startup/reset outside live runtime.
The controller's short lock protects admission, worker/client lifetime leases,
nonwrapping serials and an exclusive operation guard. Runtime access requires
the exact worker lease; client leases pin lifetime without granting MMIO access.
Callouts, allocation, sleep and user copies occur outside IRQ-safe locks.

The IRQ mailbox only acknowledges ISR, latches bits and wakes. Its ISR pointer
and callback target are immutable/pinned. Mailbox and native wake locks avoid
Rendez inversion; wake callbacks retain counted references.
Closing prevents new mapping reads but does not join pre-dispatched callers.
`intrdisable` is not treated as permission to free its aux target.

The worker reserves its lease before spawn. Done follows its last runtime and
sleep-predicate access, counted stop callbacks and lease return; Done is not a
native process reap or permission to free the worker/aux.
Bounded work yields after eight runnable bursts; idle/backpressured waits use
health deadlines. Native error/note frames belong to process context.

The broker stages writes before user copy/commit; Busy buffers belong to the
worker and Reserved/Taking buffers to the caller. Notes abandon requests without
freeing a still-borrowed buffer. Cancellation may follow consumed RX or posted
TX and provides no rollback/delivery guarantee. Live handles retain their leases
until clunk and can delay teardown. Native copies/sleep use immutable cleanup
tokens; no native error escapes while holding an ilock.

## PCI ownership, startup and recovery

`vconsole-owner-pc.[ch]` serializes same-registry PCI claims but is not a
kernel-global foreign-driver exclusion API. Final kernel config must exclude
competing drivers. Claim rejects existing BME, non-D0 state, enabled MSI/MSI-X,
all MSI capabilities and malformed lists. Checked 16-bit command writes avoid
W1C PCI status; failed/ambiguous writes retain ownership.

Startup attempts native IRQ registration, then enables/readbacks BME and
DRIVER_OK. In the default build native `intrenable` returns no success status
and may panic on allocation failure, so registration remains unproved and INTx
stays masked. The optional checked-API build with `qgavirtioirq=1` requires an
installed vector, binds it under the mailbox lock and unmasks only after
DRIVER_OK. Callback dispatch checks the actual Ureg vector before reading the
read-clear ISR; spurious unrelated dispatch is not delivery evidence. See
[IRQ](IRQ.md). Specific local emulated IRQ/reset/port tests passed; this is not
full hardware/fault/lifetime validation.

`qgavirtio=1` enables `vconsole-boot-pc.c`. It requires exactly one supported
console controller and scans at most 64 matching vendor candidates.
Four complete contexts are pinned for the boot; none is freed/reinitialized
after use. Successful retirement consumes a fresh bundle; exhaustion requires
reboot. Startup failures do not silently respawn.

Startup/drain allow 100 supervisor polls at 100 ms. These are retry bounds,
not wall-clock guarantees: native kproc resource waits may be indefinite.
Endpoint publication is asynchronous. The revised QGA helper waits with stat-only
probes before one launch. Native fixtures passed locally and on the Proxmox
guest, where the updated helper was installed and launched once. Operator host
queries and a normal shutdown/start reaching automatic agent availability then
passed; see STATUS for their scope and remaining target-specific checks.

Stop closes admission/IRQ, masks INTx and clears BME, then waits for actual
worker/client/operation/wake drainage. Namespace withdrawal precedes observed
status-zero reset and DMA/mapping release. FAILED status, BME clearing, software
epochs and timeouts are not reset proof. Ambiguous failure retains resources
and claims. Old callback/Chan targets remain pinned.
The kernel does not restart the userspace agent after owner retirement.

## Build and validation

```rc
mk test
mk kernel-check
rc kernel/build-pc64.rc
```

These run 9 native-compatible suites, compile 17 kernel-header objects and build
a separate `9pc64qga` (or an explicitly chosen unused configuration name).
Sources build directly using native port/pc rules; register/status enums come
from `virtio10.h` with ABI checks. See [UPSTREAM](UPSTREAM.md).
Linux independently runs 15 C suites and 22 Python methods;
see [DEVELOPMENT](DEVELOPMENT.md). Native post-split checks and checked-IRQ
kernel linking passed locally; full target-platform validation is still open.
Use [INSTALL](INSTALL.md) for explicit boot/config changes and rollback.

References: [VIRTIO 1.3 console §5.3](https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html),
[QEMU virtio-serial](https://github.com/qemu/qemu/blob/master/hw/char/virtio-serial-bus.c),
[PC64 configuration](https://github.com/9front/9front/blob/front/sys/src/9/pc64/pc64),
[native process/Rendez](https://github.com/9front/9front/blob/front/sys/src/9/port/proc.c),
[Dev helpers](https://github.com/9front/9front/blob/front/sys/src/9/port/dev.c) and
[Chan lifetimes](https://github.com/9front/9front/blob/front/sys/src/9/port/chan.c).
Source inspection and fixtures support these contracts; they do not prove
adversarial live IRQ/DMA/reset/unplug or full native platform lifetime safety.
