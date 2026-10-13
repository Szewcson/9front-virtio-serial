# Native virtio-console/serial driver

The driver now lives independently in `9front-virtio-serial`.
Its modern-capability PCI path is installed on the authorized test VM; native
exclusive binary I/O, operator-supplied PVE file/exec smoke tests and automatic
cold startup passed. INTx remains masked and bounded worker deadlines poll.
This is experimental hardware evidence, not production or interrupt certification.
See [STATUS](STATUS.md), [INSTALL](INSTALL.md) and [SECURITY](SECURITY.md).

Earlier per-layer evidence below records the implementation stages. “No kernel/
hardware” and combined test-count statements refer to that historical stage,
not current deployment. This repo builds only driver/test objects, never qga9.
Current contracts include the host-reconnect correction described below.

Sources:
[PC64 configuration](https://github.com/9front/9front/blob/front/sys/src/9/pc64/pc64),
[virtio register helpers](https://github.com/9front/9front/blob/front/sys/src/9/pc/virtio10pc.c),
[modern Ethernet driver](https://github.com/9front/9front/blob/front/sys/src/9/port/ethervirtio10.c),
[QEMU control/queue implementation](https://github.com/qemu/qemu/blob/master/hw/char/virtio-serial-bus.c),
[VIRTIO 1.3 console specification, section 5.3](https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html).

## What is implemented now

`qga9 -t stream -d PATH` serves an existing native duplex stream without UART
configuration. The default remains `uart`, so old config continues working.
The boot helper understands both. Protocol handlers and shutdown have no UART
dependency. A native pipe smoke test exercises the actual raw-stream entry point.

This is the separate QGA repository's userspace adapter, not part of this
kernel build. Its handlers/codecs are not linked into the driver. Build and test
both repos independently, then connect them through the native stream endpoint.

### Implemented pure core

`kernel/vconsole.c` and `kernel/vconsole.h` use native fixed-width types and only
`memmove`, `memset` and `strcmp`. They allocate nothing, retain no borrowed
pointers and have no mutable globals. They also compile in the actual PC64
kernel context without user-space libc headers. The subsequent bootstrap batch
links them into a separate opt-in test kernel; the daemon stays independent.

- `vcpci`: bounded/cycle-checked PCI-capability walk, modern/transitional console
  IDs, explicit BAR index/type/extent/overflow checks, four required nonoverlapping
  MMIO regions and minimum register lengths. Output is committed only on success.
  This prototype rejects I/O BARs and duplicate known region capabilities;
  it does not silently normalize an invalid BAR index with modulo. `vcnotify`
  checks the wide queue-offset multiplication and final two-byte notify extent.
- `vcportqueues`: checked data-queue indices, preserving control queues 2/3.
  `vcqpost` emits a direct split-ring descriptor with the correct RX/TX direction
  and records its buffer lease and caller-supplied generation cookie.
- `vcqconsume`: validates a whole immutable used-ring snapshot before releasing
  any slot. It checks index advancement, duplicates, posted IDs and RX lengths;
  modern TX completions must have zero used length. Invalid device input leaves
  all leases held and sets a sticky fault. Queue indices wrap at 16 bits; reset
  epochs are nonwrapping 64-bit values.
- `vcdecode`/`vcencode`: bounded little-endian control messages and strict UTF-8
  names. Incoming and outgoing event directions are checked. `vcreceive` owns
  the named-port lifecycle, rejects conflicting/duplicate metadata and never
  selects a text console or a QGA-name prefix lookalike.
- `vcclaim`/`vcalive`/`vcrelease`: one owner for the exact nonconsole
  `org.qemu.guest_agent.0` port. Host backend close changes availability only;
  the live guest owner and generations remain unchanged. Remove/readd, guest
  release and reset invalidate stale tokens. A stale guest close cannot affect
  a new owner. Late receipts are filtered by actual guest ownership, not each
  PVE socket connection.

Bounds are 256 descriptors/queue (power-of-two size), 4096 bytes/buffer, 64 ports
(IDs 0–63) and 127 UTF-8 name bytes plus a local NUL. A control frame is at most
136 bytes. These are explicit prototype limits, not input-driven allocations.
The adapter must reject or deliberately negotiate a supported device layout.

### Ownership and integration contract

One serialized driver worker must own each initialized state object. Init is
for fresh/unpublished objects only. Inputs must be **immutable CPU-owned
snapshots for the entire call, never live DMA memory**, with nonoverlapping state,
input and output buffers. In particular, a device must not be able to change a
used entry between the validation and commit passes. The adapter must supply
snapshot copying, bounds and memory barriers before calling this core.

The core emits descriptors, not avail-ring publication or MMIO notifications.
The caller must process/copy completed RX data before reposting its buffer and
filter its generation cookie before delivering data to a stream owner.
`vcclaim`/`vcrelease` do not themselves send PORT_OPEN. IRQs must not concurrently
mutate core state. No lock, interrupt handler or hardware barrier is hidden here.

`vcqreset` and `vcportsreset` require `quiesced == 1`, but this argument is a
caller assertion, **not proof that DMA stopped**. Only the future hardware adapter
can prove device reset completion and drain IRQ/worker/channel references before
reuse/free. A software generation counter cannot prevent a device writing an
old physical buffer. On protocol faults, hold resources until that proof.

### Executed tests

```sh
make -C tests test
make -C tests sanitize
make -C tests analyze
```

On stock 9front amd64:

```rc
mk test vconsole-kernel-check.6
```

The core fixture passed 159796 assertions on both platforms, including more than
65536 real post/completion cycles, deterministic hostile PCI/ring/control
snapshots, atomic rejection, overflow, UTF-8/NUL variants, reconnect and stale
cookie/close cases. Counts include parameterized checks, not distinct scenarios.
All eight portable suites and 54 host Python methods passed; ASan/UBSan/leak
checks and GCC analyzer passed. The compile-only kernel-context object has only
the three memory/string dependencies above. None is a live DMA/IRQ test.

### Implemented PC64 mapping and MMIO initialization

`kernel/vconsole-mmio.[ch]` owns the four bounded mapping references and a
single-owner initialization state machine. `kernel/vconsole-pc.[ch]` supplies
actual native PCI reads, volatile byte/word/dword MMIO, `vmap`/`vunmap`,
`coherence` barriers and one-millisecond reset polling. It uses assigned `Pcidev`
BAR metadata, rejects malformed 64-bit BAR/high-half layouts, and requires
memory decoding already enabled by the exclusive PCI owner. It rejects enabled
MSI/MSI-X; this prototype targets INTx and does not modify PCI configuration.

1. Map only common 56, ISR 1 and device 8 bytes, plus at most 4096 notify bytes.
   A partial map failure consumes acquired references in reverse order before
   any register I/O. A notify location outside the mapped prefix fails safely.
2. Write reset once, then wait for **observed status zero**, up to 1000 polls.
   A failure keeps every mapping and faults the object; setting FAILED does not
   authorize release. Only confirmed completion advances the nonwrapping epoch.
3. Negotiate exactly MULTIPORT and VERSION_1, with ordered status progression
   and FEATURES_OK readback. Do not accept packed/indirect/event-index,
   platform/IOMMU, notification-data or queue-reset features. A rejecting device
   is unusable, not silently treated as compatible.
4. Read max_nr_ports with generation-before/after consistency and at most eight
   attempts. Require 1–64 ports and enough queues for their data/control pairs.
   Queue discovery validates a nonzero power-of-two size, disabled state and
   checked notify location, returns a capped size and epoch, and commits no
   partial result. Caller ID/output errors fail before device I/O.
5. Drop references only from the confirmed-reset state **and** after the caller
   asserts that IRQ/worker/Chan references are drained. That assertion is not
   reference management; the future owner must implement it.

The inspected PC64 `vunmap` validates its argument but retains mappings. The
native backend therefore rejects extents outside the deterministic direct
`VMAPSIZE` physical window (1 TiB in the inspected kernel), avoiding allocation
of fresh high-physical virtual ranges on retries. Even inside that window,
dropping driver references does not reclaim mappings/page tables. This is an
explicit native limitation, not a claim of general hot-unplug cleanup.

The initialization entry points do **not** register a Dev, enable bus mastering
or IRQs, allocate DMA buffers, set DRIVER_OK or expose QGA. The subsequent DMA
layer uses `vcmmenable` to write/read back queue size, addresses and enable=1;
the earlier initialization tests still exercise no queue enable/address writes.
Exclusive PCI-driver ownership is a caller requirement, not established merely
by checking MSI/MSI-X. Init/mapping/reset/teardown are process-context operations
and cannot race another driver, IRQ, worker or channel owner.

The final simulated-register fixture passed 256194 assertions on Linux and
stock 9front. Linux tests of the actual PC adapter against fake native primitives
passed 37847 assertions, including every PCI-byte read failure, partial mapping
unwind, direct-window boundaries and independently checked MMIO access. These
counts include per-access guards/parameterized cases, not distinct scenarios.
Native compilation against actual PC64 headers passed for both new kernel
objects. At that stage Linux ran ten C suites and native `mk test` ran nine. All 54 Python
methods, ASan/UBSan/leak checks and GCC analyzer also passed. None of these tests
calls this adapter on live hardware or proves its DMA/IRQ compatibility.

Native compilation/test commands (amd64; not installation):

```rc
mk test vconsole-kernel-check.6 vconsole-mmio-kernel-check.6 vconsole-pc-check.6
```

### Implemented coherent DMA queue layer

`kernel/vconsole-dma.[ch]` supplies one-worker queue ownership and actual shared
ring accesses. `vcpcdmaops` supplies native `mallocalign`, `free`, checked
`PADDR` translation and `coherence`. This targets coherent little-endian PC64
RAM with direct physical addressing, not IOMMU or noncoherent platforms.

- Each arena contains a 16-byte-aligned descriptor table, two-byte-aligned
  available ring, four-byte-aligned used ring, both reserved event words and
  individually aligned payload slots. Bounds are 1–256 power-of-two entries,
  1–4096 bytes per payload, at most 1055264 allocated bytes per queue. Controller
  budgeting/allocation scheduling is still a future worker responsibility.
- Allocation checks the complete CPU/physical extents, alignment and exact byte
  count before zeroing any memory. Native allocation is restricted to direct
  KZERO RAM; first/last physical translations must agree with its linear extent.
  Callback error with an acquired base consumes that owned allocation once.
  Nothing is published until a separate explicit bind.
- Binding rediscover-checks the queue/epoch/notify metadata, sets and reads back
  queue size, uses NO_VECTOR, writes and reads back all six 32-bit halves of the
  three 64-bit ring addresses, then enables and reads back the queue. Ring extents
  are checked for alignment, overflow and overlap. Exposure is marked **before**
  MMIO so even an ambiguously applied failed write cannot authorize direct free.
- TX copies caller bytes into owned DMA memory; RX exposes only a bounded
  writable slot. Descriptor and avail entry writes precede a barrier and a
  single aligned 16-bit avail.idx store, followed by another barrier. No buffer
  can be posted twice or while holding a completed-but-unread payload.
- Poll reads used.idx once, acquires the entries, copies only bounded ring bytes
  into immutable CPU storage and invokes the core's whole-batch validation.
  No partial output/lease release occurs on a malformed batch. The completion
  scratch space is queue-owned rather than a large kernel stack allocation.
- Completion keeps the payload held. `vcdmatake` copies only validated RX length
  (TX reaps with zero bytes), verifies ID/epoch/cookie plus a nonwrapping per-slot
  serial, and consumes the exact receipt once. Reusing a slot with the same port
  cookie/length cannot make an old receipt valid. The future stream worker must
  still filter port-generation cookies before delivering data.
- Unexposed allocation may unwind directly. Once binding was attempted, storage
  survives setup/completion/reset failure until a **newer confirmed full-reset
  epoch** and caller-drained references. DMA queues must be dropped before MMIO.
  A drained flag is an assertion, not implemented IRQ/Chan reference management.

The DMA layer itself does **not** set DRIVER_OK, notify queues, enable PCI bus
master, register IRQs, schedule a worker, expose a Dev/Chan, or run on a live
controller. The runtime helpers below supply startup/health/notifications, but
the future controller worker must call them and own backpressure, reset and
drained-reference teardown. DMA polling checks software state/epoch; it is not
an autonomous device-health monitor.
Completion/reset ownership relies on a spec-compliant device. A malicious
hypervisor can lie about completion or write RAM regardless of these checks.

Native and Linux simulated DMA fixtures passed 313751 assertions, including all
bounded layout capacities, malformed allocation results, every setup read/write
failure, readback mismatches, stuck reset, held RX payloads, forged/stale receipts,
malformed whole-batch completions, snapshot isolation and 65538 real cycles through
16-bit avail/used wraparound. The PC shim passed 37941 checks, including actual
allocator callback bounds/physical-contiguity failure. Counts include parameterized
and per-access checks, not distinct scenarios. Ten native/eleven Linux C suites
and 54 Python methods passed; ASan/UBSan/leaks and GCC analyzer passed. Native
kernel compilation passed for DMA, MMIO and PC objects; none was linked/loaded.

Native checks (not installation):

```rc
mk test
mk vconsole-kernel-check.6 vconsole-mmio-kernel-check.6 vconsole-dma-kernel-check.6 vconsole-pc-check.6
```

### Implemented IRQ handoff and runtime MMIO

`kernel/vconsole-irq.[ch]` supplies an IRQ-safe mailbox with copied immutable
callbacks, a pinned controller epoch, coalesced queue/config/work bits and
sticky stop/fault state. Its interrupt path barriers and reads exactly one
read-to-clear ISR byte, latches defined bits under the mailbox lock, then wakes
outside that lock. It never reads or mutates worker-owned queue/core/MMIO state.
Reserved ISR bits are ignored; failed, oversized or all-ones reads latch faults.

Wake callbacks hold counted references until they return. Closing is irreversible
and makes late IRQ dispatch skip the ISR mapping. Closed/drained gates mapping
release only after hardware reset and worker join; it does **not** authorize
freeing the mailbox, whose pre-dispatched callers may still exist. Retain its
locks/aux for boot lifetime unless the platform separately proves dispatch,
predicate and caller drainage. Never rearm an old mailbox in place.

`kernel/vconsole-wake.[ch]` uses actual PC64 `ilock`/`iunlock`, `Rendez`, `tsleep`
and `wakeup`. The ISR pointer is an immutable pinned snapshot, not a concurrent
read of `VcMmio` metadata. Native sleep tests readiness under the rendezvous
lock. Wakeups are serialized with a second lock and never hold the mailbox
lock, avoiding lock-order inversion and wake-before-sleep loss. One worker may
wait with a bounded 1–1000 ms health-check deadline; its future kproc must own
the native note/error frame. The adapter supplies an entry point, **not** IRQ
registration or a running worker. Native `intrdisable` was not assumed to join
every pre-dispatched callback or authorize freeing its aux object.

Worker-only MMIO helpers now supply:

- `vcmmstart`: require all bounded data/control queues successfully configured
  in the current epoch; set DRIVER_OK and verify status before entering Running.
  Caller must already have pinned IRQ state, ready RX buffers and authorized
  bus mastering. The helper does not establish those obligations itself.
- `vcmmhealth`: bounded status/config-generation checks in Features or Running;
  unexpected reset, NEEDS_RESET, changed port count or unstable config faults
  the controller rather than silently reusing old queue layout.
- `vcmmkick`: match cached configured queue ID/size/epoch/notify metadata and
  write exactly a checked 16-bit queue ID within the mapped notify prefix, only
  after confirmed DRIVER_OK. Status is checked before/after notification.

Failures retain exposed buffers and mappings until confirmed reset plus drained
references; a failed notification may already have been applied. Confirmed reset
clears configured-queue caches. DMA completion/posting supports the Running
state. The subsequent pump supplies budget planning and control-message
scheduling, not a live controller owner.

Final Linux normal/ASan/UBSan/leak runs passed thirteen C suites and 54 Python
methods; GCC analyzer passed. Stock 9front passed eleven C suites and all six
actual kernel-header compile checks (including the native wake adapter), without
linking/loading a kernel. The IRQ fixture passed 33596 checks and the expanded
DMA/runtime fixture 317933 on both platforms. Counts include parameterized and
per-access assertions, not distinct scenarios. Linux pthread tests passed
600000 concurrent IRQ/work events and blocked-waker teardown under ThreadSanitizer.
These exercise mailbox interleavings and simulated registers, not native live
interrupts, a working controller pump, hardware DMA or usable virtio QGA.

```rc
mk test
mk vconsole-kernel-check.6 vconsole-mmio-kernel-check.6 vconsole-dma-kernel-check.6
mk vconsole-pc-check.6 vconsole-irq-kernel-check.6 vconsole-wake-check.6
```

### Implemented bounded control pump and startup footprint

`kernel/vconsole-pump.[ch]` integrates the existing DMA/core/MMIO helpers into
worker-callable startup and bounded control-plane iterations. It borrows a
genuinely bound, ordered, unused queue set; allocation, exclusive PCI ownership,
IRQ registration and native kproc scheduling are **not** hidden in these calls.
The entire pump/port/queue/MMIO state remains owned by one serialized worker.

- `vcpumpbudget` checks all queue IDs/sizes/epochs and plans RX-control capacity
  136, TX-control 8 and caller-selected bounded data capacity (default 256).
  It counts DMA arenas plus queue/pump/MMIO objects against a hard 16 MiB limit
  and commits output only on success. Run it **before** allocating/binding any
  queue; init rechecks actual capacities but does not authorize optimistic
  allocation. Allocator overhead and future Dev/Chan/platform-registry objects
  still need separate accounting. Queue objects must be allocated for the
  planned count, not an unaccounted worst-case array.
- `vcpumpstart` primes every data/control RX queue before DRIVER_OK, then
  notifies RX and posts/notifies DEVICE_READY. Initial data leases use cookie
  zero (no stream owner), while control leases use controller epoch. A future
  data adapter must drain/filter/repost leases before any client delivery;
  control-plane ownership alone is not proof that payload belongs to that owner.
- `vcpumpstep` checks health, captures at most one whole bounded completion
  snapshot per control queue and performs 1–256 reap/send/receive actions.
  Receipt FIFOs preserve used-ring order even when descriptor IDs complete out
  of order. CPU-owned receipts and frame scratch live in the pump, not a large
  kernel stack object. RX buffers are reposted only after their exact receipt
  was copied/consumed and the incoming control message validated/applied.
- A 16-entry reply FIFO handles PORT_READY for an added port and mandatory
  OPEN=1 for a console port. A full FIFO holds the next RX receipt, not a dropped
  reply or premature RX repost. TX saturation returns no runnable-CPU flag;
  the future worker must sleep on IRQ with a bounded health deadline. `Again`
  indicates executable CPU work, not permission to spin waiting for the host.
- Port-incarnation tags discard obsolete **unsent** replies after remove/readd.
  Guest OPEN/CLOSE also carries ownership generation, preventing an old queued
  close from affecting a new claim. Already posted messages cannot be retracted;
  late completion only releases its exact DMA receipt, never updates ownership.
  Exact-QGA claims remain pending until OPEN is posted and successfully notified.
  Host OPEN/close preserves a live guest claim and pending guest OPEN reply;
  reset/actual removal revokes it. Console ports are never selected as QGA.
- Malformed input, lifecycle conflict, receipt/MMIO failure, generation exhaustion
  or reset is fail-stop. No reset/free/retry is implicit, including a notification
  which may have applied before reporting failure. Logical stop invalidates
  ownership without freeing memory; the future controller must close IRQ gate,
  confirm hardware reset, join worker/Chan callers and only then release queues.

Final Linux normal/ASan/UBSan/leak runs passed fourteen C suites and 54 Python
methods; GCC analyzer passed. Stock 9front passed twelve C suites and all seven
actual PC64 kernel-header compile checks, including the new pump. Its fixture
passed 2047966 assertions on both platforms: all supported default-budget
port/queue dimensions, atomic budget rejection, all startup/receive/reply MMIO
failure positions, saturation/resumption, ordered receipts, single-owner/pending
claim, reconnect/stale replies, malformed control/ring frames and 65538 cycles
through real RX/TX/FIFO wraparound. Counts include per-access/parameterized
checks, not distinct scenarios. No live controller, kproc or kernel was loaded.

```rc
mk test vconsole-pump-kernel-check.6
```

The subsequent data layer supplies nonblocking QGA delivery/backpressure and
console discard. Remaining integration work includes PCI acquisition/unwind,
IRQ registration, native scheduling/notes/stop-join, automatic reset/reopen and
a reference-safe blocking Dev/Chan. These helpers do not yet provide a usable
virtio device or permit switching the working PVE channel.

### Implemented nonblocking data stream

`kernel/vconsole-stream.[ch]` supplies worker-only literal binary reads/writes,
not a blocking namespace device. Init attaches one data adapter to a fresh pump
and selects deferred data RX: control RX is populated before DRIVER_OK, while
QGA RX is populated only after exact-port claim and **before** its guest OPEN can
be sent. All queues are configured before DRIVER_OK; an unclaimed nonconsole
port has no guest OPEN and no posted data buffers. Console OPEN acknowledgements
remain in the control pump; the data worker populates/discards their RX queues.

- `vcsbudget` reserves the entire stream object (FIFO, snapshots, receipt arrays
  and scratch) in the original hard 16 MiB footprint, before queue acquisition.
  Platform/allocator/Dev/Chan overhead still requires separate accounting.
- RX preserves used-ring order, copies only validated bytes into a 4096-byte
  FIFO and consumes/reposts an exact receipt once. If the entire next payload
  cannot fit, its receipt/payload remains held: no truncation, overwriting or
  premature repost. Reads are bounded and may be short; zero-byte completions
  produce no fabricated data/EOF. NUL and 0xff are ordinary raw bytes.
- TX copies up to one queue payload (default 256 bytes) per bounded call and
  returns a short write. Full TX returns Wait without accepting more bytes;
  accepted data is private DMA storage, not a retained caller pointer. Completion
  reaps once. Write acceptance is not a host-delivery/durable-flush guarantee.
- Worker tokens include boot-unique controller identity, reset epoch and port
  generation. Identity is supplied by the future native owner, not an OS
  permission check or a secret capability. Wrong/stale tokens cannot read,
  write or close the owner. Pending OPEN returns Wait; it becomes live only
  after successful publication/notification. Host backend close preserves RX
  bytes and owner tokens, while positive TX waits for reconnect. Controller
  faults/actual retirement block I/O; all exposed DMA remains retained.
- Console RX uses a separate zero-cookie discard path and is never delivered
  to QGA. Retired console buffers can be reaped but not handed to a new QGA
  owner. Queue snapshots are captured before the bounded action budget, so a
  shared IRQ plus a one-action iteration cannot hide other completed queues.
  RX/TX work and sink work alternate, with round-robin sink ports/slots.
  `Again` excludes an RX FIFO blocked by client reads or a full TX ring.
- The prototype permits **one published QGA owner per stream/controller epoch**.
  Split-queue RX/TX leases cannot be individually cancelled by this negotiated
  feature set. Guest close or actual port removal therefore requires a
  confirmed full controller reset plus joined/drained callers and fresh pump/
  stream before reopening. Reassigning cookies on still-posted leases or merely
  clearing software indices is forbidden. Late old receipts are discarded/reaped,
  never reposted for a new owner. The controller manager below now supplies
  supervisor-driven reset/reopen, not automatic recovery scheduling.

Native and Linux stream fixtures passed 1909950 assertions, including binary
FIFO saturation/resumption/wrap, short reads/writes, token rejection, pending
OPEN, late close/disconnect/remove receipts, console fairness and console-to-QGA
ID reuse rejection, malformed rings, failed kicks, budget/init boundaries,
out-of-order used entries and 65538 real single-slot data RX/TX cycles. Counts
include per-access/parameterized assertions, not distinct scenarios. Final
normal/ASan/UBSan/leak and GCC analyzer checks passed fifteen Linux C suites and
54 Python methods; stock 9front passed thirteen native C suites and all eight
actual PC64 kernel-header checks. No kernel was linked/loaded or hardware run.

```rc
mk test vconsole-stream-kernel-check.6
```

The controller layer below supplies queue acquisition/unwind and stop/join/reset
gating. The following layers supply the exclusive PCI/IRQ owner, native kproc,
bounded waits and worker request broker. Still supply automatic recovery,
publish an actual Dev/Chan namespace and prove
reconnect/hardware behavior in a recoverable clone. A memory-safe core is not
proof of these integration paths, and ISA remains installed/unchanged.

### Implemented controller acquisition and reference-gated reset/reopen

`kernel/vconsole-controller.[ch]` owns a borrowed exclusive MMIO object and
acquired CPU/DMA storage. The MMIO object and its nonwrapping epoch survive
session replacement; reset never means reinitializing a live epoch counter.
`vconsole-controller-pc.[ch]` supplies actual ilock/iunlock and malloc/free
callbacks, compiled against stock PC64 headers but not activated.

- `vccprepare` confirms reset, negotiates features and discovers every queue.
  Its exact hard-16-MiB planner includes MMIO, controller bookkeeping, session
  (pump/stream/pointer table), the exact queue array and all DMA arenas BEFORE
  any acquisition. Platform allocator overhead, separately boot-pinned IRQ
  objects and future Dev state are not included and must be budgeted separately.
  All metadata/DMA allocation completes before the first queue bind.
- Partial allocation failure releases only acquired blocks, once. Any bind
  attempt makes the DMA conservative-exposed: unwind must confirm a newer reset
  before freeing it. Failed reset retains the entire allocation/session for a
  later explicit retry; no unmapping, live index clearing or cookie retagging.
- Startup requires the platform's fresh registered/pinned current-epoch IRQ
  mailbox and bus-master authority; it does not acquire those itself. Worker/
  client admission opens only after DRIVER_OK/DEVICE_READY succeeds. Concurrent
  stop prevents publication; startup failure closes the IRQ/admission gate and
  holds all exposed resources.
- A short IRQ-safe lock protects bounded lifetime leases (one worker, 64 clients),
  nonwrapping slot serials, epoch, admission and an exclusive operation guard.
  Allocator/MMIO/IRQ callbacks run outside that lock. Runtime calls require the
  exact worker lease and guard; client leases pin lifetime but grant no stream
  access. The future Dev must send requests to the worker, not touch borrowed
  stream/queue pointers. Duplicate/stale/wrong-role leases cannot release or
  mutate new owners. None of these identifiers substitutes for OS permissions.
- Stop closes admission and the separate IRQ gate without touching rings while
  an operation is in flight. `vccreset` returns Wait until ALL returned leases,
  the operation guard and actual counted IRQ wake callbacks have drained. A
  caller's last put must include its sleep-predicate access. Only then may it
  confirm hardware reset and drop DMA/metadata. Even early erroneous put cannot
  bypass an in-flight operation guard. A missing put intentionally prevents
  reclamation; force-dropping another caller's lease is forbidden.
- Reset success leaves Idle. Explicit prepare/start with fresh IRQ state creates
  a new session/epoch; old stream tokens and same-epoch ABA lease receipts are
  rejected. Old IRQ mailbox/locks/aux remain boot-pinned because a delayed native
  dispatch may still call them. The future platform owner must handle interrupt
  deregistration and bound retained mailbox memory; manager reset is NOT proof
  that the interrupt registry or all native processes have joined.

The lifecycle fixture passes 41171 native assertions and 41739 Linux assertions,
including exhaustive init MMIO reads/writes, both metadata/every DMA acquisition
failure, all bind positions with failed unwind reset, runtime reset/fault,
default-capacity maximum 64-port/256-descriptor layout, exact budget rejection,
latched ISR fault propagation despite healthy common status,
literal binary I/O across a fresh epoch and old-ref/token/mailbox rejection.
Linux additionally proves deterministic blocked worker/DRIVER_OK/waker teardown
and 40000 real concurrent reference/worker cycles. Counts include repeated
per-fixture assertions, not distinct scenarios. Sixteen Linux C suites and
54 Python methods pass normal/ASan/UBSan/leaks; GCC analyzer and controller
ThreadSanitizer pass. Fourteen stock-native C suites and ten real PC64
kernel-header compile checks pass. No kernel is linked/installed or live DMA run.

```rc
mk test vconsole-controller-kernel-check.6 vconsole-controller-pc-check.6
```

The next layer below binds these leases to a worker and supplies native PCI
reservation/registration/command helpers. Blocking Dev/Chan requests,
checked IRQ activation and clone hardware tests remain gates; no automatic
hardware recovery or usable device is claimed.

### Implemented one-shot worker and native PCI ownership helpers

`kernel/vconsole-worker.[ch]` reserves the exact worker lease BEFORE spawning a
runner and validates the actual attached IRQ object, not just its epoch. Its
short lock protects phase/stop/reference handoff; no lock crosses a callout.
One runner performs bounded queue iterations and yields after eight runnable
bursts. Idle/backpressured work uses 1–1000 ms health deadlines; waiting does
not consume IRQ bits, so the controller alone sees latched faults before queues.

Spawn errors promise no runner, and wait/yield errors return normally. Startup
failure, normalized note/error or cooperative stop closes the controller but
never frees DMA itself. Stop callouts are counted: even after the runner returns,
its lease cannot be released while another stop callback still borrows the
controller. The last stopper can complete final release without a spin join.
Done is published AFTER the actual last controller/sleep-predicate access and
lease return. Late old-worker stop cannot close a reopened epoch. Worker/aux stay
boot-pinned after Done because wakeup, kproc return and join queries can remain;
Done is not an OS process reap or permission to free/reinitialize the context.

`vconsole-worker-pc.[ch]` supplies actual ilock, kproc, tsleep, sched and wakeup
adapters with local waserror/poperror frames and a null-process-context check;
these entry points must never run in IRQ context. `vcpcwakepause` waits without
consuming IRQ events. Native kproc creation may wait for a process slot; it has
no finite startup deadline. Notes/error catching and spawn-before-ready semantics
were checked against native source and compiled, not live-injected into a kproc.
Sources: [native worker creation](https://github.com/9front/9front/blob/front/sys/src/9/port/proc.c),
[interrupt registration](https://github.com/9front/9front/blob/front/sys/src/9/pc/irq.c),
[MSI-first routing](https://github.com/9front/9front/blob/front/sys/src/9/pc/mp.c).

`vconsole-owner-pc.[ch]` supplies one boot-pinned four-controller registry with
nonwrapping identities and serialized per-owner supervisor operations. It
prevents same-TBDF and accidental same-object duplicate claims before erasing
state or writing config. It is NOT a kernel-global claim API: the final native
Dev/config must exclude foreign drivers and use this single registry. Existing
BME, enabled MSI/MSI-X, all MSI capabilities, non-D0 power and malformed/cyclic
capability lists are rejected before changes. MSI presence is rejected because
native registration may otherwise automatically enable MSI; unsupported modes
are not silently selected. Assigned BARs and parent decoding are prerequisites.

- Claim reserves ownership/identity, sets memory decode and INTx mask using
  checked **16-bit command writes**, never a dword affecting W1C PCI status.
  Readback and the native cached command are kept consistent; failed/ambiguous
  writes retain the claim unless the saved command is positively restored.
- Bind the fresh manager BEFORE acquisition, so even failed prepare retains
  DMA under a known owner. Required `vcpcownerprepare` subtracts standard native
  owner/worker/IRQ/gate/I/O broker and the whole registry footprint before the manager's
  <=16-MiB acquisition budget. Extra retained epochs, future Dev state and
  allocator overhead require separate limits; no automatic allocation/retry
  pool is hidden here. A successful budgeted prepare is required for startup.
- Startup attempts native registration with exact device/function/aux/name,
  enables BME with readback, then sets DRIVER_OK through the manager. Native
  `intrenable` returns void and can panic on its own allocation failure. Its
  registration is an ATTEMPT, not a proven success. **INTx stays masked** to avoid
  an unhandled interrupt storm; bounded worker polling remains available.
  Checked registration support/proof is required before unmasking hardware IRQs.
- Stop closes controller/IRQ admission, masks INTx/clears BME, deregisters the
  exact attempted handler, and waits for actual leases/operations/wakers.
  Only a confirmed reset may free DMA/detach mappings. Restore saved command
  (original BME was clear), then release registry. Failure retains ownership;
  restore retry after detach does not reset/free/unregister twice. Late IRQ
  dispatch remains harmless via the permanently closed, boot-pinned mailbox.

Worker fixtures now pass 1948 native assertions and 5157 Linux assertions, including
spawn/wait/yield failures, fairness, wrong-IRQ/second-worker rejection and old
worker/new-epoch protection. Linux adds real blocked spawn, predicate and stop-
callback last-access joins. Native PCI glue passes 120 Linux checks with fake
native primitives, real mapping/IRQ helpers and explicitly ordered controller
stubs; it is NOT a combined live PCI/DMA/kproc test. Final host normal/sanitizer/
analyzer suites and worker/registry ThreadSanitizer runs pass; native tests and
kernel-header compilation are recorded in STATUS. No native kproc or IRQ/device
has been activated and no kernel was installed. The existing ISA agent is intact.

```rc
mk test vconsole-worker-kernel-check.6 vconsole-worker-pc-check.6 vconsole-owner-pc-check.6
```

The next layer below supplies requests and blocking adapters. Actual Dev/Chan
publication/permissions and bounded context retention/recovery scheduling are
still missing. Add checked IRQ activation (or deliberately keep polling), link
a recoverable test kernel and verify in a disposable clone before changing the
production channel. Merely calling native APIs is not hardware proof.

### Implemented request broker and native blocking adapters

`kernel/vconsole-io.[ch]` separates client lifetime from worker authority. One
owner and eight fixed 4-KiB request slots are boot-pinned per controller epoch.
Native owner prepare reserves the entire `VcPcIo` footprint, including request
buffers, locks and Rendez objects, BEFORE controller metadata/DMA acquisition.
Dev state, allocator overhead and retained future epochs still need accounting.

- Open acquires owner/request client leases, queues work and waits for the exact
  named nonconsole port and actually posted/notified guest OPEN. Another owner
  is rejected, regardless of how a future namespace aliases the endpoint.
- Each acquired lease is checked against the broker's captured identity/epoch.
  Request serials and owner generations never wrap. A recycled slot or a full
  reset cannot make an old completion, cancellation or clunk target a new owner.
- Writes reserve private staging before a possibly faulting user copy; commit
  surrenders input access. Queued requests become exclusively worker-owned while
  executing. Completed reads transfer private output ownership until last copy
  and reap. Neither user pointers nor borrowed user payloads enter runtime queues.
- A note can abandon pending work immediately; Busy work retains its lease and
  buffer until the worker's LAST access. Cancellation may follow consumed RX or
  posted TX, so no rollback/delivery acknowledgement or blind replay is promised.
- Round-robin service is bounded and never spins `Again` on absent ports, empty
  RX or full TX. A blocked read does not serialize a write on the same owner.
  Deadline polling notices new submissions; the broker does not steal IRQ events.
- Clunk only marks owner closure. The worker performs stream close. Before a
  claim, a cancelled open can allow a fresh owner generation; after a published
  claim, a fresh reset/pump/stream/broker is mandatory, not ring/cookie retagging.
- Worker finish closes controller admission BEFORE broker shutdown and returns
  its lease AFTER client wake/cleanup. Reserved/Taking caller buffers survive
  shutdown. Live handles retain their lifetime lease until clunk; a process with
  an open handle can therefore delay teardown, never authorize premature free.
- `vciostatus` copies locked CPU state/counts only, no user payload or MMIO. It is
  an API for a future read-only status node, not an already published namespace.

`vconsole-io-pc.[ch]` supplies actual process-context blocking open/read/write,
one Rendez per slot, serialized driver wakes and native waserror cleanup around
user copies/tsleep. Copies occur outside ilocks; immutable cleanup tokens exist
BEFORE native error frames. Kernel-stack use stays small, not a 4-KiB temporary
per operation. Fresh pre-launch `vcpcworkerbindio` attaches the broker to paired
worker service/shutdown hooks; failed bound spawn also executes finish cleanup.
Unbound startup failure remains a supervisor cleanup obligation.

Inspected stock [sleep/tsleep/timer code](https://github.com/9front/9front/blob/front/sys/src/9/port/proc.c):
the timer retains the boot-pinned Rendez, not the caller's stack predicate argument;
the predicate executes in the sleeping process and notes cancel the timer. This
supports the stated argument lifetime, but is not live note/fault-injection proof.
Native adapters compile against actual kernel headers; they were not executed.

Native broker fixtures pass 1002 assertions; Linux passes 1341, including real
blocked-TX abandonment and 2000 concurrent request cycles. Tests also cover actual
controller reset/reprepare, stale owner/request epochs, caller-owned buffers at
shutdown, pool/serial bounds, short binary reads/writes and backpressure. Counts
include per-fixture assertions, not distinct scenarios. Normal/ASan/UBSan/leak
and GCC analyzer suites pass; final broker ThreadSanitizer passes. See STATUS
for native compile results and sanitizer-runtime startup limitations.

```rc
mk test vconsole-io-kernel-check.6 vconsole-io-pc-check.6
```

The layer below now implements the Dev namespace and Chan ownership as code.
It is not activated or a usable virtio QGA channel on the VM. Bounded recovery,
startup/config integration and a recoverable test kernel remain. The installed
ISA daemon, boot kernel and PVE configuration are unchanged.

### Implemented native data/status Dev and pinned Chan views

`kernel/devvconsole.c` and `vconsole-dev.h` define native `vconsoledevtab`, rune
`Y`. The rune was absent from the running stock PC64 driver list and inspected
port/PC sources; final kernel configuration must still exclude collisions.
No new rune/device was registered in the running VM by these compile checks.

| Defined interface | Mode | Meaning |
| --- | --- | --- |
| `#Y[0..3]` (`#Y` defaults to slot 0) | eve-owned 0500 directory | Only an explicitly published, assigned endpoint; no automatic discovery |
| `#Y[0..3]/data` | eve-owned exclusive 0600, ORDWR only | Literal binary stream through the existing worker broker |
| `#Y[0..3]/status` | eve-owned 0400, OREAD only | Bounded CPU snapshot of identity/epoch/owner and request counts, no MMIO/payload |

Every attach/walk/stat/open/read/write/block-read checks `iseve`. A delegated
FD cannot bypass checks after changing user. Close deliberately omits permission
checks so a different native clunk process/recipient can release the exact owner.
No create/remove/wstat, chmod bypass, control commands or arbitrary port names
are supplied. Normal namec strips OCEXEC before the Dev; remaining noncanonical
open modes including truncate/remove-on-close are rejected.

- Four static registry slots, no heap allocation for an endpoint/data-open
  handle. Required native owner prepare now counts `VcPcDev` and the entire Dev
  registry BEFORE controller acquisition. Stock Chan/block allocator costs and
  retained future contexts still need separate bounded accounting.
- Publish requires actual `Running` worker entry, exact admitted lease and
  bound IRQ/broker epoch. `Launched` is set before spawn and is NOT ready proof.
  The supervisor must bound waits/retries and clean up failed startup. Duplicate
  endpoint/broker/controller-epoch registration and serial exhaustion fail.
- Each publication gets a monotonically issued 32-bit namespace serial in the
  upper Qid path bits. Serial exhaustion never wraps. Attach captures immutable
  endpoint in Chan.aux, not a registry-slot lookup on each operation; a later
  replacement cannot redirect an old FD/status/directory view.
- Reserve one data opening BEFORE blocking. Only successful notified broker open
  commits handle/exact Chan identity and then COPEN. Notes clear the reservation;
  the blocking adapter cleans its own request/owner leases. No error-throwing
  operation or user copy occurs between successful open and final publication.
- Unopened clones own no broker handle and can close harmlessly. Opened sources
  or destinations are rejected before native devclone can panic. Supplied clones
  must match source endpoint/Qid; walk counts/names are bounded/validated. Native
  dup shares the SAME refcounted Chan; only last framework close calls the Dev.
- Data reads/writes/clunk require the exact owner Chan, not merely a copied
  COPEN flag or alias path. Clunk marks closed once and transfers close to the
  worker without sleeping or hardware access. After a claim, a fresh epoch and
  distinct endpoint are mandatory; published objects are never reinitialized.
- Directory stat explicitly preserves 0500 instead of generic helper fallback.
  Status offset is range-checked before narrowing; large offsets return EOF.
  Stat/copy lengths are checked. Block reads clamp to 4 KiB BEFORE allocb;
  standard block-write cleanup releases a denied/errored write's block.
- Withdraw requires worker Done, actual client/operation drainage, stopped
  broker and no opening/live data Chan; removes only the exact pointer. It does
  NOT free the endpoint/IO/worker. Old directory/status Chans still reference
  boot-pinned CPU state and remain readable to eve after withdrawal.

Source inspection: [Dev walk/clone/stat helpers](https://github.com/9front/9front/blob/front/sys/src/9/port/dev.c),
[Chan reference and close paths](https://github.com/9front/9front/blob/front/sys/src/9/port/chan.c),
and the running stock `devdup.c`. These observations support the ownership
contract, not proof of live native scheduling, notes or permission behavior.

The host fixture executes ACTUAL Dev and blocking-adapter sources with real
broker/controller/pump/DMA/MMIO logic, but simulated native Chan/error/copy/sleep
primitives and bus. It covers permissions/delegated FDs, all open modes, alias
and clone safety, last-ref clunk, real threaded blocked opening/note, user-copy
faults, stopped old views/new replacement, four slots and final/exhausted Qids.
This is not genuine native kernel execution or actual 9P directory encoding.
Normal/ASan/UBSan/leak/analyzer/ThreadSanitizer results are in STATUS.

```rc
mk test vconsole-dev-check.6 vconsole-owner-pc-check.6
```

After successful test-kernel boot and verified native virtio discovery, the raw
agent can use the verified interface (do not run a second agent if owned):

```rc
cat '#Y0/status'
/bin/aux/qga9 -t stream -d '#Y0/data'
```

Startup/recovery/configuration integration and the complete separate kernel build
are now supplied below. Hardware and migration verification remain outstanding.

### Opt-in bounded PC64 startup and recovery (2026-10-13)

`vconsole-boot-pc.c` connects the actual owner/controller/worker/broker/Dev helpers.
The native Dev.init spawns a process-context supervisor only with `qgavirtio=1`.
It requires exactly one supported PCI console controller (at most 64 matching
vendor candidates inspected); zero or multiple controllers leave all unclaimed.
Only the exact named nonconsole QGA port can be opened.

Four complete context bundles are statically pinned for the whole boot. The full
pool plus 64 KiB conservative allocator slack is subtracted before the existing
16 MiB acquisition budget; current-context accounting is deliberately duplicated.
No IRQ, worker, broker, controller or Dev object is reinitialized after use.
Normal retired-context recovery consumes a fresh bundle; four epochs total,
pool exhaustion requires reboot. Startup failures do not silently respawn.

Acquisition is ordered: claim/map, controller init/bind, budgeted prepare,
fresh wake/broker/worker/Dev initialization, IRQ attempt and checked BME/start,
then one worker launch. Publication waits for actual Running admission, not
Launched. Startup and draining each allow at most 100 supervisor polls at 100 ms;
these are retry limits, not a hard native scheduling/resource-wait guarantee.
INTx stays masked, using worker polling until checked IRQ activation exists.

On terminal broker/worker state, quiesce closes admission/IRQ, masks INTx and
clears BME immediately without treating that as reset proof. Actual worker
completion and live-Chan/client drainage precede namespace withdrawal; only then
does owner stop confirm device reset, release DMA/maps and restore PCI command.
Timeout or ambiguous reset/write retains objects/claims and fails stopped; no
forced lease release, stale-cookie reuse, replay or delivery guarantee is added.
Pinned directory/status views survive withdrawal. The kernel does not restart a
userspace qga9 daemon: automatic daemon restart is a separate integration gate.

Build from a native checkout, without replacing the stock kernel/config:

```rc
rc kernel/build-pc64.rc
```

Outputs `/sys/src/9/pc64/9pc64qga` from a separately derived `pc64qga` configuration.
`pc64.mk` supplies explicit native/helper rules. Hyphenated helper names must
each be the first word of their own misc line: the inspected native mkdevlist
otherwise filters them as option-like dependency words. No stock mkfile or
stock `pc64` config is replaced; common native build objects may be regenerated.

The operator explicitly allowed testing on the existing disposable VM instead
of requiring a clone. The complete kernel linked cleanly (5515801 file bytes),
all 16 native suites passed, and stock boot image/plan9.ini backups were compared
byte-for-byte before installing `/amd64/9pc64qga`. With only ISA present, the
expected live result is a no-device message without a PCI claim. After setting
the kernel configuration environment and invoking synchronized `fshalt -r`, the
filesystem reported halt completion but the VM did not return on its network
port during initial checks. The operator reported that it was waiting for the
normal bootargs acknowledgement; after acknowledgement, reconnection succeeded.
The live kernel lists `#Y vconsole` and its exact log is `vconsole: expected one
console PCI device, found 0; no device claimed`. No console PCI device is present;
`#Y0/status` correctly fails absent rather than publishing a fake endpoint. Stock
boot files still compare equal, and the installed ISA daemon restarted normally.
See STATUS for precise evidence. No virtio DMA was live-tested.

The inspected native bootrc prompts unless `nobootprompt` supplies the complete
bootargs value, not a boolean. For a subsequent test-only kexec, it can be set in
`#ec` to the already verified local bootargs before synchronized reboot; this
does not modify on-disk plan9.ini. No prompt-policy change was made in this batch.
The exiting no-device supervisor shows Broken because native linkproc invokes
`pexit("kproc exiting", 0)` on normal callback return, preserving it for inspection.
This observed state is not a vconsole panic or a claim of worker/DMA execution.

## Driver shape

The native `devvconsole` interface above supplies a single-owner byte stream
and read-only status. The registered namespace is `#Y[0..3]`, not a Linux
`/dev/virtio-ports` path. Reuse checked PCI mapping/barrier
primitives, but do not copy unchecked DMA-ring handling from another driver.

Target modern virtio 1.x PCI capabilities first: vendor `0x1af4`, device type 3;
recognize the modern `0x1043` and transitional `0x1003` IDs with modern capability
registers. Transitional legacy I/O support is a separate compatibility task,
not required to call a modern-capability implementation real virtio.

1. Validate PCI capability BAR/index/offset/length arithmetic and map common,
   notify, ISR and device config regions. Reset; set ACKNOWLEDGE/DRIVER; negotiate
   `VIRTIO_F_VERSION_1` and `VIRTIO_CONSOLE_F_MULTIPORT`; verify FEATURES_OK.
   Do not negotiate unsupported packed-ring/indirect/event-index features.
2. Set up bounded split queues with correct physical/DMA addresses, descriptor
   direction flags, alignment and notify-offset multiplication. Validate queue
   sizes before allocation. Port 0 uses queues 0/1, control RX/TX use 2/3, port N
   greater than zero uses `2*N+2` / `2*N+3`. Memory and port limits are explicit.
3. Post control RX buffers, set DRIVER_OK and send DEVICE_READY. Process PORT_ADD,
   PORT_READY, PORT_NAME, PORT_OPEN and PORT_REMOVE in a serialized worker.
   Select the port by exact UTF-8 name `org.qemu.guest_agent.0`, never by guessing
   port 0 or treating a console port as QGA. Reject oversized IDs/names and
   duplicate/conflicting lifecycle events safely.
4. Expose raw RX/TX with no line discipline, baud settings or flow-control byte
   consumption. RX buffers must be device-writable; TX/control-TX readable.
   Single-reader/open ownership prevents two qga9 processes consuming a channel.
   Backend close preserves guest ownership; positive TX waits for reconnect.
   Actual removal/fault retires ownership and wakes blocked callers.
5. IRQ paths only acknowledge/queue/wake; a worker owns lifecycle and buffer
   recycling. Specify lock ordering and barriers. Check every used ID/length
   against its posted descriptor, and never reuse/free a DMA buffer until reset
   completion/device quiescence. Reset/unplug cancels pending I/O and drains worker
   references; late completions must not reach a new guest-owner generation.
   Host socket reconnect alone creates no new guest-owner generation.

The control message header is 8 bytes (LE32 port ID, LE16 event, LE16 value).
The specification defines PORT_NAME as UTF-8 without a NUL terminator, while
the inspected QEMU implementation sends `strlen(name)+1`. The decoder accepts
both representations, allowing exactly one final NUL but no embedded/additional
NUL. Console size and
emergency-write features are unnecessary for a QGA-only driver. Console-port
events still need spec-compliant handling or a documented restricted device
configuration; they must not silently open a QGA stream on the wrong port.

The inspected native PCI helper uses modulo for a capability BAR index and
unchecked extent arithmetic. That pattern is not copied by this core. Before
reuse, the adapter must validate the actual mapping/allocation primitives too;
pure-core checks do not harden another driver's mapping path. This is a scoped
source observation, not an assigned CVE or an upstream patch.

## Next implementation steps

1. Add checked native IRQ routing/registration before enabling INTx or MSI-X.
   A registration attempt or IRQ table row is not proof of usable delivery.
2. Extend real reset/error/hot-unplug, allocator/note/copy-fault and competing
   driver testing while preserving pinned targets and DMA-drain/reset contracts.
3. Validate terminal-fault recovery on hardware and define a safe QGA restart
   policy after actual guest owner retirement. Backend reconnect is not retirement.
4. Complete command-by-command host conformance and adversarial timing tests.
   Preserve stock kernel/ISA rollback and report each untested gate honestly.

## Migration/test gates

Follow [INSTALL](INSTALL.md). Preserve the stock image/config, prepare the
separate tested kernel and use PVE's single normal virtio agent channel.
Verify actual PCI capabilities, exact named port, binary NUL/ff, exclusive Chan
ownership, blocking/backpressure and repeated backend connects/disconnects.
Run QGA host scripts via PVE, not a competing raw socket client, then verify
automatic cold startup and rollback. See [STATUS](STATUS.md) for passed gates;
fault/reset/unplug and checked interrupts remain incomplete.

Host operations are performed by the operator, who supplies PVE results.
Guest-side builds/pipe fixtures alone are never evidence of host compatibility.
