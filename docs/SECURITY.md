# Virtio-serial security review

## Current scope and refactor review — 2026-10-13

No new unresolved finding was identified in the repository split across the
five required areas: memory safety, thread safety, race freedom, CVE-style
trust-boundary patterns and access control. This is a scoped review, not a
whole-kernel or hypervisor certification. The split changes build/document
ownership only: kernel and fixture C sources are byte-identical, fixed buffers,
locks, generation tokens, DMA retention and eve-only permissions are unchanged.
Build/test targets have no runtime cross-repository includes or sibling writes.
No credentials, remote publication, guest operation or extra export is added.

A prior medium availability/compatibility issue was fixed before activation:
treating each host socket close as guest owner retirement exhausted bounded
reset contexts and disrupted persistent file/job state. Host PORT_OPEN now
changes only backend availability. Received bytes and guest ownership survive;
positive TX waits without retagging posted leases. Guest close/removal/fault
still requires reference drainage and confirmed reset. Tests cover repeated
host reconnect with outstanding binary requests and unchanged owner epochs.

One worker owns ring/control state; locks guard broker/Dev/controller/IRQ shared
state. No borrowed user pointer is queued. The native child-identity access
test denied both data and status to none, without changing the parent.
Boot installation verifies exact stock backups before configuration changes;
FAT replacement is not atomic, so console access and rollback remain necessary.

Residuals: masked INTx/polling does not verify interrupt routing; live reset,
hot-unplug and adversarial DMA/timing/native fault injection remain incomplete.
Native intrenable can panic on allocation, kproc waits can be indefinite and
boot-pinned contexts remain until reboot. Host trust and eve permissions are
required; a malicious guest can replace the driver or agent. This does not
harden QEMU/PVE against compromised guest responses.

## Historical layer reviews

These reviews are dated implementation evidence from the combined repository.
Their “ISA unchanged/no hardware” statements describe those batches; current
hardware, access and reconnect evidence is in [STATUS](STATUS.md). Host socket
disconnect no longer invalidates the guest generation or clears its RX bytes.
Only actual guest ownership retirement/removal/fault follows the reset rules.

## Virtio foundation review

The new `kernel/vconsole.[ch]`, fixtures and build rules were reviewed across
all five areas: memory safety, thread safety, race-free execution, CVE-style
input/overflow/lifetime patterns and insecure access paths. No unresolved finding
was identified in this scoped pure-core review; it is not an independent audit
or evidence that an unimplemented DMA/IRQ driver is safe.

- The threat map is untrusted PCI capability/BAR metadata, used-ring entries and
  control messages entering fixed-capacity decoders. Indices/lengths/UTF-8 and
  extent arithmetic precede access. PCI lists are cycle-checked. Invalid batches
  do not partially publish output or release posted DMA buffer leases. Input
  IDs cannot choose a pointer, filesystem path, console or another stream owner.
- State is caller-owned and allocated outside the core. It retains no borrowed
  buffers and performs no resource acquisition, free, MMIO or DMA. Each object
  must have one serialized worker; all snapshots must be immutable CPU memory
  for the entire call. There is no reentrant/shared-instance safety claim, and
  no locks, ISR state mutations or barriers hidden inside these functions.
- Queue leases and nonwrapping epochs are separate from port ownership and
  nonwrapping generations. Disconnect never authorizes buffer reuse. Old data
  cookies and stale close tokens cannot select a new owner. Duplicate/conflicting
  names fault the controller; only the exact nonconsole QGA name is claimable.
- Reset requires a caller-supplied quiescence assertion; the future adapter must
  actually prove reset completion and drain interrupt/worker/Chan references.
  Passing `1` or advancing an epoch cannot stop late DMA. Snapshot copying and
  acquire/release barriers are also adapter obligations, not solved by this core.
- Strict frame directions, complete UTF-8 validation and both spec/QEMU name
  terminator representations prevent truncation/name-confusion paths. There is
  no shell, format-string execution, credential export or new guest privilege.
  The inspected native helper's BAR-modulo/unchecked-extent pattern was not
  copied. The later mapping-primitives review is recorded below; actual hardware
  execution and full driver integration remain separate gates.

ASan/UBSan/leak checks, GCC analyzer, native fixtures and actual PC64 kernel-header
compilation passed. The pure core does not implement mappings, negotiation or
reset polling; the later initialization layer below adds those operations but
does not hardware-test them. DMA memory ownership/fences, interrupts, unplug and
stream lifetime still require driver integration. This component is not loaded; the installed
`0.5.1-dev` daemon and working ISA transport are unchanged. No hypervisor trust
or sandbox guarantee follows from these tests. See VIRTIO for integration gates.

## PCI/MMIO initialization review

The mapping/MMIO/PC-backend/fixture paths were reviewed across all five areas:
memory safety, thread safety, race-free execution, CVE-style input/lifetime/
overflow patterns and insecure access. No unresolved finding was identified in
this changed slice after hardening. It is not a full kernel/hypervisor audit.

- PCI input is snapshotted into 256 bounded bytes; register regions remain
  within validated assigned BAR extents. Mapping lengths are capped and failed
  acquisition unwinds only acquired references. The PC backend independently
  checks mapping identity, access width/alignment/extent and narrow write values.
  Queue-info padding is zeroed; invalid results are never partially committed.
- Native PC64 retains mappings in `vunmap`. To avoid accumulating fresh
  high-physical virtual ranges on retry, the backend restricts every extent to
  the deterministic direct VMAP window. Reference release does not reclaim
  native mappings/page tables; hotplug/platform mapping cleanup remains limited.
- One process-context owner mutates initialization and feature/queue selectors.
  Every register access has before/after barriers. No callbacks retain borrowed
  output; ops/aux must outlive state and must not reenter or throw native errors.
  This is not concurrent IRQ/worker/Chan safety. The later IRQ mailbox is separate
  and never reads/mutates this worker-owned state; controller/Chan joining is
  still absent.
- Reset completion is established by status-zero readback, not a successful
  write or FAILED flag. Polls/config retries are bounded. Failures retain
  mappings, disallow normal teardown and preserve epoch; confirmed reset plus
  caller-drained references is required for release. The drained flag is an
  assertion, not an implemented reference-counting mechanism or hardware proof.
- Only required VERSION_1/MULTIPORT features are accepted, and FEATURES_OK,
  stable port count, queue size/disabled state and notify extent are checked.
  All-ones status gets no best-effort FAILED write. Initialization supplies no
  DRIVER_OK, DMA allocation, PCI command write, IRQ registration, stream endpoint,
  host credential/export or new guest authorization path. Checking disabled
  MSI/MSI-X does not itself prove exclusive PCI ownership; the future driver
  owner must establish that before calling the adapter on hardware. The later
  `vcmmenable` helper configures/enables owned queues; its DMA review is below.

Native header compilation, simulated native tests, host PC-primitive mocks,
ASan/UBSan/leak checks and analyzer passed. Residual gaps: actual BAR mappings,
device timing/error/reset/unplug behavior, platform cache/DMA ordering, IRQ
handoff and channel/reference lifetime are not hardware-tested. No new kernel
or daemon binary is installed; the tested ISA service is unchanged. See VIRTIO.

## Coherent DMA queue review

No unresolved finding was identified after hardening this changed DMA/queue-bind/
native-allocator slice. Review covered all five required areas: memory safety,
thread safety, race-free execution, CVE-style overflow/lifetime/input patterns
and insecure access paths. This is a scoped source review, not a kernel audit
or proof that live DMA/IRQ/Chan integration is correct.

- Memory: layout size/capacity checks precede every offset calculation. Complete
  CPU/physical extents and exact allocation lengths are validated before zeroing;
  ring extents are disjoint and aligned. Native physical translation accepts only
  direct KZERO RAM with a checked linear extent. Failed acquisition releases once;
  attempted queue binding marks exposure before any possibly applied MMIO write.
  Bounded completion scratch is object-owned, avoiding large kernel stack arrays.
- Threads: one process-context worker owns each queue and its controller selectors.
  Ops/aux/MMIO lifetimes must cover every queue reference, callbacks must return
  normally, and no concurrent or reentrant IRQ/Chan mutation is permitted. There
  are no mutable production globals or implicit locks; the test shim is serial.
- Races: descriptor/data/avail entries precede an ordered aligned idx publication;
  used.idx is read once before an acquire barrier and immutable snapshot copying.
  A completed RX buffer stays held until bounded copying consumes its receipt.
  Nonwrapping receipt serials reject old receipts even with repeated port cookies.
  Reset invalidates the epoch; free requires a newer confirmed-reset epoch plus
  drained references. The assertion does not implement future IRQ/Chan draining.
- CVE patterns: malformed lengths/IDs, duplicate completions, jumped indices and
  bad readback are bounded whole-batch faults, with no partial lease release.
  Physical addresses are not truncated to 32 bits; all halves are checked.
  Invalid owner lengths/layout and used-length conversion fail before access.
  Only initialized descriptor lengths are exposed, and RX copies no tail bytes.
- Access: only a caller-owned allocation can reach the binding helper. This code
  itself does not claim a PCI device, enable bus master/DRIVER_OK, notify it,
  expose a stream, export credentials or authorize new QGA commands. Later MMIO
  helpers supply checked DRIVER_OK/notifications, not PCI ownership. Exclusive PCI
  ownership and capability/port access remain future controller/Dev obligations.

Native kernel-header compilation, native/host simulated queues and allocator
mocks passed, as did ASan/UBSan/leaks and analyzer. Remaining gaps: actual DMA
coherence/timing and device compliance, live interrupts/config health monitoring,
live controller acquisition/budget enforcement and worker scheduling, channel cancellation and
reference draining, hot-unplug and IOMMU/noncoherent support. A malicious device
or hypervisor can keep writing after a reported completion/reset; validation is
not protection against that authority. No live controller executes this code,
and the installed ISA daemon remains unchanged. See VIRTIO for migration gates.

## IRQ handoff and runtime-MMIO review

No unresolved finding was identified in this changed mailbox/native-wake/
startup-health-notification slice under its documented lifetime and ownership
contracts. Review covered memory safety, thread safety, races, CVE-style
input/lifetime/overflow patterns and insecure access paths. This is not a
full driver/kernel audit or proof of native IRQ/hardware correctness.

- Memory: one bounded ISR-byte read uses an immutable pinned pointer, not
  worker-owned MMIO metadata. Closed mailboxes reject further mapping reads.
  Wake callbacks retain counted references with an explicit overflow guard.
  Exhaustion latches a fault without an untracked callback; bounded worker waits
  must observe it even if earlier wake callbacks already signaled.
  Closed/drained permits mapping release only after hardware reset and worker
  join; it never proves pre-dispatched callers are gone. Mailbox/locks/aux remain
  boot-pinned unless dispatch, predicate and caller drainage is separately proven.
  Configured-queue caches are bounded to 130 entries and cleared on confirmed
  reset. Stale/forged kick metadata fails before device I/O.
- Threads: all mutable mailbox fields have one IRQ-safe lock; ops/aux/epoch
  remain immutable after fresh initialization. Exactly one worker owns MMIO,
  queues and port lifecycle. IRQs do not touch those objects. Native wake callers
  have a separate serialization lock. Read/fence callbacks must not reenter;
  callbacks return normally without throwing errors or retaining borrowed output.
- Races: pending bits plus the native rendezvous readiness predicate close the
  wake-before-sleep gap. IRQ producers release the mailbox lock before wakeup,
  preventing the rendezvous/mailbox inversion. Close cannot drain a blocked
  wake callback. Never reinitialize a published mailbox or assume `intrdisable`
  joins late dispatch. Future kproc shutdown/notes still require an error frame,
  worker join and independent channel-reference draining.
- CVE patterns: shared/spurious IRQs and reserved ISR bits do not fabricate
  queue work; failed/oversized/all-ones reads fault. DRIVER_OK requires every
  queue configured in the current epoch and status readback. Health checks are
  bounded and reject changed/unstable port layout, NEEDS_RESET or unexpected
  reset. Notifications use checked 16-bit width/ID/extent, with status checks
  before/after. Ambiguous failed writes retain DMA/mappings, never free them.
- Access: these helpers do not register an IRQ, claim PCI ownership, enable bus
  mastering, create a worker/Dev, export files/credentials or enable a new QGA
  command. Existing guest and host access policy is unchanged. Future controller
  startup must prove exclusive ownership, RX readiness and memory budgets; the
  native stream endpoint must enforce exact-port and single-owner access.

Portable/native simulations, actual kernel-header compilation, Linux pthread
interleavings, ASan/UBSan/leaks, ThreadSanitizer and GCC analyzer passed. Native
Lock/Rendez execution, IRQ registration/teardown, live DMA/reset/error timing,
live control-message/reply backpressure, console data handling, worker/Chan joining
and hardware migration remain unverified. No live controller or test kernel was
executed; the installed ISA daemon is byte-for-byte unchanged. See VIRTIO/STATUS.

## Control-pump and footprint review

No unresolved finding was identified in the changed pump/planner/fixtures after
hardening. All five required areas were reviewed: memory safety, thread safety,
race-free ownership, CVE-style input/lifetime/overflow patterns and insecure
access. This is scoped to worker-callable control-plane helpers, not a completed
native controller/stream or an independent kernel/hypervisor audit.

- Memory: queue count/ID/size/epoch, capacities and total footprint are checked
  before planning output is committed. A fixed 16 MiB cap prevents policy
  expansion through the caller's limit. Planner accounting includes all arenas
  plus the planned queue/pump/MMIO objects, not allocator/platform/Dev overhead.
  Init rejects foreign/used/mismatched queue sets and rechecks footprint; actual
  allocation must still follow the plan. Control messages and both receipt
  snapshots use object-owned bounded storage, avoiding large kernel stack arrays.
  Full reply FIFO retains the next RX receipt/payload until reply space exists.
- Threads: exactly one process-context owner controls pump, ports, queues and
  MMIO. No locks, worker spawn or mutable production globals are hidden in this
  slice. IRQs use the separate mailbox; future Dev/Chan callers must enqueue
  requests rather than directly call these functions concurrently. Queue/MMIO/
  ops/aux and pump references must survive every borrowed call/receipt.
- Races: incoming controls follow used-ring order, not descriptor ID order.
  RX copying/receipt consumption precedes repost. Reply FIFO preserves ordering;
  unsent replies carry port incarnation and guest ownership generation. Removed,
  released or disconnected generations cannot reopen/close a new owner. Posted
  replies cannot be cancelled, and late TX completion only reaps its receipt.
  Claim becomes control-plane alive only after OPEN publication plus successful
  notification; duplicate host OPEN does not silently revoke an existing owner.
  Logical stop/fault never frees DMA or proves IRQ/worker/Chan reference drainage.
- CVE patterns: wrong-direction/truncated frames, invalid UTF-8/IDs, duplicate
  add/name conflicts, forbidden console/QGA aliasing, malformed whole ring
  snapshots and generation exhaustion fail-stop. Each iteration has explicit
  bounded work; TX saturation is not reported as runnable CPU work. Ambiguously
  applied failed I/O retains exposed allocations, with no best-effort retry that
  could duplicate a privileged OPEN. Initial data cookies are zero, never an
  accidentally matching port generation; data routing must implement separate
  drain/repost/filter semantics before payload delivery.
- Access: only exact nonconsole QGA name can be claimed, and only one owner.
  A full reply FIFO cannot partially claim/release a stream or overwrite a token
  output. Console OPEN acknowledgement does not grant a QGA owner. No PCI claim,
  bus-master activation, IRQ registration, kproc/Dev, new QGA command, host file
  export or credential handling is supplied; existing access policy is unchanged.

Native and host simulated pump/DMA/MMIO tests, actual kernel-header compilation,
ASan/UBSan/leaks and GCC analyzer passed. This pump has no concurrent mutable
state under its single-owner contract; the earlier ThreadSanitizer result is
for the separate IRQ mailbox, not a running native pump. Remaining gaps: actual
PCI/controller acquisition/unwind, native kproc notes/termination/join, hardware
DMA/config/disconnect timing, automatic reset/reopen and blocking Dev/Chan/
reference lifetime. The subsequent stream slice supplies worker-only payload/
backpressure and console discard. No hardware or installed
daemon was changed. See VIRTIO/STATUS.

## Nonblocking stream review

No unresolved finding was identified in the changed stream/deferred-start/
shared-fixture slice after hardening. All five areas were assessed: memory
safety, thread safety, race-free execution, CVE-style input/lifetime/overflow
patterns and insecure access. This is a scoped source/simulation review, not
proof of a live kernel driver, blocking Dev/Chan or automatic reconnect manager.

- Memory: the whole stream object is reserved before queue acquisition within
  the existing hard footprint limit. RX FIFO/snapshots/receipt/sink scratch are
  object-owned, not large kernel-stack allocations. Every read/write length is
  bounded; a whole RX payload must fit before receipt consumption/repost. TX
  copies to owned DMA storage and retains no caller pointer. Taken FIFO bytes,
  temporary payloads and closed/faulted unread data are cleared. Token padding
  is initialized, avoiding stack-padding propagation in the control claim path.
- Threads: one serialized worker owns stream/pump/queues/MMIO and every API
  call, including status/alive. Exactly one data adapter attaches to a fresh
  pump. IRQs use the separate mailbox only. Future Chan clients must exchange
  owned/pinned requests with that worker; direct concurrent calls are forbidden.
  No mutable production globals, hidden worker threads or new locks are added.
- Races: owner-tagged QGA RX is published before guest OPEN, never by changing
  a posted descriptor's old cookie. Tokens include boot-unique controller identity,
  reset epoch and port generation; the future owner must assign identities and
  retain objects/references correctly. Data follows used-ring order. Whole
  queue snapshots precede the action budget, preserving shared-IRQ work even
  with one action. RX/TX and console sink actions alternate; sink ports/slots
  rotate. Full RX FIFO/TX ring requires external progress, not a CPU busy-loop.
- CVE patterns: malformed used IDs/lengths, duplicate receipts, impossible cookie
  or epoch, ambiguous failed notification and reset fail-stop across stream and
  pump. Close/disconnect/remove clears queued bytes but does not cancel leased
  DMA or authorize freeing it. The prototype permits one published QGA owner
  per controller epoch; reopen returns Reset until full hardware reset plus
  joined/drained references and fresh state. A retired console cannot become
  QGA on the same used queue without that reset. Late receipts only discard/reap
  old leases, never retag/repost them for a new owner.
- Access: exact nonconsole QGA selection and single owner remain mandatory.
  Wrong controller/epoch/port-generation tokens cannot read/write/close the live
  owner. Identity is an internal anti-ABA identifier, not a secret capability or
  substitute for future native file permissions. Console traffic is discarded
  separately; unused nonconsole ports have no guest OPEN/data buffers. No new
  QGA command, host export/credential path, PCI/IRQ activation or Dev is added.

Native/host binary/FIFO/backpressure/token/late-completion/fault/wrap fixtures,
actual PC64 kernel-header compilation, ASan/UBSan/leaks and GCC analyzer passed.
No concurrent Chan execution is claimed; the stream's correctness contract
remains single-owner. The later controller gate has its own ThreadSanitizer
evidence below. Remaining gaps: live DMA/IRQ timing,
native PCI/IRQ ownership and kproc/note-safe scheduling/last-access handoff,
automatic recovery scheduling, blocking Dev/Chan requests/cancellation/
permission/reference lifetime, hot-unplug and hardware migration. No installed
binary/kernel/channel was changed; the ISA service remains byte-identical.

## Controller lifecycle review (2026-10-12 continuation)

Changed scope: controller acquisition/unwind, lifetime admission/operation guard,
reset/reopen, native PC64 lock/allocation callbacks and their fixtures. No
unresolved finding was identified in this scope under its documented immutable
callback, exclusive PCI/MMIO ownership and boot-pinned reference contracts. All
five required areas were checked; this is not an independent/hypervisor audit.

- Memory: bounded count/capacity/footprint is checked before allocation; complete
  controller/session/queue metadata is counted alongside DMA. Metadata alignment
  and CPU-address extent are checked before access. Partial acquisition is
  unwound exactly once; any bind attempt requires a newer observed reset before
  freeing. Failed reset retains memory/mappings, never inventing quiescence.
  Reopened sessions keep the original nonwrapping MMIO epoch and reject old tokens.
- Thread safety: one short IRQ-safe gate protects admission, exact lease slots,
  serials and the operation guard. Runtime/session/MMIO state is accessed only
  with that guard. No gate lock is held across allocation/MMIO/IRQ/wake callbacks;
  IRQ mutates only its separately locked mailbox, not rings/controller state.
  Native lock callbacks compile; pthread tests do not prove native IRQ ordering.
- Race freedom: reset closes admission before waiting for ALL worker/client
  leases, in-flight operations and counted IRQ wake callbacks. No external
  drained assertion can bypass those checks. Concurrent stop wins startup
  publication; stale/double puts cannot release replacement leases. Fault closure
  occurs before the operation guard is surrendered so it cannot target a newly
  reopened epoch. Deterministic blocked-worker/DRIVER_OK/waker tests and 40000
  concurrent reference/worker cycles passed controller ThreadSanitizer.
- CVE patterns: no unchecked input-sized allocation, unbounded index, live ring
  retagging, format-string evaluation, deserialization, shell execution, key
  transfer or dependency addition. Latched IRQ faults are checked before ring
  processing even when common MMIO status remains healthy. Failed hardware
  operations fail closed and retain resource ownership.
- Access: only the exact worker-role identity/epoch/slot/serial lease can mutate
  stream state; client leases provide lifetime, not read/write authority. Native
  OS file permissions and exclusive PCI ownership remain mandatory future
  integration boundaries. Manager APIs neither claim a PCI device nor enable
  bus mastering/register IRQs/create kprocs/Dev files. No QGA command or host
  export/credential path is added.

During hardening, fault closure was moved before guard release to prevent a late
stop targeting a new epoch, reverse unwind now advances its acquired count per
successful free, and IRQ-fault propagation received a healthy-status regression.
Native fixture symbol collisions were corrected without changing production
behavior. Final normal/ASan/UBSan/leak/analyzer and native tests pass; controller
ThreadSanitizer passes. These are simulated DMA/MMIO plus real pthread gates,
not live kernel execution or hardware evidence.

Residual gaps: native PCI/INTx/bus-master ownership, kproc notes and cooperative
last-access cleanup, blocking Dev/Chan permissions/request cancellation, hardware
reset/unplug/order timing and clone migration. Missing caller puts deliberately
retain memory rather than force reclamation. Controller/locks/aux and old IRQ
mailboxes stay boot-pinned: a late native IRQ dispatch can still call the closed
mailbox. Future native scheduling must separately bound retained IRQ/Dev memory;
it is excluded from the manager's 16-MiB allocation budget. This batch creates no
such native registry objects and performs no unbounded automatic retry loop.

## Worker and native PCI ownership review (2026-10-12 continuation)

Changed scope: one-shot worker lifetime/scheduling, native note-catching wait/
spawn/yield adapters, PCI reservation/command/registration helpers and fixtures.
No unresolved code finding was identified within the documented immutable
callout, single-supervisor/runner and boot-pinned-object contracts after
hardening. All five areas were assessed; this is not a hardware or hypervisor audit.

- Memory: worker lease is acquired before spawn, with exact attached-IRQ
  validation. Spawn errors guarantee no runner; sleep predicates retain the
  lease until they return. Stop callouts also borrow it. Only the last actual
  controller/predicate/stop access releases it once. PCI failure paths retain
  claim/maps until confirmed reset, and failed saved-command restoration after
  detach can retry without double reset/free. Required owner prepare reserves
  standard native worker/IRQ/gate/owner/registry footprint before DMA acquisition.
- Thread: short locks protect worker phase/stop/reference handoff and the bounded
  registry; no allocation, MMIO, sleep, wake or registration under those locks.
  One runner mutates rings through the controller guard, one supervisor changes
  each PCI owner. IRQ touches only its separate mailbox. Worker, native aux and
  old IRQ contexts remain boot-pinned after Done; Done is not an OS process reap.
- Race: stop cannot falsely join a blocked spawn, sleep predicate or external
  stop callback. Finishing prevents new stop callouts; the last tracked stopper
  can finalize release. Old-worker stop cannot close a new controller epoch.
  Concurrent TBDF claims have one registry winner; same-object duplicate claim
  is rejected before memset can erase mappings. Real pthread tests and final
  worker/registry ThreadSanitizer runs passed, not native IRQ timing tests.
- CVE patterns: no unchecked input-sized allocation, raw ring mutation, new
  parser/shell path or secret handling. Capability offsets/cycles/power/MSI modes
  are validated before native changes. PCI command writes are 16-bit, checked
  and cache-consistent, not dwords affecting W1C status. Native MSI-first routing
  was discovered during inspection, so all MSI capabilities are rejected by
  the INTx prototype. Unchecked registration keeps INTx MASKED; worker polling
  prevents silent registration failure becoming an unhandled interrupt storm.
- Access: registry/lease identities are not authentication. Registry excludes
  duplicates only within this driver; native config must exclude foreign drivers
  because no kernel-global PCI claim primitive is used. Existing BME/active
  MSI/MSI-X are rejected; no resetting an unrelated active driver is authorized.
  Native helper calls exist in source but no driver/Dev, bus master, IRQ or kproc
  was activated. No new QGA command, host export or credential transfer occurred.

Hardening also rejects null native process context before error frames, bound
the manager BEFORE prepare (failed acquisition may retain
DMA), checked independently registered wake drainage, distinguished post-detach
restore retries, preserved exact IRQ-event consumption and corrected native
fixture libc symbol collisions. Final host normal/ASan/UBSan/leak/analyzer and
worker/registry ThreadSanitizer checks pass; stock-native fixture/kernel-header
checks are recorded in STATUS. PCI tests use fake native primitives and ordered
controller stubs; they do not prove the combined PCI/DMA/kproc path.

Residual/native-platform risks: kproc creation may wait indefinitely for a process
slot; native intrenable returns no success and may panic on its own allocation
failure. Keeping INTx masked addresses unchecked registration availability, not
native allocation panic or all kernel IRQ bugs. Checked IRQ activation, actual
note/error and resource-exhaustion injection, native scheduling/order/reset/unplug
timing, blocking Dev/Chan permissions/cancellation and clone migration remain
unverified. Boot-pinned old contexts require bounded retention/accounting before
automatic reconnect; only one standard context is in the new startup allowance.
These helpers neither allocate an unbounded epoch pool nor create an automatic
retry loop. Forced destruction/reinitialization of published contexts is forbidden.

## Request broker and blocking adapter review

The scoped five-area review covers `vconsole-io`, native blocking adapters,
worker service/finish hooks, exact worker-reference admission and the updated
startup allowance. No unresolved changed-scope finding remains under the stated
kernel-caller contracts; this is a local review, not an independent audit.

- Memory: eight fixed 4-KiB buffers, checked lengths/slots and no retained user
  pointers. Reserved/Taking callers exclusively own payloads outside locks;
  Busy ownership transfers to the sole worker. Abandon/reap return exact client
  leases once, only after LAST copy/predicate/runtime access. Native stack use
  is small. Exposed DMA still requires observed reset and actual lease drainage.
- Thread safety: broker metadata has one IRQ-safe lock; Busy private bytes have
  one worker, Reserved/Taking bytes one caller. Controller locks/callouts/wakes
  never nest under the broker lock. Native per-slot driver wakes are serialized;
  sleep predicates can check broker state without Rendez lock inversion.
- Races: nonwrapping request serials/owner generations and captured controller
  epochs reject stale operations. Temporary new-epoch leases acquired by an old
  broker are immediately returned without runtime access. Busy cancellation
  cannot free a worker buffer; shutdown cannot destroy a caller's copying buffer.
  Live handles must clunk before reset can join, even after worker exit.
- CVE patterns: lengths are checked before staging, no request-derived address,
  format string, command or namespace path is used. Native errors are fixed
  text. Static analysis found a test-fixture null/zero memmove path; it was
  guarded along with simulated queue/length validation and final analysis passed.
  Production callbacks do not throw across locks/operation guards.
- Access: only the exact admitted slot-zero worker lease may service requests;
  client leases pin lifetime but grant no MMIO/stream authority. Another open
  owner cannot share an endpoint. The Dev layer below now supplies eve checks
  and Chan ownership as code/host fixtures, not live native activation.

Final host normal/ASan/UBSan/leak/analyzer and broker ThreadSanitizer checks pass.
Native fixtures and actual kernel-header compilation pass. Native blocking,
notes/page faults/user-copy failure, actual Chan/permission behavior, scheduling,
hardware DMA/reset/unplug and recovery context retention remain unverified.
An open handle can delay teardown; no forced lifetime release is supplied.
Request-pool exhaustion fails busy without unbounded allocation. Waiting for
an absent port or host data is intentionally interruptible but has no overall
operation timeout. Cancelled in-flight RX/TX may have irreversible side effects;
never infer host delivery or safely replay mutations from a cancellation.
Boot-pinned broker/native wake contexts are not freed/reused after worker join.
Only one standard fresh context is budgeted; automatic retained-epoch allocation
is still absent. The working ISA transport is unchanged.

## Native Dev/Chan review

All five areas were checked for `devvconsole`, its immutable endpoint/registry
types, native adapter host-test seam and the added startup footprint. No
unresolved changed-scope finding remains under the documented kernel-caller
contracts. This is a local source review with fixtures, not an independent
audit or a proof that the native device/hypervisor is secure.

- Memory: four fixed slots, one fixed endpoint handle per context, no per-open
  endpoint allocation. Lengths/slots/walk vectors/stat buffers and large status
  offsets are checked before use/narrowing. Block reads clamp before allocb.
  Old endpoints/IO/workers remain boot-pinned after withdrawal because CPU-only
  directory/status Chans still reference them. Withdrawal is never permission
  to free/reinitialize those objects or bypass observed DMA reset.
- Thread safety: registry, opening/opened/closed state and owner handle each
  have an explicit lock; published endpoint/Qid/slot fields are immutable.
  No hardware, sleep, user copy, throwing native call or broker call occurs
  under the Dev lock. Existing broker/worker lock and lifetime contracts remain.
- Races: reserve data opening before blocking, clear only that reservation on
  failure, commit exact Chan/handle before COPEN. Only exact last-ref owner clunk
  releases the broker handle, once. Attach captures an endpoint pointer, never
  an indirect current-slot lookup. Unique nonwrapping Qids and exact withdrawal
  prevent an old view/late close from reaching a replacement. Publication now
  requires actual Running entry: Launched can precede successful spawn.
- CVE patterns: malformed/opened/foreign clones fail before native devclone's
  panic path. No host/user-controlled format string, command, pointer, port name
  or filesystem path is evaluated. Status has fixed numeric formatting and
  no payload/credential/address dump. Notes/copy faults use immutable cleanup
  tokens; native errors never escape with an ilock held. Default block-write
  cleanup retains one release path even for denied writes.
- Access: attach/walk/stat/open/read/write/block-read check iseve, including
  already-open delegated FDs. Data is exact ORDWR/0600/exclusive, status OREAD/
  0400, root 0500. Modes with truncation/remove-on-close and all create/remove/
  wstat are denied. Close deliberately needs no eve permission so native clunk
  workers/recipients can clean up the exact Chan. Copied COPEN aliases cannot
  read/write/clunk another Chan's handle. There is no user-facing publish/reset
  control, raw-port selector or PCI/IRQ operation in this Dev.

Final actual-source host tests cover blocked threaded open/note, input/output
copy faults, lease cleanup, modes/permissions/aliases/clones/dup/clunk, four slots,
replacement/stopped views and final/exhausted Qid serials. Memory/undefined/leak
sanitizers, GCC analyzer and final ThreadSanitizer pass. Native kernel-header
compilation passes, but no Dev was linked/registered/executed on the VM.
The host fixtures simulate Chan/error/timer/copy primitives, serial scheduling
and directory encoding; they cannot prove actual page-fault/note/IRQ ordering,
native allocator failure behavior, filesystem export checks or hardware safety.

The subsequent bootstrap batch supplies the bounded retained-context pool,
startup/recovery supervisor, opt-in config and separate full test-kernel build.
Remaining gates: successful live boot, ownership exclusion, real note/FD
and permission execution, hardware DMA/reset/unplug/PCI/timer behavior and
Proxmox virtio interoperability. Stock Chan/block allocator costs and old context
retention are not bounded by the queue allowance alone. Native kproc resource
wait and intrenable failure/panic limitations still apply. A live handle can
delay teardown; it is not forcibly invalidated to reclaim storage. Guest eve
trust is still required, and this does not harden Proxmox against a compromised
guest that replaces the protocol/driver. The installed ISA transport is unchanged.

## PC64 bootstrap/recovery review (2026-10-13)

Scoped review covers memory, thread safety, race freedom, CVE-pattern boundaries
and privileged access for the new supervisor and extracted owner quiesce path.
No unresolved finding identified within the documented ownership/platform
contracts. It is not a whole-kernel security certification.

- Memory: a four-context static pool is never freed/reused after publication;
  the entire retained pool is budgeted before acquisition. Every failed stage
  retains ambiguous hardware state until checked quiescence/reset. A live FD,
  worker or operation cannot be force-drained by the supervisor timeout.
- Thread/race: supervisor alone owns startup/recovery flags and PCI ownership;
  worker/broker/Dev locks protect runtime state. Quiesce closes admission before
  disabling BME; it never mutates rings or frees their storage. Dev withdrawal
  and actual worker/client join precede reset/free. Old late IRQ/Chan callbacks
  have pinned targets, not recycled memory. Native Rendez timer/worker resource
  limitations remain those documented in the adapter review.
- CVE patterns: no input-derived unbounded allocation/spawn/recovery loop,
  arbitrary kernel pointer, shell command or raw PCI control file is exposed.
  Fixed stages short-circuit acquisition failures. FAILED/BME-clear/timeouts are
  never treated as proof of DMA reset, preventing reset-race/UAF patterns.
- Access: `qgavirtio=1` is an administrator-controlled boot configuration switch,
  not a userspace ctl operation. PCI selection rejects multiple controllers;
  Dev eve/exact-channel checks and named-port filtering remain unchanged. No
  local host files, SSH material or credentials are supplied to the guest.

Actual-source transition fixture: 625 assertions; extracted quiesce adds native
owner regression coverage (122 assertions total). Full host/sanitizer/analyzer
checks passed, as did complete native kernel linking and 16 native suites.
Residual gaps: real PCI/DMA/reset/hot-unplug, native Chan/notes, unchecked IRQ
registration, privileged competing drivers and daemon reconnection. The first
authorized test kernel has now booted and correctly rejected the no-console
device case; no hardware-safety claim follows from that, compilation or fixtures.
Boot-pinned retention is bounded
but consumes memory until reboot; 64 KiB allocator slack is conservative, not
a verified native allocator maximum. Native kproc can wait indefinitely, and
native intrenable allocation failure can panic. Finite retry counts are not
hard wall-clock deadlines.
