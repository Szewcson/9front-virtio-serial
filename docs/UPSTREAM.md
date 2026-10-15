# Native kernel integration

This is a prepared source overlay, not an upstream submission or a new booted
kernel. Production sources compile directly: no generated C wrappers, absolute
PC64 include paths or dependency on the QGA daemon. The standalone `kernel/`
tree remains the source of truth; export is an explicit build artifact.

From this checkout on 9front, choose a **new** directory:

```rc
rc scripts/export-upstream.rc /tmp/vconsole-upstream
```

The explicit manifests export 17 C files and 16 private headers:

```text
sys/src/9/port/   Dev, checked core, private headers
sys/src/9/pc/     PCI/MMIO, wake, kproc, ownership and boot adapters
sys/src/9/pc64/   vconsole.mk
sys/man/3/       vconsole
```

For an upstream development checkout, review/copy those new files, append
`<vconsole.mk` to its `sys/src/9/pc64/mkfile`, and generate a new configuration
from its stock `pc64` using the supplied manifests and converter:

```rc
awk -f integration/config.awk integration/port.modules integration/pc.modules /sys/src/9/pc64/pc64 >NEW-CONFIG
```

Run the converter from this repository; replace `NEW-CONFIG` with an unused
file in the owning pc64 build directory. It emits a native `dev` entry and
ordinary `misc` module entries, not source wrappers or link registration.
Native `mkdevlist` treats hyphens in dependency fields as flags, so the retained
module names must be first `misc` fields, not dependencies on the `dev` line.
It rejects missing/duplicate `dev`/`misc` sections and an already integrated driver
before printing configuration. Keep new files and inspect failures; the shell
may have created an empty output file. Do not overwrite stock configuration.

Alternatively, `integration/pc64.patch` adds the same dev/misc entries and
mkfile include to an ordinary native source checkout. Review it with
`git apply --check` before applying it there. The patch and converter are
regression-checked against small inspected native contexts; no upstream checkout
was changed and no submission was made. It intentionally enables only PC64.

The fragment extends normal native rules, compiles port/pc sources with owning
architecture headers, and tracks all private headers and native `virtio10.h`.
The generic-looking core still belongs to a **PC64-only** driver; do not enable
it on other architectures without porting the MMIO/DMA/lifetime contracts.
The kernel is inactive unless the trusted boot configuration sets `qgavirtio=1`.

For a build without copying into the stock source directories:

```rc
rc kernel/build-pc64.rc pc64vconsoleTest
```

This refuses existing configuration/output names, exports a new
`/sys/src/9/pc64/vconsole-src-$pid` overlay and builds
`/sys/src/9/pc64/9pc64vconsoleTest` with the same source rules. Ordinary shared
kernel objects can be regenerated. No kernel install, boot edit or reboot is
performed. The default name remains `pc64qga`, if unused. Existing deployment
names `9pc64qga` and `qgavirtio=1` remain compatible.

An independent optional `integration/checked-intx.patch` adds a checked native
IRQ API. It is never applied by the exporter. `--checked-irq` as the second
build-helper argument enables its driver call sites; runtime unmasking also
requires explicit `qgavirtioirq=1`. See [IRQ](IRQ.md) for review, build and
validation requirements. Default builds retain masked INTx and polling.

The manifests, configuration converter and build dependencies have host
regressions, and actual native export/build/link passed in the isolated local
QEMU guest. Production C uses native-supported conditional preprocessing;
ANSI `#if` expressions are rejected by a structural regression.
Before submission, resolve the hardware gates in [STATUS](STATUS.md), decide
with upstream whether the QGA-specific `#Y` API/names are appropriate, and
review whether this decomposition should be combined further. This package
does not imply upstream acceptance or a production-ready driver.

Native register/status values come from `port/virtio10.h` with ABI size checks.
Checked capability mapping, DMA snapshots and reference/reset gates remain
local: native helpers do not provide equivalent validation/lifetime contracts.
The layout follows the inspected native [PC64 build](https://github.com/9front/9front/blob/front/sys/src/9/pc64/mkfile)
and [port rules](https://github.com/9front/9front/blob/front/sys/src/9/port/portmkfile).
The module-name rule was checked in native
[mkdevlist](https://github.com/9front/9front/blob/front/sys/src/9/port/mkdevlist).
