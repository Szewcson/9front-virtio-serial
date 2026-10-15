# Development and verification

## Repository boundary

This repository owns `kernel/`, virtio fixtures/native header checks, the
boot-config converter and kernel build/enable helpers. It does not include QGA
handlers, JSON/base64 codecs, an agent binary or service startup configuration.
No sibling repository is required for its build or tests.
Legacy QGA-prefixed test binaries/macros and `9pc64qga`/`qgavirtio` boot names
are retained for compatibility, not userspace build dependencies.

## Layers

- `vconsole.[ch]`: bounded PCI/ring/control snapshots and named-port ownership.
- `vconsole-mmio.[ch]`, `vconsole-pc.[ch]`: checked modern MMIO/reset/features,
  actual native mappings and coherent PC64 allocation/address callbacks.
- `vconsole-dma.[ch]`: private split-queue arenas, immutable used snapshots,
  leases/receipts and confirmed-reset-gated release.
- `vconsole-irq.[ch]`, `vconsole-wake.[ch]`: locked IRQ mailbox, pinned native
  wake targets, checked vector gating and numeric delivery telemetry; default
  hardware policy remains polling with INTx masked.
- `vconsole-pump.[ch]`, `vconsole-stream.[ch]`: single-worker control/data
  processing, bounded FIFO/backpressure; host reconnect preserves guest ownership.
- `vconsole-controller*.[ch]`, `vconsole-worker*.[ch]`,
  `vconsole-owner-pc.[ch]`: budgeted acquisition, native PCI/kproc lifecycle,
  exact references, last-access join and reset/teardown gates.
- `vconsole-io*.[ch]`, `devvconsole.c`: private staged requests, cancellation/
  note/user-copy cleanup, eve-only exclusive data Chan and read-only status.
- `vconsole-boot-pc.c`: opt-in supervisor, fresh boot-pinned epochs,
  reference-gated publication/withdrawal/recovery.
- `build-pc64.rc`, `pc64.mk`: separate full native kernel build.
- `integration/`, `scripts/export-upstream.rc`, `man/3/vconsole`: native source
  layout, explicit header rules/configuration patch and device manual.
- `boot-config.awk`, `enable-pc64.rc`: conservative local boot conversion,
  verified backups, explicit installation without reboot.

## Linux checks

From this repository:

```sh
make -C tests test
make -C tests sanitize
make -C tests analyze
```

Linux runs 15 C suites and 21 boot/config/integration Python methods. It requires
C99, pthreads, make, awk, git (patch fixtures) and Python, not libresolv or QGA.
These are simulated hardware/kernel-primitive tests, not a native kernel build.
Sanitizer targets use separate output and ASan/UBSan/leak checks.
Runtime startup may require an approved environment; failed sanitizer startup
is not a passing test. GCC analyzer output is not proof of memory safety.

A separate ThreadSanitizer example:

```sh
make -C tests OUT=../build/tsan \
  CFLAGS='-O1 -g -std=c99 -Wall -Wextra -Werror -fsanitize=thread -fno-omit-frame-pointer -fno-pie' \
  LDFLAGS='-fsanitize=thread -no-pie' \
  ../build/tsan/qga9-vconsole-io-test ../build/tsan/qga9-vconsole-dev-test
./build/tsan/qga9-vconsole-io-test
./build/tsan/qga9-vconsole-dev-test
```

This checks host fixtures/locks, not actual native DMA memory ordering.
Current TSan attempts failed runtime initialization with unexpected memory
mapping; see [STATUS](STATUS.md). The recipe is not a passing-run claim.

## Native checks

```rc
mk test
mk kernel-check
rc kernel/build-pc64.rc
```

Native tests run 9 portable C suites. `kernel-check` compiles 17 objects
against actual PC64 headers; the full build additionally validates kernel link.
Native post-split checks have not been rerun.
The optional `mk qga9-native-vconsole-access` binary requires a live #Y0
endpoint and explicitly tests denial to a private child changed to none; it is
not run automatically. No default target installs, enables or reboots.

Test shims keep the same relative `tests/` / `kernel/` topology as before
the split. Neither production nor tests include files from the QGA repository.
Pure state/rings are worker-owned; shared state has explicit locks. Build changes
must preserve fixed capacities, immutable DMA snapshots, generation checks,
exact Chan identity and reset-before-release lifetimes. Read
[SECURITY](SECURITY.md) before changing these contracts.

## Release gates

Preserve a stock kernel and console recovery. Record real capability/layout,
exact named port, exclusive owner, binary I/O and repeated host reconnect.
Run the separate QGA host scripts via PVE's synchronized client; do not race a
second socket reader. Check cold startup and real reset/error/hot-unplug behavior.
The revised QGA helper has a bounded readiness wait for asynchronous #Y
publication; its native execution/deployment is still pending.
Masked INTx with polling is not IRQ evidence. The optional checked-API patch and
activation path are locally tested with mocked native primitives, not native
hardware. See [IRQ](IRQ.md) for the required distinction and live checks.
See [STATUS](STATUS.md) for executed evidence versus missing gates.
The native manual and upstream-layout package are prepared; see
[UPSTREAM](UPSTREAM.md). Native export/build/link is pending while the VM is
offline. Driver-neutral compatibility names remain a future migration decision.
