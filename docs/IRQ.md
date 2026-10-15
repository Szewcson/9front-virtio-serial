# Experimental checked INTx

The default build keeps INTx masked and polls. This document describes prepared
code and future validation, not passing native/hardware results. The last
verified Proxmox guest still used the polling driver.

## Build and activation

Stock native `intrenable` returns void and cannot confirm that a route was
installed. `integration/checked-intx.patch` factors its existing implementation
into a shared helper and adds `intrenablechecked`, returning the installed
vector or -1. The original void API and its allocation-failure policy remain
available to existing callers. The checked entry returns allocation failure.

The patch touches native `sys/src/9/pc/irq.c` and `sys/src/9/pc64/fns.h`.
It is not applied automatically or included by merely exporting this driver.
Review it against a separate native source checkout; where ordinary Git is
available, run `git apply --check` before applying it. Its context was checked
against inspected upstream source, not compiled into a tested native kernel.
Do not apply it blindly to another 9front revision.

With that patch already present in the native source tree, run from this repo:

```rc
rc kernel/build-pc64.rc pc64vconsoleIrq --checked-irq
```

Choose an unused config/output name. This adds `VCONSOLE_CHECKED_IRQ` for the
driver objects and builds only; it does not install, edit boot files or reboot.
A missing native prototype/implementation must fail the build, not silently
fall back to unchecked unmasking.

Explicitly installing/booting that test kernel needs the backup and console
recovery procedure in [INSTALL](INSTALL.md). Set both trusted boot options
`qgavirtio=1` and `qgavirtioirq=1`. The latter is not added by the default boot
converter. An ordinary build refuses the checked opt-in instead of unmasking.

Successful checked registration binds a vector under the IRQ mailbox lock.
Startup enables BME while masked, establishes DRIVER_OK, then performs a checked
INTx unmask write/readback. An ambiguous failure retains claims and objects;
best-effort masking does not prove that faulty hardware stopped interrupting.
Keep a console and host Stop/Start recovery path.

## Evidence required on a disposable VM

1. Run the independent native suites, kernel-header checks and full checked
   kernel link. Record source revision, kernel identity and boot configuration.
2. Inspect `cat '#Y0/status'`: require a vector from 32 through 255. Save identity,
   epoch, owner-generation and irq-deliveries. A registered vector alone is not
   interrupt evidence.
3. Start exactly one QGA consumer, generate repeated real host requests, and
   re-read status. Require irq-deliveries to increase on the same live context.
   The callback verifies actual Ureg vector before reading ISR, so native
   unrelated/spurious fallback dispatch cannot count as delivery. Polling or
   software fault wakes do not count. This verifies the emulated device/IRQ
   path, not physical hardware or all native cache/lifetime contracts.
4. Repeat backend socket reconnects and binary file/exec tests. Host reconnect
   must retain the same guest owner/epoch and must not masquerade as reset.
5. Close the guest's final owner, observe withdrawal and actual full-reset
   success before a fresh epoch is published, then explicitly restart one
   daemon. The kernel is not an agent restart supervisor. Four context epochs
   are boot-pinned; exhaustion may require reboot.
6. For port removal/re-add, inspect actual QMP device/QOM IDs first and select
   the exact named QGA port, not a guessed controller or unrelated device.
   Use a disposable image and console. Check withdrawal, pending-I/O behavior,
   reference drain and reset before claiming recovery. Do not treat a host
   command returning success as proof that guest DMA drained.
7. Controller unplug and deliberately unreadable MMIO, invalid completion,
   reset failure or interrupt storms need separate controlled scenarios.
   Several require an instrumented QEMU/device fault fixture. CPU-side mocks
   cover failure branches but do not satisfy live validation. Retaining a
   failed claim until reboot is not successful hotplug recovery.

No steps above have completed on the revised checked-IRQ kernel. The local
QEMU environment began serial boot only; no native test result is claimed.
See [STATUS](STATUS.md) and [SECURITY](SECURITY.md) for the remaining gates.
