# Driver security model and safety contracts

This is an experimental native kernel driver, not a full kernel audit or
isolation guarantee. The host controls the VM and DMA; malicious host guest
authority is accepted. A host can lie about completion/reset or write guest RAM
despite software checks. This project makes no host-escape claim.

Untrusted device inputs include PCI capabilities, registers, ring completions,
control events and data. Guest requests enter through the eve-only #Y Dev.
Same-registry ownership checks do not exclude a privileged foreign kernel driver;
final kernel configuration must establish that exclusion.

## Memory safety and DMA lifetime

Fixed port/queue/buffer/frame/request bounds precede allocation, copying and
indexing. Capability/BAR/notify arithmetic checks overflow, type, extent and
overlap. Whole used-ring snapshots are immutable CPU storage; all entries are
validated before any lease release. Malformed metadata faults without partial
commit. Native physical translations check the entire allocation's linear extent.

Exposed DMA storage survives setup, completion and notification failures.
A failed MMIO write may already have applied. Reclamation requires a newer
observed full-reset epoch plus drained worker, client, operation and wake
references. FAILED status, clearing BME, a timeout or a software generation
alone proves none of that. Never clear live rings, retag posted leases or
force-release a caller's reference.

Per-slot receipt/request serials, port generations, controller epochs and exact
Chan identity reject stale/forged reuse. These are lifetime identifiers, not
secret authentication capabilities. A completed RX payload stays held until
bounded copy/reap; FIFO saturation cannot authorize premature repost.

Four fresh boot contexts remain pinned. Withdrawal does not free endpoint,
broker, worker, mailbox, locks or callback aux: delayed IRQ and CPU-only Chan
views can still reference them. Budgeting reserves the full pool and conservative
allocator slack before DMA. It does not prove a native allocator maximum or
bound every stock Chan/block allocation.
Stock `vunmap` retains native mappings/page tables; direct-window restrictions
avoid repeated high-physical mapping growth, not general hot-unplug reclamation.

## Thread safety and races

One worker owns runtime queue/control/data MMIO and ring/port/pump/stream state.
IRQs acknowledge ISR, latch and wake; the supervisor owns startup/reset outside
live runtime. Controller admission/leases/operation guard, mailbox, worker
phase, broker metadata, Dev registry and owner state have explicit locks.
Blocking native calls, user copies, allocations and callouts occur outside
ilocks. Wake callbacks release mailbox state before native wake locks, avoiding
Rendez inversion. Sleep readiness predicates close wake-before-sleep gaps.

The worker lease is reserved before spawn; publication requires actual Running
entry, not Launched. Done follows last runtime/predicate access and counted stop
callbacks, but does not prove native process return/dispatch drainage.
Closed mailboxes reject further mapping reads; `intrdisable` is not treated as
a join of pre-dispatched callbacks.

Broker Busy bytes belong to the worker, Reserved/Taking bytes to callers.
Cancellation cannot free still-borrowed storage. Notes and copy faults clean up
using immutable ownership tokens outside locks. A live data handle can prevent
teardown, intentionally; timeout never bypasses lifetime checks.

Host backend closes preserve guest owner/epoch, RX and handles. Actual guest
close/remove/fault requires a fresh reset/epoch before replacement.
Stop closes admission and IRQ, masks INTx/clears BME, drains actual users and
withdraws the namespace before reset/free. Ambiguous PCI/reset or drain failure
retains claims and objects.

The revised userspace helper waits for asynchronous publication with a stat-only
bounded retry loop before one launch. Native validation/deployment is pending;
the earlier installed helper may still have the one-shot readiness race.
The driver also does not restart an agent after actual owner retirement.

## CVE-pattern boundaries

The decoder rejects out-of-range IDs/lengths, duplicate or unposted completions,
conflicting port lifecycle and invalid UTF-8/NUL names. Console data never becomes
QGA data; only the exact nonconsole name is selected. Control backpressure holds
receipts instead of dropping mandatory replies. A stale guest close cannot
release a new owner.

Checked 16-bit PCI command writes avoid W1C status. Unsupported MSI paths,
IOMMU/noncoherent features, packed/indirect/event-index and per-queue reset are
not negotiated. Notification metadata must match configured current-epoch queues.
No user-derived kernel pointer, format string, shell command or raw PCI operation
is exposed. Status is bounded numeric CPU metadata with no payload or addresses.

Cancellation, short writes and backend disconnect do not prove rollback or host
delivery. Blind replay may repeat guest side effects. Byte/resource limits do not
create an overall operation deadline or prevent a host withholding data.

## Access control

Data is eve-only exclusive ORDWR/0600, status OREAD/0400 and directory 0500.
Attach/walk/stat/open/read/write/block-read enforce `iseve` even for delegated
FDs. Create/remove/wstat, truncation/remove-on-close and arbitrary port selection
are denied. Copied open flags/aliases cannot read/write/clunk another Chan's
handle. Native dup shares the same Chan; final close consumes the exact owner
once. Close deliberately permits native clunk cleanup without eve.

`qgavirtio=1`, kernel images/config and startup hooks are trusted administrator
inputs. The owner registry is not a global driver-claim mechanism.
The stream does not isolate eve from eve or establish trust in guest QGA replies.
Host credentials or local host filesystem exports are not part of this driver.

## Evidence and remaining limits

The source/fixture review covers memory safety, thread ownership, races,
parser/lifetime/overflow patterns and insecure access paths. It establishes
contracts and identified gaps; it is not independent security certification.

Independent Linux checks passed 15 C suites and 21 Python methods, post-split
ASan/UBSan/leak checks and GCC analyzer. Fixtures cover actual Dev/blocking source
with simulated Chan/note/copy/sleep primitives, concurrent brokers/workers,
malformed PCI/rings, backpressure, wraparound, stale references and failure paths.

Current ThreadSanitizer attempts on three concurrency fixtures failed runtime
initialization with `unexpected memory mapping`. No detected code race and no
current TSan pass follows. Earlier combined-tree TSan passes are prior evidence.
Native post-split checks remain unrerun; prior combined native suites and complete
kernel linking passed.

The new integration uses ordinary native include/build rules and reuses native
VirtIO register/status enums with wire-ABI size checks. Separately, the optional
checked-IRQ path binds an installed vector under the mailbox lock and filters
actual Ureg dispatch before consuming ISR. Default INTx stays masked; opt-in
unmasking requires both the native API patch and trusted boot configuration.
The saturating delivery counter is metadata, not a reset/lifetime token.
Checked mapping and DMA
lifetime logic remain local; replacing them with unchecked native helpers would
weaken these contracts. Actual native compilation remains a release gate.

Real native normal binary I/O, private-child `none` data/status denial,
operator PVE file/exec results and automatic cold startup passed.
The last verified guest keeps INTx masked; 100 ms worker polling does not prove
interrupt routing. Legacy native `intrenable` lacks success reporting and may
panic on allocation failure. The optional checked patch reports registration
failure, not hardware delivery or platform fault safety. An ambiguous unmask
failure may also defeat best-effort re-masking; resources remain retained and
recovery may require reboot. The path must not be treated as production-verified.
Native kproc resource waits are not bounded by supervisor retry counts.

Live reset/fault/unplug, native note/copy/allocator failure, competing-driver
injection, hardware recovery, daemon restart and full cache-ordering/callback
lifetime checks remain incomplete. Pool exhaustion/drain failures require
recovery or reboot; no unsafe reclamation is supplied.
See [VIRTIO](VIRTIO.md) for detailed contracts and [STATUS](STATUS.md) for evidence.
