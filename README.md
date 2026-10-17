# 9front virtio-serial

An experimental native PC64 virtio-console/serial kernel driver for 9front,
exposing the exact nonconsole `org.qemu.guest_agent.0` port as an exclusive,
eve-only binary `#Y[0..3]/data` stream plus read-only `status`.

This repository was split out of
[9front-qemu-guest-agent](https://github.com/Szewcson/9front-qemu-guest-agent). It owns the
kernel modules, build/boot helpers and driver tests, not the QGA daemon, JSON or
base64 codecs. Both repositories build and test independently; the installed
agent consumes the driver through the native stream interface. This is currently
a QGA-focused driver, not a general arbitrary-port userspace API.

Real modern-capability PCI transport, automatic cold boot and operator-supplied
PVE binary file/exec smoke tests passed on the authorized VM. INTx remains masked
and a bounded worker polls at 100 ms deadlines in the default build. An optional
checked-IRQ kernel patch, opt-in activation and vector-filtered delivery counters
passed native builds and actual emulated IRQ delivery in a separate local
QEMU guest. Forced device-status reset and exact named-port removal/re-add
recovered into fresh contexts, followed by passing binary file/exec smoke tests.
PCI-controller unplug, broader fault injection and full kernel safety validation
remain pending.
Not a verified production release; see [STATUS](docs/STATUS.md).

## Linux development tests

```sh
make -C tests test
make -C tests sanitize
make -C tests analyze
```

The independent suite has 15 C suites and 22 boot/config/integration Python
methods. These are host fixtures, not a native kernel build. No QGA checkout
or libresolv needed.
Post-split ASan/UBSan/leak checks and GCC analyzer passed. Current TSan attempts
failed runtime initialization; see [STATUS](docs/STATUS.md) for the distinction.

## Native 9front build

From this checkout with the native amd64 toolchain and PC64 kernel sources:

```rc
mk test
mk kernel-check
rc kernel/build-pc64.rc
```

The first command runs 9 native-compatible C suites; the second compiles native
kernel-header checks. The last builds a separate
`/sys/src/9/pc64/9pc64qga`, refusing existing configuration/output names, without
installing or rebooting. Supply a new config name if the default already exists.
The nine native suites, all 17 kernel-header checks and a complete checked-IRQ
kernel link passed locally. On the Proxmox guest the nine suites, 17 header
checks and default polling-kernel link also passed after fixing the native rc
empty-list argument path. That kernel then passed a one-time native warm boot
and automatic agent startup; persistent cold boot still selects the older
known-good image during that test. A subsequent checked-IRQ warm boot passed
real vector-73 delivery and operator PVE file/exec/query tests. The checked image
is now selected persistently and passed full cold boot, automatic agent startup
and operator PVE smoke tests, with verified previous images/config retained.
See STATUS for exact evidence and remaining gates.
Legacy `QGA_*` macros, test names, `9pc64qga` and `qgavirtio=1`
are retained for compatibility with the deployed image/configuration.
Neither default target writes boot files or enables hardware.
The build now uses ordinary sources, not generated C wrappers.
Explicit `mk install-man` installs [vconsole(3)](man/3/vconsole), refusing an
existing manual. [UPSTREAM](docs/UPSTREAM.md) describes the native source overlay,
configuration/mkfile patch and checked native-constant reuse.
See [installation and rollback](docs/INSTALL.md) before explicit enablement.

## Documentation

The active milestone is focused PC64 reliability, not new hardware mechanisms;
see [RELIABILITY](docs/RELIABILITY.md) for acceptance checks and the explicit
disruptive native lifetime fixture.

- [VIRTIO](docs/VIRTIO.md): driver contracts, limits and implementation layers.
- [UPSTREAM](docs/UPSTREAM.md): prepared native port/pc layout and build integration.
- [IRQ](docs/IRQ.md): experimental checked routing and live-validation gates.
- [INSTALL](docs/INSTALL.md): explicit guest/Proxmox deployment and rollback.
- [DEVELOPMENT](docs/DEVELOPMENT.md): independent host/native checks and gates.
- [SECURITY](docs/SECURITY.md): memory, concurrency, DMA and access review.
- [STATUS](docs/STATUS.md): actual evidence versus unverified behavior.
- [PROVENANCE](PROVENANCE.md): extraction boundary and retained attribution.

Repository: [Szewcson/9front-virtio-serial](https://github.com/Szewcson/9front-virtio-serial),
branch `main`. MIT licensed with the original attribution retained.
