# Virtio-serial verification status

Evidence through 2026-10-15. The driver is experimental and QGA-port-specific.

## Current repository checks

This change follows baseline `d7f689f` and adds native manuals, ordinary
include/build integration, native VirtIO enum reuse and optional checked IRQ
registration/activation with vector-filtered numeric telemetry.
No Proxmox guest binary, kernel, helper or boot configuration was changed.
The driver and [QGA repository](https://github.com/Szewcson/9front-qemu-guest-agent)
build/test independently with no sibling checkout dependency.

Linux tests passed 15 driver C suites and 22 boot/config/integration Python
methods. Post-split ASan/UBSan with leak checks and GCC `-fanalyzer` passed.
QGA separately passed 11 C suites and 54 Python methods with those checks.

Current ThreadSanitizer attempts on three concurrency fixtures failed runtime
initialization with `unexpected memory mapping`. This is neither a reported
code race nor a passing current TSan run. Earlier combined-tree fixture TSan
passes remain prior evidence, not current concurrency certification.

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

## Open findings and release gates

- Userspace readiness: fixed locally in the QGA helper with bounded stat-only
  waits before one launch. Native ready/delayed/absent fixtures passed;
  Proxmox deployment remains pending and
  earlier installed helpers may still have the one-shot race.
- Native manual, source-overlay export and configuration/mkfile patch are
  prepared. Sources compile directly without generated wrappers and use native
  VirtIO enums with ABI guards. Host manifest/configuration/patch checks and
  actual native export/compiler/linker execution passed locally.
- The operator confirmed the Proxmox VM offline on 2026-10-14; no updated native
  run or deployment on that VM is claimed. Local evidence is separate above.
- Local native post-split build and basic integration passed; updated Proxmox
  deployment and verification remain pending.
- The optional checked-API patch and unmask path passed host fixtures, including
  failed registration and actual-vector filtering. Native compilation and real
  emulated interrupt delivery passed locally; polling is not IRQ evidence. [IRQ](IRQ.md)
  describes the prepared code and live-validation procedure.
- Forced status-zero reset, invalid used ID and named-port unplug/re-add passed locally.
  PCI-controller unplug, failed reset, unreadable MMIO, other invalid DMA completions,
  interrupt storms and deliberate timing faults remain untested live.
- Native note/copy/allocator exhaustion and competing-driver injection.
- Full native cache-ordering, callback/Chan/Rendez lifetime and kernel safety.
- Real terminal-fault recovery and safe daemon restart after owner retirement.
- Command-by-command host conformance and PVE UI; local virtio powerdown/clock
  tests passed, but updated PVE acceptance and reboot/failure tests remain open.

Four context epochs remain boot-pinned and cannot be recycled. Retry limits
are not hard scheduling/resource deadlines; exhaustion or failed drainage can
require reboot. Fixtures do not prove safety against a malicious host controlling
guest RAM. [SECURITY](SECURITY.md) and [VIRTIO](VIRTIO.md) describe these contracts.
