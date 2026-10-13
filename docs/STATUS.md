# Virtio-serial verification ledger

## Current state — 2026-10-13

The native modern-capability PCI virtio-serial driver is installed and enabled
on the authorized test VM. A real cold BIOS boot selected `9pc64qga`, supplied
the full local bootargs without a prompt and started the existing
`qga9-0.5.1-dev` daemon on `#Y0/data`. The normal PVE agent channel is virtio;
the daemon has no UART descriptors. Stock `9pc64` and verified rollback files
are retained. This repository split makes no VM or Proxmox changes.

The operator supplied successful real PVE info, synthetic six-operation binary
file round-trip and all five exec smoke tests over virtio: literal argv, binary
stdin, merged output, responsive ping and one-shot reaping. This is operator
host evidence, not an independent developer session on PVE. Guest inspection
showed the same live owner/epoch after those per-RPC socket reconnects.
An explicit native test changed only its private child's identity to `none`;
both data and status access were denied and the parent was unchanged.

The driver remains experimental: INTx stays masked and the worker polls at
bounded 100 ms deadlines. A registered IRQ row does not prove usable routing.
Checked IRQ activation, live fault/reset/unplug injection and full platform
lifetime validation remain pending.

## Repository split — 2026-10-13

Moved the kernel tree, 31 driver test/fixture files and driver documentation out
of the QGA working tree. Source files were preserved byte-for-byte. Independent
Linux builds passed 14 driver C suites and 13 boot-config Python methods; the
QGA repository separately passed 7 C suites and 54 Python methods.
Independent GCC -fanalyzer checks also passed in both repositories.
Native mkfiles were separated into 9 driver C suites and 7 QGA C suites.
The native split build has not been rerun on the guest; approval-review
availability initially prevented that check. Prior combined native builds passed
all 16 C suites and linked the complete revised kernel warning-free.
No new post-split sanitizer or ThreadSanitizer run is claimed.

The kernel sources/tests were untracked in the original working tree, so there
is no committed kernel history to extract. The original QGA Git history remains
untouched. This repository uses the operator-supplied GitHub remote and a
separate initial driver import on main. See
[provenance](../PROVENANCE.md).

## Host reconnect fix and persistent activation — 2026-10-13

PVE opens and closes its QGA socket per RPC. Host PORT_OPEN changes therefore
do not close the guest Chan, change its owner generation, clear received bytes
or consume a reset context. Positive writes wait while the backend is closed;
reconnect resumes with the same cookies and posted leases. Real guest close,
port removal or controller faults still revoke ownership and require drained,
confirmed reset before reuse. No already-executed command is cancelled/replayed
by a host disconnect.

Regression fixtures cover 257 stream backend cycles and 65 broker backend
cycles, including pending binary I/O. Final combined normal checks passed 21 C
suites and 67 Python methods. GCC analyzer passed; the broker (3069 assertions)
and actual Dev/blocking-adapter fixture (1879 assertions) passed ThreadSanitizer.
The revised kernel linked to 5515658 bytes; native IO assertions were 2730.

The boot helper now skips chmod only for exact canonical #Y stream data paths,
whose fixed permissions are enforced by the driver. Generic streams and UARTs
retain chmod checks. It was started in a clean native namespace and later
automatically by the cold-boot hook, independent of diagnostic Drawterm.

The explicit boot helper preserves and byte-checks the stock image/config,
adds a separate FAT image and sets bootfile, qgavirtio and full nobootprompt.
A warm reboot from the FAT image failed after clean filesystem halt because
the FAT copy lacks execute permission; this was not a kernel panic. A full host
Stop/Start then booted the separate image successfully. Use the executable
`/amd64/9pc64qga` for kexec, not the FAT file.

The `can't find bus type 12, number 0` warning comes from the stock MP/PCI
interrupt-routing lookup (type 12 is PCI), and was also seen with the stock
kernel. Cold boot and native QGA worked despite it. Do not hide it by disabling
ACPI/APIC/MP globally; retain masked INTx until routing/registration is verified.

## Earlier combined-repository batches

The dated entries below preserve the historical evidence and limitations at
each stage. Statements such as “no hardware”, “ISA unchanged”, missing layers,
combined test counts and old checkout paths describe that earlier batch, not
current deployment or repository boundaries. Current state above takes precedence.

- Implemented native-compatible virtio foundations in `kernel/vconsole.[ch]`:
  bounded PCI capability/BAR/notify decoding, direct split-ring descriptors,
  whole-batch used-ring validation and lease accounting, control codecs and
  exact nonconsole named-port selection with single-owner generation tokens.
  The inspected spec omits the name NUL while QEMU includes one; both forms are
  tested without permitting embedded/additional NUL. This is a pure core, not a
  PCI/MMIO/DMA/IRQ/Dev driver, and is not linked into the daemon or a boot kernel.
- Final Linux tests passed all eight portable C suites, including 159796 virtio
  assertions, and all 54 Python methods. ASan/UBSan/leak checks and GCC analyzer
  passed. Virtio tests include actual 16-bit index wraparound through 65538
  completions, hostile snapshots, output atomicity, lease retention, reconnect
  and stale generation cookies/tokens. Counts include parameterized checks, not
  that many distinct scenarios. Scoped review covered memory, threads, races,
  CVE-style patterns and access with no unresolved finding. DMA/fences/IRQ
  correctness is explicitly outside this unimplemented adapter's test evidence.
- Copied the foundation source/fixtures into a separate native checkout,
  `/usr/glenda/src/qga9-virtio-core-20261011`. Warning-free `mk qga9 test
  vconsole-kernel-check.6` passed all eight native C suites (57 information,
  159796 virtio assertions). The compile-only PC64 kernel-header check passed;
  its 21989-byte object depends only on memmove/memset/strcmp. An initial include
  lookup failure was fixed with `-Ikernel` before the final successful build.
  No object was linked into or installed as a kernel, and no live DMA occurred.
  The installed daemon remains `0.5.1-dev`; no new command/version, binary
  replacement, service restart, boot/PVE change, power/clock action or wider
  host export was performed. The existing detached ISA daemon remained alive.
  The installed binary still compared byte-for-byte equal to its original codec
  deployment build. The fresh different-checkout test build was not installed.
  The isolated diagnostic login exited cleanly; no client was left running.

- Implemented the next virtio layer in `kernel/vconsole-mmio.[ch]` and
  `kernel/vconsole-pc.[ch]`: bounded assigned-BAR mappings, ordered register
  access, confirmed status-zero reset, required VERSION_1/MULTIPORT negotiation,
  stable port/queue discovery and checked queue/notify metadata. Failed resets
  retain references and do not advance epochs; release requires confirmed reset
  and a caller assertion that references are drained. This is initialization,
  not a usable driver: no queue DMA addresses/enable, DRIVER_OK, PCI command
  writes, IRQ registration or Dev/Chan endpoint were added.
- Inspected the actual PC64 `vmap`, `vunmap`, kernel headers and existing virtio
  helpers. Stock `vunmap` retains mappings; above the direct window, repeated
  mappings can allocate fresh virtual ranges. The adapter therefore restricts
  complete extents to the deterministic direct VMAP window (1 TiB on this
  kernel). Its reference release does not reclaim native mappings/page tables.
  Independent access validation also rejects invalid owner lengths before
  subtraction. Scoped memory/thread/race/CVE-pattern/access review found no
  unresolved issue in this changed slice after hardening; DMA/IRQ and hardware
  lifetime correctness remain separate unverified integration requirements.
- Final Linux normal, ASan/UBSan/leak and GCC analyzer runs passed all ten C
  suites and 54 Python methods. The new MMIO fixture passed 256194 assertions,
  including stuck/delayed reset, acquisition/read/write faults, feature/config
  rejection, queue boundaries, stale epochs and hostile metadata. The PC adapter
  fixture passed 37847 assertions against test-only kernel primitives. These
  counts include per-access and parameterized checks, not distinct scenarios.
  LeakSanitizer required the usual approved execution outside the ptrace sandbox.
- Copied sources/fixtures to `/usr/glenda/src/qga9-virtio-mmio-20261011`.
  Warning-free native `mk qga9 test vconsole-kernel-check.6
  vconsole-mmio-kernel-check.6 vconsole-pc-check.6` passed all nine portable C
  suites, including the 256194 MMIO assertions. Actual PC64 kernel-header
  compilation passed for MMIO (15656-byte object) and PC adapter (7652 bytes).
  An initial test-helper `access` name collision was corrected before the final
  successful build. These are CPU simulations and compile checks, not live PCI,
  MMIO or DMA execution; no kernel was linked, installed or booted.
- The fresh daemon regression build was not installed. The installed 199083-byte
  `0.5.1-dev` binary still compares byte-for-byte equal to its original codec
  deployment build, and the current detached ISA agent remains running. No
  binary replacement, service restart, boot/PVE change, power/clock mutation,
  credential copying or expanded filesystem export was performed in this batch.
  The isolated diagnostic login exited cleanly; no diagnostic client remains.

- Implemented coherent PC64 split-queue storage in `kernel/vconsole-dma.[ch]`,
  with bounded aligned ring/payload arenas, owned allocation results, checked
  physical addresses, barrier-ordered avail publication and immutable used-ring
  snapshots. RX payloads stay held until bounded copy/reaping, and nonwrapping
  per-slot receipt serials prevent old receipts recycling later uses of a slot.
  Completion scratch is object-owned rather than a large kernel stack array.
- Added actual native `mallocalign`/`free`/`PADDR`/`coherence` callbacks to the PC
  backend. Inspected allocator and direct translation source on the running VM;
  accepts only direct KZERO RAM and verifies the linear physical extent.
  `vcmmenable` rechecks discovery metadata, validates disjoint aligned rings,
  writes/readbacks queue size and all 64-bit address halves, and enables/readbacks
  the queue without DRIVER_OK. The DMA owner marks exposure before MMIO, so
  partially applied setup errors retain memory until a newer confirmed full
  reset and caller-drained references. No queue-disable-with-zero shortcut exists.
- Final Linux normal, ASan/UBSan/leak and GCC analyzer runs passed eleven C
  suites and 54 Python methods. Native/host DMA tests passed 313751 assertions,
  including every bounded layout capacity, malformed allocation results, all
  setup read/write failures, readback mismatches, stuck reset, malformed whole
  completion batches, held RX buffers, forged/duplicate receipts, snapshot
  isolation and 65538 real cycles through 16-bit index wraparound. PC primitive
  tests passed 37941 assertions, now including native allocator callbacks.
  Counts include parameterized/per-access checks, not distinct scenarios.
- Scoped memory/thread/race/CVE-pattern/access review found no unresolved issue
  in this changed slice after bounds/lifetime hardening. One worker owns each
  queue and controller selectors; barriers/immutable snapshots address device
  publication ordering, not concurrent IRQ/Chan reference management. Hardware
  DMA compliance/coherence, notifications/health checks, controller budgets,
  IRQ/worker/Dev ownership and reference draining remain integration gates.
- Copied source/fixtures to `/usr/glenda/src/qga9-virtio-dma-20261011`. Final
  warning-free native tests passed ten C suites; actual PC64 kernel-header
  compilation passed for DMA (15000-byte object), updated MMIO (19960 bytes)
  and PC allocator/backend (9508 bytes). Initial fixture unused-argument warnings
  were corrected using native USED before the final warning-free compilation.
  No kernel was linked/installed and no live controller/DMA was invoked. The
  separate daemon regression build was not installed or linked with this layer.
- The installed 199083-byte `0.5.1-dev` agent still compares byte-for-byte equal
  to its original codec deployment build; the detached ISA daemon remains alive.
  No installed binary, service restart, boot/PVE setting, power/clock action,
  credential copy or expanded filesystem export was performed in this batch.
  The isolated diagnostic login exited cleanly; no diagnostic client remains.

- Added a separately locked IRQ mailbox in `kernel/vconsole-irq.[ch]`: bounded
  read-to-clear ISR acknowledgement, coalesced queue/config/work events,
  readiness predicate, sticky fault/stop and counted wake callbacks. Wake runs
  outside the mailbox lock; irreversible close skips late mapping reads.
  Closed/drained gates ISR mapping release only after reset and worker join,
  not freeing mailbox/locks/aux. These remain boot-pinned unless the platform
  independently proves every pre-dispatched/predicate/caller reference gone.
- Inspected actual native sleep/wakeup and IRQ registration/removal source.
  Added `kernel/vconsole-wake.[ch]` using real PC64 ilock/iunlock/Rendez/tsleep/
  wakeup, an immutable pinned ISR pointer and separately serialized wakeups.
  Lock ordering avoids holding the mailbox across wakeup; bounded sleep permits
  future worker health checks. Supplies an IRQ entry point, not registration,
  kproc creation or execution. The future worker owns its native note/error frame.
- Added worker-only `vcmmstart`, `vcmmhealth` and `vcmmkick`. All bounded queues
  must be configured in the current epoch before DRIVER_OK/readback; notifications
  match cached metadata and write a checked 16-bit queue ID only in Running.
  Bounded config-generation/status checks detect reset/NEEDS_RESET and changed
  layout. Confirmed reset clears queue caches; failed startup/health/notification
  retains exposed memory/mappings. DMA helpers now accept the Running state.
- Final Linux normal, ASan/UBSan/leak and GCC analyzer runs passed thirteen C
  suites and 54 Python methods. IRQ fixture: 33596 checks; expanded DMA/runtime
  fixture: 317933, including every startup/kick/stable-health I/O failure position,
  applied failed notification, status/layout faults and forged/stale metadata.
  Counts include per-access/parameterized checks, not distinct scenarios.
  Linux pthread tests passed blocked-waker teardown and 600000 concurrent IRQ/work
  events. ThreadSanitizer passed without race reports in approved execution
  outside the sandbox; its initial sandbox runtime startup exited 139. Final
  LeakSanitizer initially rejected sandbox ptrace, then passed outside it too.
- Scoped memory/thread/race/CVE-pattern/access review found no unresolved issue
  in this slice under its explicit pinning/ownership/callback contracts. Tests
  do not prove native Lock/Rendez or live hardware ordering. Remaining work:
  controller kproc/memory budget, control RX/TX pump/reply backpressure, console
  events, reset/worker join, Dev/Chan and migration/hardware validation.
- Copied sources/fixtures to `/usr/glenda/src/qga9-virtio-irq-20261011`. Final
  warning-free native tests passed eleven C suites, including the same 33596
  IRQ and 317933 DMA/runtime assertions. Actual PC64 kernel-header checks passed:
  core 21986, MMIO 24293, DMA 15068, PC adapter 9508, IRQ 6670 and wake 4033
  bytes. Native wake object has actual lock/sleep/wakeup and mailbox dependencies;
  no userspace pthread shim. No kernel was linked/installed or IRQ/live DMA run.
- Confirmed the installed 199083-byte `0.5.1-dev` binary still matches its
  original codec deployment build; the detached ISA daemon remains running.
  No binary replacement, service restart, boot/PVE change, power/clock mutation,
  credential copy or broader filesystem export occurred. The isolated diagnostic
  login exited and a process check found no remaining diagnostic Drawterm client.

### 2026-10-12: bounded control pump and startup footprint

- Added `kernel/vconsole-pump.[ch]` with a plan-before-acquisition footprint
  check, borrowed genuinely bound/unused queue set, all-RX-before-DRIVER_OK
  startup, DEVICE_READY and bounded worker-callable control RX/TX iterations.
  A hard 16 MiB limit counts DMA arenas and planned queue/pump/MMIO objects;
  allocator/platform/future Dev/Chan overhead still needs separate accounting.
  Init rechecks actual capacity/footprint, not permission to allocate first and
  exceed the budget temporarily. No live PCI owner, IRQ registration or kproc
  is supplied, and this layer is not linked into the daemon.
- Receipt FIFOs preserve used-ring order rather than descriptor ID order.
  A 16-entry reply FIFO handles PORT_READY and mandatory console OPEN=1;
  saturation retains the next RX receipt instead of dropping a reply or
  prematurely reposting its lease. Runnable-CPU flags exclude TX backpressure.
  Unsent replies carry port incarnation and guest ownership generation;
  old remove/readd/release/disconnect replies cannot affect a new control-plane
  owner. Already posted replies cannot be retracted; late TX completion only
  releases its exact receipt. Exact-QGA claims stay inactive until OPEN was
  posted and successfully notified. Duplicate host OPEN preserves a live claim.
- Initial data RX cookies are zero (no stream owner), not controller epochs
  that might accidentally match a port generation. Future data adapter must
  drain/filter/repost before delivery; control-plane alive is not proof that
  payload belongs to a new stream owner. Console ports are acknowledged but
  never claimed as QGA; console/non-QGA data handling is still future work.
- Malformed controls/rings, lifecycle conflicts, I/O/notification failure,
  generation exhaustion and unexpected reset are fail-stop. Stop/fault never
  frees exposed queues; future owner must close IRQ gate, confirm device reset,
  join worker/Chan callers, then release DMA and MMIO in order. Scoped review of
  memory/thread/race/CVE-pattern/access paths found no unresolved issue under
  these ownership/pinning contracts. Actual controller/Dev integration and
  hardware behavior remain unverified, not implied by the mock tests.
- Final Linux normal and ASan/UBSan/leak runs passed fourteen C suites and
  54 Python methods; GCC analyzer passed. Leak checks used approved execution
  outside the ptrace sandbox. The new pump fixture passed 2047966 assertions,
  covering every supported default-budget port/queue dimension, atomic budget
  rejection, genuinely bound/fresh queue checks, all startup/receive/reply MMIO
  failure positions, reply saturation/resumption, pending/single-owner claims,
  host reconnect and stale replies, malformed control/ring frames, out-of-order
  descriptor completion and 65538 cycles through actual RX/TX/FIFO index wraps.
  Counts include parameterized/per-access checks, not distinct scenarios.
  Prior IRQ mailbox pthread regressions also passed; no new pump TSAN claim.
- Source/fixtures were copied to `/usr/glenda/src/qga9-virtio-pump-20261011`
  (the checkout was started before the date rollover). Final warning-free native
  `mk qga9 test` passed twelve C suites and all seven actual PC64 kernel-header
  checks. Pump fixture passed the same 2047966 assertions; kernel-context pump
  object is 19053 bytes, depending on existing core/DMA/MMIO helpers and memset,
  not user-space allocation or pthreads. Initial native helper-name collisions
  and a fixture definite-assignment warning were corrected before final success.
  No kernel was linked/installed, IRQ registered or hardware DMA/controller run.
- Confirmed installed agent is still the original byte-for-byte 199083-byte
  `0.5.1-dev` codec deployment build and the detached ISA daemon is running.
  No installed binary/config/boot/PVE change, service restart, power/clock
  mutation, credential copy or expanded filesystem export occurred. The
  isolated diagnostic client exited cleanly; its project export was read-only,
  without home-directory or SSH-agent sharing.

### 2026-10-12: bounded worker-only binary stream

- Added `kernel/vconsole-stream.[ch]`: nonblocking literal binary RX/TX for the
  exact QGA port, 4096-byte RX FIFO, bounded short reads/writes and explicit
  Wait/Closed/Reset results. Stream object/scratch/receipts are budgeted before
  acquisition; no allocation, worker spawn, kernel Dev or hardware activation
  is hidden here. Refactored the existing fake ring/MMIO/allocator fixture into
  a shared test-only header, retaining the pump's prior 2047966 checks.
- One adapter selects deferred data RX on a fresh pump: control RX remains
  primed before DRIVER_OK, QGA data RX is posted with its actual owner generation
  before guest OPEN can be sent. Console RX is populated and discarded separately;
  unused nonconsole ports receive no guest OPEN/data buffers. All queue configs
  still precede DRIVER_OK. Claim/token padding is initialized.
- RX preserves used-ring order; a full FIFO retains the entire next receipt/
  payload until space exists, with no truncation or premature reuse. TX copies
  only the accepted bounded prefix into owned DMA memory; full TX returns Wait.
  All relevant queues are snapshotted before the action budget so shared IRQ
  work cannot be missed in a one-action iteration. RX/TX and sink actions
  alternate, sink ports/slots rotate, and external backpressure is not runnable
  CPU work. Taken/scratch/closed/faulted bytes are cleared.
- Tokens carry controller identity, reset epoch and port generation. The future
  native owner supplies boot-unique identities and object/reference lifetime;
  these are anti-ABA identifiers, not OS permissions. Wrong/stale tokens cannot
  consume bytes, write or close a live owner. Pending OPEN cannot perform I/O.
  Host close/remove or local close clears queued bytes, while late receipts are
  discarded/reaped without posting for a new owner. One published QGA owner
  per controller epoch: reopening explicitly requires full confirmed reset and
  drained/joined callers, never software-index clearing or live-cookie retagging.
  No automatic reset/reopen or blocking Dev/Chan is supplied yet.
- Scoped memory/thread/race/CVE-pattern/access review found no unresolved issue
  under the one-worker/pinned-reference contracts after hardening. During tests,
  a shared-IRQ/small-budget scheduling gap was corrected by snapshotting relevant
  sink queues before bounded actions; a single-slot fixture overfilled a control
  queue and was corrected to wait/repost between messages. No new thread/race
  sanitizer claim applies to the stream; earlier IRQ pthread regressions passed.
- Final Linux normal/ASan/UBSan/leak runs passed fifteen C suites and 54 Python
  methods; GCC analyzer passed. Stream fixture passed 1909950 assertions covering
  FIFO saturation/resumption/wrap, literal NUL/ff, short reads/writes, copied TX,
  pending/single-owner tokens, stale identity/epoch/generation rejection, late
  close/disconnect/remove receipts, fair console discard, console-to-QGA reuse
  rejection, malformed rings, all TX kick failure positions, budget/init/adaptor
  exclusivity, out-of-order completions and 65538 actual single-slot RX/TX cycles.
  Counts include per-access/parameterized assertions, not distinct scenarios.
  LeakSanitizer used approved execution outside the ptrace sandbox.
- Copied sources/fixtures to `/usr/glenda/src/qga9-virtio-stream-20261012`.
  Final warning-free native build/tests passed thirteen C suites and all eight
  actual PC64 kernel-header checks. Stream fixture passed the same 1909950
  assertions; stream kernel object is 21516 bytes, updated pump 19987. Dependencies
  are existing core/pump/DMA/MMIO helpers plus memset/strcmp, not userspace
  allocation/pthreads. No kernel was linked/loaded and no live DMA/IRQ was run.
- Confirmed installed agent still matches its original 199083-byte `0.5.1-dev`
  codec build; detached ISA daemon remains running. No installed binary/config/
  boot/PVE change, service restart, power/clock mutation, credential copy or
  broader host export occurred. The isolated diagnostic login exited cleanly;
  project sharing stayed read-only with no home directory or SSH-agent export.

### 2026-10-12 continuation — controller acquisition and reference-gated reset/reopen

- Added `kernel/vconsole-controller.[ch]`: confirm reset, negotiate/discover,
  plan the complete <=16-MiB footprint BEFORE acquisition, acquire all metadata/
  DMA before bind, then initialize a fresh pump/stream. Exact metadata includes
  controller/MMIO/session/pointer table and the actual queue count, not an
  unbudgeted fixed maximum array. Platform allocator overhead, pinned IRQ state
  and future Dev state remain separately accounted integration obligations.
- Partial allocation failure unwinds only acquired blocks. Every attempted bind
  is conservative-exposed; unwind requires a newer confirmed reset. Failed bind
  plus failed reset retains all arenas/session for an explicit later retry.
  No FAILED-status shortcut, register unmapping or live-cookie retagging.
- Implemented actual bounded lifetime leases (one worker, 64 clients) protected
  by a short gate lock, plus an exclusive operation guard and nonwrapping slot
  serials. Runtime requires the exact worker lease; client references do not
  authorize stream access. Stop closes admission/IRQ without mutating rings.
  Reset waits for ALL returned references, in-flight operations and counted IRQ
  wake callbacks before confirming reset/free. Missing puts retain memory safely.
  New session epochs reject stale worker, client, stream and IRQ state.
- Added native `vconsole-controller-pc.[ch]` ilock/iunlock/malloc/free callbacks,
  compiled against actual PC64 declarations. No native PCI ownership claim,
  bus-master activation, interrupt registration, kproc or Dev was performed.
- Scoped memory/thread/race/CVE-pattern/access review found no unresolved issue
  under the documented contracts after hardening. Fault closure occurs before
  guard release so it cannot hit a new epoch; reverse unwind advances per freed
  allocation. Latched IRQ fault/stop is checked before queue work even if common
  status looks healthy; deliberate stop remains distinct from failed startup.
  Native fixture names conflicting with libc were corrected.
- Final Linux normal/ASan/UBSan/leak runs passed sixteen C suites and 54 Python
  methods; GCC analyzer passed. The controller fixture passed 41739 assertions:
  exact budgets, all init MMIO read/write failures, both metadata/every DMA
  acquisition failure, every bind position with failed unwind reset, failed
  runtime reset, maximal default-capacity layout, binary reopen/old-token
  rejection, healthy-status ISR fault and serial/role bounds. Linux additionally
  ran deterministic blocked-operation/DRIVER_OK/waker teardown and 40000 actual
  concurrent reference/worker cycles. Counts include per-fixture assertions,
  not distinct scenarios. Final controller ThreadSanitizer passed without race
  reports; one intermediate startup failed with its known unexpected-memory-
  mapping runtime limitation, before tests, then a fresh approved run passed.
- Copied final sources to `/usr/glenda/src/qga9-virtio-controller-20261012`.
  Warning-free native `mk qga9 test` passed fourteen C suites; controller fixture
  passed 41171 assertions (pthread cases are Linux-only). All ten actual PC64
  kernel-header objects compiled. Final manager object is 19083 bytes and native
  gate/allocation glue 3154 bytes; unresolved dependencies are existing bounded helpers
  plus memset. Native glue uses only ilock/iunlock/malloc/free. No kernel was
  linked/loaded, no live DMA/IRQ/kproc ran and no usable virtio Dev exists yet.
- Confirmed installed 199083-byte codec-build agent remains byte-identical and
  the detached ISA daemon stays running. No installed binary/config/boot/PVE
  change, service restart, power/clock mutation, credentials/key transfer or
  broader filesystem export occurred. Diagnostic export remained project-only,
  read-only, isolated from home and SSH-agent state. Diagnostic login exited
  cleanly and no development Drawterm client remained.

### 2026-10-12 continuation — one-shot worker and native PCI ownership helpers

- Added `kernel/vconsole-worker.[ch]`: exact worker lease and attached IRQ object
  reserved before one-shot spawn; bounded queue iterations/health deadlines,
  eight-burst fairness, cooperative stop and normalized callout errors. Worker
  reference remains pinned through ALL sleep-predicate and tracked stop-callback
  access. Done follows actual final lease return, not just a stopped flag or
  runner return. Late old-worker stop cannot target a reopened epoch.
- Added native `vconsole-worker-pc.[ch]` ilock/kproc/tsleep/sched/wakeup adapters
  with local waserror frames and nonconsuming `vcpcwakepause`. No native kproc
  was started. Native creation has no finite resource-wait bound, and native
  note/error handling was inspected/compiled rather than live-injected.
- Added `vconsole-owner-pc.[ch]`: bounded four-controller registry/nonwrapping
  identity, same-TBDF/object exclusion, bounded capability/MSI/power validation,
  assigned-BAR mapping and checked 16-bit command/cache changes. Bind manager
  BEFORE acquisition; required budgeted prepare reserves standard native
  owner/worker/IRQ/gate and whole registry overhead before queue acquisition.
  Extra retained contexts/Dev/allocator overhead remain separate obligations.
- Native routing inspection showed MSI-first selection; all MSI capabilities
  are rejected in this INTx prototype, plus existing BME and enabled MSI/MSI-X.
  Registration attempt precedes checked BME and DRIVER_OK, but native intrenable
  has no success status and can panic on allocation. INTx therefore stays masked;
  bounded worker deadlines poll until checked IRQ activation is supplied/proven.
  No IRQ or bus-master operation was invoked on the VM by this compile-only batch.
- Teardown closes admission/IRQ, masks/clears BME, deregisters exact attempted
  handler, waits real lease/operation/waker drainage, confirms reset then frees/
  detaches. Saved command is restored before registry release. Failed restore
  after detach retains claim and retries without resetting/freeing/unregistering
  twice. Old IRQ/worker/aux stay boot-pinned because Done/deregister is not a
  late-dispatch/native-process join or permission to free those objects.
- All five scoped review areas (memory/thread/race/CVE patterns/access) were
  checked with no unresolved changed-scope finding under the stated contracts.
  PCI registry is not a kernel-global foreign-driver claim; final config must
  exclude competition. Same-object claim, wrong IRQ sleep predicate, late old
  stop, native-context budget and post-detach restore received regressions.
- Final Linux normal/ASan/UBSan/leak runs pass eighteen C suites and 54 Python
  methods; GCC analyzer passes. Worker fixture passes 4581 assertions including
  real pthread blocked startup/predicate/stop callback joins; native PCI owner
  fixture passes 120 checks with fake native primitives and ordered controller
  stubs. Counts include per-fixture assertions, not distinct scenarios. Worker
  and concurrent registry ThreadSanitizer runs pass. Intermediate registry
  runtimes failed before tests with unexpected-memory-mapping startup limitation;
  a fresh approved run passed. One silent SIGSEGV retry could not be backtraced
  because gdb was unavailable; the same final binary subsequently completed all
  120 checks with ThreadSanitizer verbosity enabled and no race reports. No
  native interrupt-ordering or sanitizer-runtime reliability proof is claimed.
- VM temporarily became unreachable during work; no native result was claimed
  from that outage. After operator restored connectivity, final sources were
  copied to `/usr/glenda/src/qga9-virtio-worker-20261012`. Warning-free native
  build/tests pass fifteen C suites, worker 1372 assertions and all thirteen real
  PC64 kernel-header checks. Worker core object 9523 bytes, native worker adapter
  5639 and final native PCI owner 13917. Native adapters depend on actual kernel
  primitives, not pthread/runtime shims. No kernel was linked/installed or live
  kproc/PCI/DMA/IRQ path exercised. Fixture wlock/wunlock names were corrected
  to avoid native libc declarations; production behavior was unchanged. Native
  spawn/wait/yield/join reject null process context before installing error frames.
- Installed 199083-byte ISA agent remains byte-identical to its original codec
  build and is running after connectivity returned. No installed binary/config/
  boot/PVE change, service restart, power/clock mutation, credential/key copy or
  wider host export was performed. Diagnostic export was project-only/read-only
  in the isolated environment, with no home directory or SSH-agent sharing.
  Final diagnostic login exited cleanly; no project-export Drawterm client remained.

## Worker request broker / native blocking adapter batch (2026-10-12)

- Added `vconsole-io.[ch]`: one exclusive owner, eight fixed private 4-KiB
  request buffers, captured identity/epoch and nonwrapping slot/owner tokens.
  Input reservation precedes user copy; commit transfers to worker; completed
  output remains caller-owned until last copy/reap. Never retains user pointers.
  Cancelled Busy operations retain their buffer/lease until worker last access;
  RX consumption/TX publication can already have happened, so no blind replay.
- Open completion waits for matching host-open named port and posted/notified
  guest OPEN. Empty RX and full TX wait independently; a blocked read does not
  block a write. Clunk marks closure, worker closes stream. Unclaimed opening
  may retry with a new owner generation; claimed reopening requires a full
  confirmed reset and fresh stream/broker. CPU-only locked status is supplied.
- Exact worker-reference admission is checked before broker service. Client
  holds from a newer controller epoch are rejected/unwound without runtime
  access. Pending requests and live handles retain actual client references
  through shutdown; no lease is forcibly dropped. Reserved/Taking caller
  buffers survive stop until their last copy/predicate access.
- Worker optional service/finish hooks are paired and validated before launch.
  Finish closes controller admission BEFORE broker cleanup, still holding its
  worker lease. Bound spawn failure also runs finish; unbound startup cleanup
  stays the supervisor's responsibility. Native fresh pre-launch binding adds
  the request service to the one kproc, not a second stream owner.
- Added actual PC64 `vconsole-io-pc.[ch]` blocking open/read/write with native
  per-slot Rendez/serialized wakes, small immutable stack wait arguments and
  waserror cleanup around user copies/sleep. No ilock across copies or native
  errors, no mutable automatic cleanup flags across error frames. Final review
  added null-process-context rejection before native errors and failed-init
  controller/epoch checks before worker binding. Inspected
  running stock sleep/tsleep/timer sources: timer uses pinned Rendez, predicates
  run only in sleeping process, notes cancel timer. Native runtime fault/note
  injection is NOT claimed by compilation or this inspection.
- Required native owner prepare now subtracts complete `VcPcIo` context before
  controller acquisition. Eight buffers/locks/Rendez are counted; extra retained
  epochs, allocator overhead and final Dev state still require separate bounds.
  No automatic context pool, retries, device namespace or permission bypass was
  created; live handles can legitimately delay teardown until clunk.
- All five changed-scope review areas were assessed: memory, thread safety,
  races, CVE patterns and access. No unresolved finding remains under the
  stated contracts. Analyzer caught a test-fixture nullable zero-length copy;
  guarded copies and simulated queue/length checks corrected it. Final analyzer
  passes without suppressions. Permission/Chan alias/clone/clunk integration
  remains a gate because no native Dev is published or executed yet.
- Final Linux normal/ASan/UBSan/leak checks pass nineteen C suites and 54 Python
  methods. Broker passes 1341 fixture assertions, including genuinely blocked
  TX cancellation and 2000 concurrent requests across four callers. Worker
  passes 5157 assertions including service/finish/failure-order checks and
  previous threaded startup/predicate/stop joins. GCC analyzer now includes both
  broker and worker fixtures. Final broker and worker ThreadSanitizer runs pass;
  two intermediate worker runs failed at runtime startup with unexpected memory
  mappings before a fresh final run passed. No sanitizer-runtime reliability
  or native scheduling/order proof is inferred. Counts are assertions, not
  distinct scenarios or hardware tests.
- Final sources copied to `/usr/glenda/src/qga9-virtio-io-20261012`. Native
  warning-free `mk qga9 test` passes sixteen C suites: broker 1002 assertions,
  worker 1948. All fifteen actual PC64 kernel-header checks pass, including
  broker and blocking adapter. Final kernel broker object 21889 bytes, native
  blocking adapter 8529, native worker 7105 and native PCI owner 14179. These
  modules are NOT linked into a kernel, installed or executed as kernel code.
- Final native source comparisons match the project export. Installed
  199083-byte ISA agent still matches its original retained codec binary exactly;
  the same PID 356 remains blocked in read. A separate freshly linked checkout
  agent was built/tested, not installed (it is not byte-identical to the retained
  installed build). No installed daemon/config/boot/kernel/PVE change or restart,
  power/clock mutation, SSH key/agent copy or broader home export was performed.
  Diagnostic export remained project-only/read-only with filesystem/PID and
  environment isolation, not network isolation. Login exited cleanly; no
  project-export Drawterm client remained.

Next: actual Dev/Chan publication, eve-only permissions/aliases/clone/clunk,
bounded recovery retention/scheduling, recovery-capable test kernel and disposable
clone hardware tests. Current helpers still do not provide a usable virtio QGA
channel, and the operator should keep the working ISA channel unchanged.

## Native Dev/Chan interface batch (2026-10-12–13)

- Added actual `vconsoledevtab` in `kernel/devvconsole.c` and `vconsole-dev.h`.
  Defined `#Y[0..3]/data` (exclusive eve-owned 0600, exact ORDWR) and `status`
  (eve-owned 0400, OREAD), root 0500, default attach slot zero. Checked the
  running stock PC64 driver list/port+PC sources for rune Y collision. No
  device/rune was registered in the VM or native kernel config by this work.
- Every namespace/I/O entry point checks iseve, including a delegated FD after
  user change. No create/remove/wstat/truncate/remove-on-close or user control
  commands. Close is permission-independent, nonblocking and CPU-only so another
  native clunk process can release only the exact owner Chan once.
- Four static registry slots; unique monotonic 32-bit namespace serials in Qid
  path bits, no wrap. Duplicate broker/controller epoch/object publication is
  rejected. Publish requires actual Running worker entry and exact admitted
  lease/IRQ/broker epoch; Launched-before-spawn is intentionally rejected.
  Startup supervisor must still bound its waits/retries and handle failures.
- Data opening is reserved before blocking, failed notes clear only the reservation,
  success commits handle/exact Chan before COPEN. Aliases/unopened clones own
  no broker handle. Opened/foreign/malformed clones fail before native panic;
  walk vectors and stat buffers have explicit bounds. Native dup shares same
  refcounted Chan; only last close invokes owner clunk. Even a copied COPEN flag
  cannot make an alias read/write/close the actual owner's handle.
- Attach captures an immutable boot-pinned endpoint, not a current slot lookup.
  Withdraw requires worker Done, actual client/operation drainage, stopped broker
  and no opening/live data Chan; removes only exact pointer and never frees it.
  Old CPU directory/status views remain valid, do not redirect to replacements.
  Endpoint/IO/worker reuse after publication/withdrawal remains forbidden.
- Bounded numeric CPU-only status; large offsets return EOF before narrowing.
  Directory stat preserves 0500 explicitly. Block reads clamp to 4 KiB before
  allocator entry; default block-write cleanup releases denied/error blocks.
  Native owner prepare now includes endpoint and whole Dev registry before
  controller acquisition. Retained contexts/native allocator overhead remain
  separate obligations; no automatic context allocation/recovery loop exists.
- Inspected running native dev/chan/devdup/namec/close sources plus primary
  upstream Dev/Chan code. Added host-only primitive definitions/fixture, compiled
  out of the native kernel. The fixture executes ACTUAL Dev and blocking adapter
  plus real broker/controller/pump/DMA/MMIO logic with simulated native Chan,
  error stack, copy faults, sleep/timers, scheduling and coherent bus. No genuine
  native kernel execution, 9P directory codec, kproc or live hardware proof is
  claimed by these tests. Native source retains actual kernel primitives/types.
- Final five-area changed-scope review (memory/thread/race/CVE/access) has no
  unresolved finding under documented contracts. Exact Chan ownership, guarded
  Running publication, supplied-clone checks, stat/walk/offset bounds and whole
  Dev budget received explicit tests. Native exception paths avoid modified
  automatic cleanup tokens and do not throw while holding ilocks.
- Final Linux normal/ASan/UBSan/leak suites pass twenty C suites and 54 Python
  methods. Dev/adapter fixture passes 1879 assertions including real threaded
  blocked open/note, user-copy faults, actual broker lease cleanup, permission/
  mode/alias/clone/clunk/delegation, replacement and final/exhausted Qids. Counts
  include per-fixture assertions, not distinct scenarios. GCC analyzer includes
  actual Dev/native adapter and fixture. Final ThreadSanitizer completed all
  1879 assertions without race reports, including the final stat-buffer guards.
  Intermediate runtimes failed before tests with unexpected
  memory mappings; no native timing or sanitizer-runtime reliability is inferred.
- Final kernel sources copied into `/usr/glenda/src/qga9-virtio-dev-20261012`.
  Warning-free stock `mk qga9 test` passes sixteen native suites (broker 1002,
  worker 1948 assertions); all sixteen real PC64 kernel-header checks pass,
  including the Dev ABI. Final Dev object 22131 bytes, PCI owner 14440 and native
  blocking adapter 8540. No kernel linking/install/boot, discovery, PCI command,
  IRQ registration/unmasking, kproc startup or Dev publication was performed.
- After the overnight reconnect, the installed 199083-byte ISA agent still
  compares exactly with the retained original codec binary and is blocked in
  read, now PID 354 (earlier 356). This work did not restart it; the cause of
  the intervening PID change was not established. Its config/boot/PVE channel
  and stock driver list remain untouched; native Y is absent from that list.
  No power/clock/credential mutation, SSH-key/agent copy or broad home export.
  Diagnostic client used project-only read-only export and cleared environment;
  it is not network isolation. Final login exited cleanly and no project-export
  Drawterm client remained.

Next: bounded startup/recovery/context retention, native config integration and
recoverable clone kernel, then real hardware/host transport tests. Defined paths
are not present/usable on the current ISA VM; no working virtio driver is installed.

No passwords, keys, private addresses, or personal service configuration are
included in the public project. Deployment used a read-only project export in
an OS-isolated Drawterm environment, without home-directory or SSH-agent sharing.

## PC64 bootstrap/recovery and first full kernel test (2026-10-13)

- Added actual native opt-in startup, full ordered helper integration, and four
  fresh boot-pinned context bundles. Complete pool + conservative allocator
  slack reserved before DMA. Exactly one console PCI controller required;
  zero/multiple rejected. Publication requires actual runner admission.
- Extracted supervisor-only `vcpcownerquiesce`: close admission/IRQ, checked
  INTx masking/BME removal and exact deregistration without reset/free. Stop
  waits real worker/client drainage, withdraws Dev before reset, then uses the
  existing observed-reset release. Startup/drain retry bounds and pool exhaustion
  fail stopped/retain resources rather than recycling old objects.
- Added 625 actual supervisor-transition assertions with ordered lifecycle
  doubles, covering all 13 startup stages, delayed runner publication, online
  operation without arbitrary expiry, live-FD drain timeout, ambiguous PCI writes
  and fresh epoch recovery. Owner fixture now has 122 assertions including
  quiesce without reset/free and repeat deregistration guards. These are not
  actual MMIO, kernel scheduler or hardware tests.
- All 21 Linux C suites and 54 Python tests passed, with ASan/UBSan/leaks and
  GCC analyzer; final extra owner checks also passed sanitizer verification.
- Added build-only native `kernel/build-pc64.rc`/`pc64.mk`, using a separate
  generated pc64qga config and qga9kern source staging. Native mkdevlist filters
  hyphenated dependency names, so helpers get separate first-word misc lines.
  Complete kernel link passed with no warning/error after correcting this,
  file size 5515801 bytes. All 16 native suites passed again.
- New native checkout `/usr/glenda/src/qga9-virtio-boot-20261013`. Retained stock
  9fat boot image and plan9.ini copies there; both compared equal immediately
  before installation. Installed separate `/amd64/9pc64qga`, 0755 and exact cmp
  against its build. DID NOT replace stock `/n/9fat/9pc64`, plan9.ini, installed
  qga9 binary, trusted agent config/helper, or host ISA setting.
- User explicitly authorized live testing on this existing disposable VM.
  Before reboot ISA qga9 PID 354 was still blocking in read; PCI inventory had
  virtio network/SCSI only and no console controller. Set `#ec/qgavirtio` to 1
  for the kexec test and invoked `fshalt -r /amd64/9pc64qga`. It printed hjfs halt
  completion, then a drive-switch-off `device or object already in use` write
  error, then `rebooting...`. The VM's Drawterm port did not return in subsequent
  checks. **At that point kernel boot was unverified; the follow-up below
  resolved it.** Proxmox console/status requested;
  no live PCI ownership/DMA/IRQ or native Dev I/O success is claimed.
- Recovery: stock on-disk boot image/config untouched, so a full PVE Stop/Start
  should boot the stock image again. In-guest boot environment writes are not a
  persistent plan9.ini change. Preserve console output before recovery. The
  guest filesystem reported a halt before the reboot attempt; do not generalize
  this into a passed QGA shutdown/powerdown test.

### Follow-up: bootargs acknowledged, test kernel running (2026-10-13)

- Operator reported the VM was waiting for acknowledgement of the normal
  bootargs prompt, then requested reconnection. Isolated native login succeeded.
  This establishes the earlier unreachable interval was at a boot prompt, not
  evidence of a kernel panic. The full kmesg also contains that exact prompt.
- `#ec/bootfile` is `/bin/bootf` (fshalt's ramfs copy), `qgavirtio` is 1, and
  live `#c/config` includes all test modules. `/dev/drivers` registers `#Y
  vconsole`; kmesg names pc64qga.bootfs and reports `vconsole: expected one
  console PCI device, found 0; no device claimed`. PCI still has only virtio
  network/SCSI; no console. `#Y0/status` is absent as expected: no fake endpoint,
  controller/DMA acquisition or hardware verification inferred.
- Native no-device supervisor appears as PID 2, Broken. Inspected port/proc.c
  linkproc normally calls `pexit("kproc exiting", 0)` after a kproc callback
  returns, retaining an inspection state. No vconsole panic appears in kmesg;
  no live worker was started in this no-device path.
- ISA startup hook ran after the test reboot. New daemon observed at PID 354,
  264K/Pread (the PID was reused; not the old process surviving reboot). Installed
  binary still exactly compares with the codec build and native stdio ping/info
  pass at 0.5.1-dev. These diagnostics do NOT query the live PVE socket.
  UART remains 0600, 38400/8N1/no flow, with zero reported framing/overrun errors.
- Stock 9fat kernel and plan9.ini still compare equal to the retained backups;
  plan9.ini still selects stock 9pc64, with no qgavirtio/nobootprompt addition.
  Installed separate test kernel compares equal to the full build. No host-side
  transport configuration, guest agent config/binary or persistent boot-policy
  change was made during this follow-up.
- Remaining external step is provision the actual virtio-serial channel via
  Proxmox's normal agent option and a full cold start, while keeping console
  access/ISA rollback. The stock kernel will boot on that cold start; then the
  separate test kernel must be entered again. Native bootrc confirms that a
  test-only `#ec/nobootprompt` can hold full verified bootargs to avoid another
  manual acknowledgement; no such change was made yet.
- Follow-up rc session exited; only its verified project-isolated Drawterm
  client was then terminated locally after remaining idle. No guest process,
  desktop Drawterm client, broader filesystem export or credentials were touched.

## Still unverified

- Checked interrupt routing/registration/unmasking; polling is not IRQ evidence.
- Live PCI reset, deliberate timing/MMIO/DMA errors and hot unplug/replug.
- Native note, copy-fault, allocator/resource-exhaustion and competing-driver
  fault injection beyond the explicit native access/normal I/O checks.
- Full kernel/platform cache-ordering and callback lifetime safety.
- Automatic terminal-fault recovery on real hardware; fixtures cover ordering.
- Driver-driven userspace daemon restart after real owner retirement.
- Every implemented QGA command independently rerun over virtio; the supplied
  smoke results are not a full command conformance or power/clock test.
