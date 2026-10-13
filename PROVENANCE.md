# Extraction provenance

Split on 2026-10-13 from the working tree of
`9front-qemu-guest-agent`, whose existing remote is
`git@github.com:Szewcson/9front-qemu-guest-agent.git` and committed HEAD was
`e1375fc` (initial commit). The driver/kernel sources and driver tests were
untracked work, not files with a committed history to extract. The original
repository/history and unrelated dirty userspace changes were retained.
The original QGA commits were not rewritten, staged or pushed. This repository
uses the operator-supplied `git@github.com:Szewcson/9front-virtio-serial.git`
remote, with a separate initial driver import on `main`.

Moved byte-identically: all 37 `kernel/` files, 30 C/header driver tests/fixtures
(`tests/vconsole-*`, `tests/native-vconsole-*`) and `tests/test_boot_config.py`.
Copied the original MIT license verbatim, preserving copyright attribution.
Build definitions were split by ownership, and driver documentation/evidence/
review was extracted and updated. QGA retains its startup/service and PVE
integration scripts; this repository does not duplicate them.

No guest secrets, private host files, SSH material, compiled kernel or binaries
were imported. Build artifacts remain ignored. Historical evidence may mention
old combined checkout names/counts: these are not current dependencies.
