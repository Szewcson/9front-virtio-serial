# Virtio-serial verification status

Evidence through 2026-10-16. The driver is experimental and QGA-port-specific.

## Current repository checks

This change follows baseline `d7f689f` and adds native manuals, ordinary
include/build integration, native VirtIO enum reuse and optional checked IRQ
registration/activation with vector-filtered numeric telemetry.
Earlier local-only validation did not change the Proxmox guest. Its later
userspace update, alternate native kernel build and one-time warm kernel test
are recorded separately below, followed by checked-IRQ rollout and its cold-start gate.
The driver and [QGA repository](https://github.com/Szewcson/9front-qemu-guest-agent)
build/test independently with no sibling checkout dependency.

Linux tests passed 15 driver C suites and 22 boot/config/integration Python
methods. Post-split ASan/UBSan with leak checks and GCC `-fanalyzer` passed.
QGA separately passed 11 C suites and 54 Python methods with those checks.

Current ThreadSanitizer attempts on three concurrency fixtures failed runtime
initialization with `unexpected memory mapping`. This is neither a reported
code race nor a passing current TSan run. Earlier combined-tree fixture TSan
passes remain prior evidence, not current concurrency certification.

## Native Proxmox guest build (2026-10-15)

The reachable guest at `10.99.0.2` identifies itself as `p9lab`. A fresh separate
`/usr/glenda/src/virtio-serial-roadmap-20261015` checkout passed all nine native
C suites and all 17 PC64 header checks. The initial default full-build attempt
stopped before compilation: `vc_irqflags=()` made native rc's assignment-prefix
concatenation raise `null list in concatenation`. Previous full builds used the
nonempty checked-IRQ option and did not expose this default-path failure.

The helper now preserves one empty word with `vc_irqflags=''`; the host
integration fixture guards against reintroducing an empty list. The corrected
default path linked `/sys/src/9/pc64/9pc64roadmap15b` without warnings, using a
fresh name rather than overwriting the first attempt's retained configuration
and overlay. The output is 5520542 bytes, SHA-1
`4ee107539dec633ab0163358cff1eac215fecf81` (identity only). At this stage it was
staged, not installed or selected. Native source API files were not patched and
no boot file or running kernel was replaced; ordinary shared build objects may
have been regenerated. vconsole(3) installed/rendered without formatter errors.

The separate QGA repository's updated `0.6.0-dev` daemon and readiness helper
were built/tested and installed in that guest with verified rollback copies.
One detached owner survived a closed/reopened Drawterm session on the unchanged
polling kernel, identity 2/epoch 2/generation 1. Its sampler holds no data Chan,
and its clean service namespace contains no desktop export. No live cancellation,
port-removal or context-exhaustion fixture was run there; those would retire the
active management service. The operator subsequently supplied passing updated
PVE ping/info/network, CPU/timezone/routes, all six file operations and all five
exec checks. An independent read-only guest recheck retained the same owner,
identity/epoch and sampler descriptor isolation. This validates userspace on
the unchanged polling kernel, not the staged new kernel. A subsequent operator
host batch also returned OS/hostname/time, five disk/partition nodes and nonzero
load estimates. The next operator transcript, recorded on 2026-10-16, reports
normal PVE shutdown reaching `stopped`, a new start, early agent-not-running
probes and later successful `0.6.0-dev` ping/info with all 23 commands. It shows
eventual automatic agent availability, not measured startup latency, a direct
QGA-versus-ACPI path trace or filesystem integrity. The staged kernel had not
yet been activated; the later tests below supersede that state.

The first independent 2026-10-16 guest follow-up failed with Host Unreachable/No
route to host and made no guest changes. After operator route/VPN restoration,
Drawterm authenticated. Post-cold-boot inspection found one data owner (PID 292),
its sampler (293) without a data descriptor, matching installed executable text
and a service namespace without the Drawterm export. Identity 1/epoch 2/generation
1 was live. The boot log still identified the older known-good polling image,
fingerprint `cef52c141d9fa4dae81177fce3a43b0c5e6f3d9d`.

## Proxmox kernel tests and rollout (2026-10-16)

The FAT partition was inspected through a private read-only dossrv instance.
Byte-compared copies of its stock image, known-good polling image and plan9.ini
remain in `/usr/glenda/virtio-backup-20261016`. The staged build above was copied
to a new executable `/amd64/9pc64roadmap16`, compared and hashed. A clean native
`auth/newns /bin/fshalt -r /amd64/9pc64roadmap16` reported filesystem halt and
reboot. Reauthentication succeeded; the boot log identifies
`pc64roadmap15b.bootfs` and fingerprint
`334951c61aaa29f0c445c9534d60413458d20afb`.

The newer polling driver published one live context, identity 1/epoch 2/generation
1, with `irq-vector -1` and zero deliveries as expected for masked INTx. One
matching QGA agent and its isolated sampler started automatically. PIDs happened
to repeat after reboot; this is not survival of the old processes. No persistent
boot file/image was replaced during that test. The operator then supplied passing
PVE ping/info, CPU/timezone/routes, all six file operations and all five exec
checks on the newer polling kernel. In response to the Summary-page question,
the operator reported `10.99.0.2` and `fe80::be24:11ff:feb0:49fc`. This verifies
that explicit warm kernel boot and operator host smoke/IP reporting, not the
QGA reboot RPC or exhaustive filesystem integrity.
Local QEMU fault/IRQ results below remain separate evidence.

The optional checked-API patch was reviewed against this guest's actual irq.c
and fns.h. Host `git apply --check` and native `patch -n` passed. Original source
copies were byte-compared into the same backup directory before applying the
existing `integration/checked-intx.patch`. A separate native
`pc64roadmap16irq --checked-irq` build linked without warnings, output
`/sys/src/9/pc64/9pc64roadmap16irq`, 5521244 bytes, SHA-1 identity
`a774e2a124425981c01bb1d21b4c8af56b260289`.

A second one-time `fshalt -r` boot used an executable copy at
`/amd64/9pc64roadmap16irq` and a volatile kernel-environment `qgavirtioirq=1`.
Filesystem halt completed and reauthentication succeeded. The boot log identifies
`pc64roadmap16irq.bootfs`, fingerprint
`a298ce6814c44ce2a798d876c414902b0ed69a6a`, and checked INTx with health polling.
Vector 73 deliveries increased from 13 to 234 on identity 1/epoch 2/generation 1,
with the same owner PID 298. The separate sampler 299 holds no data descriptor,
and the owner namespace contains no Drawterm export. The operator supplied
passing info (all 23 commands), CPU/timezone/routes, six file operations and all
five exec checks including responsive ping. The separately pasted standalone
ping line is garbled; the script's ping result is unambiguous. This is actual
target IRQ delivery and host reconnect smoke, not merely registered-vector evidence.

After those checks, the verified checked image was copied to new short FAT name
`9qga16i` and compared. Persistent plan9.ini now selects it with both opt-ins and
the original complete nobootprompt/bootargs. Stock `9pc64` and previous polling
`9pc64qga` remain byte-equal to the backups, which include the original plan9.ini.
No Proxmox host hardware option changed. Recovery can restore the
verified old plan9.ini; a loader-only fallback to `9pc64qga` must also set
`qgavirtioirq=0`. Do not leave the checked opt-in with an unchecked fallback image.

The operator subsequently reported normal shutdown to `stopped`, full start,
successful ping/info and passing file/exec/query smoke tests. Independent
reauthentication observed BIOS `bootfile=9qga16i`, the checked fingerprint above,
vector 73 with 232 recognized deliveries, one matching owner PID 297 and sampler
298 without a data descriptor. Its namespace has no Drawterm export. Thus the
new persistent image cold-booted and the agent started automatically. The first
connection retry was refused before CPU service became available; startup time
was not measured. The operator's first pasted script omitted the requested
`--set-time`; a separate explicit rerun then passed files and supplied-time
clock/readback, reporting `1792131271868666048` ns. Delayed independent guest
and RTC reads both returned `1792131315`, within workstation timestamps
`1792131313`–`1792131316`, with timesync still running. The same owner/context
retained vector 73 and deliveries increased from 232 to 320. Time-sync policy,
agent configuration and the single hook remain unchanged. This is supplied-time
UTC agreement, not long-term/physical RTC, omitted-time PVE or power-failure proof.

## Current local native and hardware-path evidence

A separate local QEMU 10.0.13/TCG guest runs official 9front amd64 release 11952,
with verified torrent pieces, no NIC, shared directory or Drawterm export.
Inputs were read-only project snapshots and explicitly enumerated source updates
through a private test UART. Images/emulator/control tools stay outside both Git
repositories. The Proxmox VM/configuration was not changed.

Native testing exposed unsupported ANSI `#if` expressions; these became nested
native ifdef/ifndef directives with a structural regression. The revised tree
passed nine native C suites, all 17 PC64 header checks, actual source-overlay
export/configuration generation and a full checked-IRQ kernel link, without
warnings. The native checked-API patch dry-run and application both succeeded.

Test image `9pc64localirq`: 5511365 bytes, SHA-1 identity
`553e753c564f8e12e2c221bad2355405c8669aec` (not an authenticity proof).
Boot fingerprint: `71a0a88fc78eb378b6192b1a379588ea8423bd68`.
Only the disposable guest selects this image; stock kernel/config backups remain.
The BIOS loader needs an 8+3 FAT name: a verified copy named `9qgairq` subsequently
cold-booted automatically, with complete bootargs and no acknowledgement prompt.
vconsole(3) installed and rendered with native `man -n` without formatter errors.

Observed real QEMU paths, with `qgavirtio=1`, `qgavirtioirq=1` and MSI-X disabled:

- Context identity 1, MMIO epoch 2, vector 81: recognized IRQ deliveries rose
  from 3 to 81 during 20 synchronized backend reconnect/ping cycles; the same
  live context then passed binary files and all five exec smoke tests, reaching
  179 deliveries. Actual Ureg-vector filtering excludes unrelated spurious calls.
- With CPUs stopped, the verified controller's common status was observed as 15,
  explicitly reset through QEMU's private test interface, and observed as zero.
  On resume the old agent received a channel error and exited. A fresh context
  identity 2, epoch 2, vector 113 was published. After explicit agent restart,
  binary file/exec smoke tests passed; deliveries rose from 8 to 111.
- The exact QOM port `qgaport`, named `org.qemu.guest_agent.0`, nr 1, was removed,
  deletion verified, then re-added on the same controller/backend. The old agent
  received a channel error. Context identity 3, epoch 2, vector 121 was published;
  explicit agent restart and binary file/exec tests passed, with deliveries 4–98.
- With CPUs stopped and the device still at status 15, an out-of-range used ID
  `0xffffffff` was injected into the verified QGA RX queue 4. On resume the old
  agent received a channel error and context identity 4, epoch 2, vector 129 was
  published. Explicit agent restart and file/exec smoke tests passed, with IRQ
  deliveries 3–105 on that context. This tests one real malformed-completion
  fault, not every ring or reset-failure variant.

After a new cold boot, QGA-triggered powerdown halted the filesystem, emitted
QMP `SHUTDOWN` with `guest: true, reason: guest-shutdown`, and exited QEMU with
status 0. No QGA success reply was sent. A QEMU GLib timer diagnostic followed
halt; the stopped qcow2 image check found no errors. See the QGA STATUS for the
clock policy conflict and power diagnostics; reboot/failure paths remain open.

MMIO epoch is per controller context, not a globally increasing boot serial;
compare identity/epoch pairs. The boot log's `epoch N/4` counts retained contexts.
Polling alone was not accepted as IRQ evidence. These tests establish specific
emulated scenarios, not physical hardware, failed-reset/unreadable-MMIO behavior,
PCI-controller hot-unplug, exhaustive concurrency or cache/lifetime safety.

### Focused native lifetime follow-up

The new explicit native lifetime fixture compiled without warnings and passed
notes/read-fault/write-fault/notes in one cold boot. It is not part of default
build/test execution; see [RELIABILITY](RELIABILITY.md) for its coordinator protocol.

- Two notes runs each passed four interrupted reads and four interrupted writes,
  second-owner denial, nonfinal dup close and two exact binary round trips across
  a backend reconnect. The same owner remained live until final close.
- Named-port removal woke a verified pending read and a verified pending write
  without alarm expiry. Deliberately held data Chans retained the original
  identity/epoch and ownership reference until final clunk. A port I/O error
  need not mean controller-wide stopped state; the initial coordinator's stronger
  assumption was corrected, then the complete sequence passed.
- After explicit clunk, context identities 2/3 advanced to 3/4. Open old status
  descriptors retained their original Qid/identity while the new endpoint was
  published; the old snapshots subsequently reported retired/stopped state.
- Closing context 4 withdrew the endpoint and logged the explicit pool-exhausted
  diagnostic. The separate management channel stayed responsive. No fifth
  context or silent object recycling occurred. After clean filesystem halt and
  cold boot, the installed QGA helper automatically served all 23 commands.

This tests selected native cancellation/drain/lifetime paths, not arbitrary
delayed IRQ/copy/allocator or failed-reset scenarios. No kernel fix or new
production mechanism was needed.

The real boot helper subsequently served 20 synchronized reconnects with retained
file/job state, an abandoned 4096-byte reply followed by successful resync, and
the full synthetic file/exec smoke retest. The same identity 1/epoch 2/generation 1
was retained; checked IRQ deliveries rose from 3 to 199, then 293. Descriptor
inspection found one data owner and no management descriptor in its sampler.
The separate UTC-configured timesync remained running during delayed clock
readback, within one second of host time. No Proxmox VM/configuration changed.
Final agent-triggered powerdown halted the filesystem and produced the exact QMP
guest-shutdown event with QEMU exit status 0. Its post-halt GLib timer diagnostic
recurred; the stopped image's non-repair metadata check found no errors.

## Prior real guest and host evidence

The authorized test VM cold-booted `9pc64qga` through BIOS with complete
`nobootprompt` local bootargs. It automatically started `qga9-0.5.1-dev` on
`#Y0/data` using PVE's normal virtio QGA channel. The daemon had no UART
descriptors. Stock `9pc64` and byte-verified boot/config/ISA rollback copies
were retained.

Native binary stream I/O and a private child changed to `none` verified denial
for both data and status without changing the parent identity.
The operator supplied successful PVE info, six-operation binary file round-trip
and all five exec smoke tests: literal argv, binary stdin, merged output,
responsive ping and one-shot reaping. These are real operator host results,
not an independent developer PVE session or full command conformance.

Guest inspection confirmed the same owner/epoch after PVE's per-RPC backend
socket reconnects. Host close preserves Chan, RX and handles; positive writes
wait for reconnect. Guest close/removal/fault still requires drained full reset.
Stream and broker fixtures covered repeated backend cycles with pending binary I/O.

INTx remains masked and the worker uses 100 ms polling deadlines.
A registered IRQ row proves no usable routing or live interrupt behavior.

## Boot and rollback caveats

A warm reboot using the FAT copy failed after filesystem halt because that file
lacked execute permission. A full host Stop/Start subsequently booted it through
BIOS. Use executable `/amd64/9pc64qga` for warm kexec, not the FAT image.
An earlier network-unreachable interval was resolved as a bootargs prompt, not
a demonstrated kernel panic.

The stock `can't find bus type 12, number 0` MP/PCI interrupt-routing warning
also appeared with stock kernel. Preserve diagnostics; do not disable global
ACPI/APIC/MP to hide it. Keep INTx masked until delivery is checked.

The explicit boot helper preserves the stock image/config and adds a separate
FAT image. Configuration replacement is not atomic; verified backups and console
recovery remain necessary. See [INSTALL](INSTALL.md).

## Focused remaining acceptance

- Userspace readiness: fixed locally in the QGA helper with bounded stat-only
  waits before one launch. Native ready/delayed/absent fixtures passed locally
  and on the Proxmox guest. The updated helper is installed there and launched
  once. Operator host queries and a normal PVE shutdown/start with eventual
  automatic agent availability subsequently passed. Post-boot owner/namespace
  inspection passed after route restoration; no startup timing was supplied.
- Native manual, source-overlay export and configuration/mkfile patch are
  prepared. Sources compile directly without generated wrappers and use native
  VirtIO enums with ABI guards. Host manifest/configuration/patch checks and
  actual native export/compiler/linker execution passed locally.
- The operator confirmed the Proxmox VM offline on 2026-10-14. It became reachable
  on 2026-10-15; new native builds/userspace deployment are recorded above, not
  conflated with the earlier local hardware-path tests.
- New polling and checked-IRQ kernels passed explicit warm boots with automatic
  agent startup and operator PVE file/exec/query tests. Checked vector 73 showed
  increasing real deliveries. The verified checked image is selected persistently;
  its full cold-start/automatic-agent/PVE smoke acceptance passed, with older
  images/backups retained.
- The optional checked-API patch and unmask path passed host fixtures, including
  failed registration and actual-vector filtering. Native compilation and real
  emulated interrupt delivery passed locally and on the target warm boot;
  polling is not IRQ evidence. [IRQ](IRQ.md) describes activation and validation.
- Forced status-zero reset, invalid used ID, pending-I/O named-port removal,
  native note cancellation, delayed final clunk, stale metadata views and
  bounded pool exhaustion passed locally. Explicit cold boot restored service.
- Updated PVE userspace ping/info/network, CPU/timezone/routes, file and exec
  smoke checks passed on operator evidence with the unchanged polling kernel.
  OS/hostname/time/disk/load host queries also returned successfully. The operator
  reported both IPs in response to the Summary-page question. Supplied-time
  setter and delayed UTC agreement passed with running timesync. QGA
  reboot/failure tests remain open.
  The separate earlier normal PVE shutdown/start passed on operator
  evidence; it is not a direct shutdown-path or filesystem-integrity proof.

General PCI-controller unplug, unreadable MMIO/failed reset, other invalid DMA
variants, interrupt storms, copy/allocator faults and competing-driver injection
remain unverified residual risks. Full physical cache-ordering/callback lifetime
safety is not certified. Adding new drivers, general hardware portability or
extensive emulator fault infrastructure is outside the revised milestone;
existing conservative resource/lifetime contracts must still be preserved.

Four context epochs remain boot-pinned and cannot be recycled. Retry limits
are not hard scheduling/resource deadlines; exhaustion or failed drainage can
require reboot. Fixtures do not prove safety against a malicious host controlling
guest RAM. [SECURITY](SECURITY.md) and [VIRTIO](VIRTIO.md) describe these contracts.
