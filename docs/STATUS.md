# Virtio-serial verification status

Evidence through 2026-10-15. The driver is experimental and QGA-port-specific.

## Current repository checks

This change follows baseline `d7f689f` and adds native manuals, ordinary
include/build integration, native VirtIO enum reuse and optional checked IRQ
registration/activation with vector-filtered numeric telemetry.
No Proxmox guest binary, kernel, helper or boot configuration was changed.
The driver and [QGA repository](https://github.com/Szewcson/9front-qemu-guest-agent)
build/test independently with no sibling checkout dependency.

Linux tests passed 15 driver C suites and 21 boot/config/integration Python
methods. Post-split ASan/UBSan with leak checks and GCC `-fanalyzer` passed.
QGA separately passed 11 C suites and 54 Python methods with those checks.

Current ThreadSanitizer attempts on three concurrency fixtures failed runtime
initialization with `unexpected memory mapping`. This is neither a reported
code race nor a passing current TSan run. Earlier combined-tree fixture TSan
passes remain prior evidence, not current concurrency certification.

The split native mkfiles define 9 driver C suites and 17 kernel-header objects.
Native build/VM checks have not been rerun after the split.
Prior combined native builds passed all 16 C suites and linked the complete
revised kernel without warnings. The latest pre-split linked image was 5515658
bytes. Those results do not establish the revised independent build rules.

A separate local QEMU 10.0.13/TCG environment was prepared from official 9front
release 11952, with verified torrent pieces, no NIC or shared directory, and
read-only project-source snapshots. Serial boot began, but no completed native
build/test result was obtained. Images/emulator packages are outside both Git
repositories. This does not satisfy any native/IRQ/reset/unplug release gate.

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
  waits before one launch. Native regression and deployment remain pending;
  earlier installed helpers may still have the one-shot race.
- Native manual, source-overlay export and configuration/mkfile patch are
  prepared. Sources compile directly without generated wrappers and use native
  VirtIO enums with ABI guards. Host manifest/configuration/patch checks passed,
  not actual native exporter/compiler/linker execution.
- The operator confirmed the VM offline on 2026-10-14; no new native run or
  deployment is claimed.
- Native post-split build/tests and VM integration.
- The optional checked-API patch and unmask path passed host fixtures, including
  failed registration and actual-vector filtering. Native compilation and real
  interrupt delivery remain unverified; polling is not IRQ evidence. [IRQ](IRQ.md)
  describes the prepared code and live-validation procedure.
- Live PCI/DMA reset, intentional MMIO/timing faults and unplug/replug.
- Native note/copy/allocator exhaustion and competing-driver injection.
- Full native cache-ordering, callback/Chan/Rendez lifetime and kernel safety.
- Real terminal-fault recovery and safe daemon restart after owner retirement.
- Command-by-command host conformance, PVE UI and agent power/clock tests over virtio.

Four context epochs remain boot-pinned and cannot be recycled. Retry limits
are not hard scheduling/resource deadlines; exhaustion or failed drainage can
require reboot. Fixtures do not prove safety against a malicious host controlling
guest RAM. [SECURITY](SECURITY.md) and [VIRTIO](VIRTIO.md) describe these contracts.
