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
and a bounded worker polls at 100 ms deadlines. Checked interrupt activation,
live fault/reset/unplug testing and full kernel safety validation remain pending.
Not a verified production release; see [STATUS](docs/STATUS.md).

## Linux development tests

```sh
make -C tests test
make -C tests sanitize
make -C tests analyze
```

The independent suite has 14 C suites and 13 boot-config Python methods. These
are host fixtures, not a native kernel build. No QGA checkout or libresolv needed.

## Native 9front build

From this checkout with the native amd64 toolchain and PC64 kernel sources:

```rc
mk test
mk kernel-check
rc kernel/build-pc64.rc
```

The first command runs 9 native-compatible C suites; the second compiles native
kernel-header checks. The last builds a separate
`/sys/src/9/pc64/9pc64qga`, without installing or rebooting.
Legacy `QGA_*` macros, test names, `qga9kern`, `9pc64qga` and `qgavirtio=1`
are retained for compatibility with the deployed image/configuration.
Neither default target writes boot files or enables hardware.
See [installation and rollback](docs/INSTALL.md) before explicit enablement.

## Documentation

- [VIRTIO](docs/VIRTIO.md): driver contracts, limits and implementation layers.
- [INSTALL](docs/INSTALL.md): explicit guest/Proxmox deployment and rollback.
- [DEVELOPMENT](docs/DEVELOPMENT.md): independent host/native checks and gates.
- [SECURITY](docs/SECURITY.md): memory, concurrency, DMA and access review.
- [STATUS](docs/STATUS.md): actual evidence versus unverified behavior.
- [PROVENANCE](PROVENANCE.md): extraction boundary and retained attribution.

Repository: [Szewcson/9front-virtio-serial](https://github.com/Szewcson/9front-virtio-serial),
branch `main`. MIT licensed with the original attribution retained.
