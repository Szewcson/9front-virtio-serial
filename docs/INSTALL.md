# Explicit installation and rollback

These are instructions, not default build actions. Keep a VM console, verified
stock kernel/configuration and host access for recovery. The existing test VM
already has virtio enabled; the repository split does not require reinstalling.

## Build and inspect

On native 9front amd64, from this driver checkout:

```rc
mk test
mk kernel-check
rc kernel/build-pc64.rc
```

The build derives `pc64qga` from stock PC64 configuration, stages helper sources
under `/sys/src/9/pc64/qga9kern` and links `9pc64qga`; stock `pc64` and
`9pc64` are not replaced. Common kernel build objects may be regenerated.
The kernel configuration must exclude competing drivers for the selected PCI
console controller. Exactly one supported console controller is required.

Copy the verified output to a NEW `/amd64/9pc64qga` only if that target does not
already exist; for an existing deployment, explicitly back up and compare the
installed image before a deliberate update. Preserve execute permission for
warm `fshalt -r /amd64/9pc64qga` tests. A FAT boot image lacking execute permission
is readable by BIOS but not a valid executable warm-reboot source.

## Host channel

After arranging the guest kernel/service transition, use PVE's normal agent
option; do not add a competing custom socket/channel:

```sh
qm set <VMID> --agent enabled=1,type=virtio,freeze-fs=0,fstrim_cloned_disks=0
qm showcmd <VMID> --pretty
```

This hardware change requires a controlled full Stop/Start. Keep filesystem
freeze and automatic trim disabled: the separate QGA agent does not implement
those commands. PVE supplies the port name `org.qemu.guest_agent.0`.

## Persistent boot

Mount the verified boot partition at `/n/9fat` using the site's normal procedure.
With a tested executable `/amd64/9pc64qga`, run explicitly from this checkout:

```rc
rc kernel/enable-pc64.rc NEW-BACKUP-DIRECTORY
```

Substitute a concrete new backup directory. The script refuses an existing
backup or alternate FAT image; it byte-checks stock kernel/config backups before
writing. The conservative converter accepts only one local boot entry selecting
stock `9pc64`; menus/unknown images/ambiguous arguments fail without generated
config output. It adds separate `9pc64qga`, sets `bootfile=9pc64qga`,
`qgavirtio=1` and `nobootprompt` to the FULL verified local bootargs.
It does not reboot. FAT config replacement is not atomic; verified backups and
the untouched stock image are the recovery path.

After a controlled cold start, inspect:

```rc
cat '#ec/bootfile'
cat '#Y0/status'
```

Configure/start the agent using the separate QGA repository's
`scripts/start.rc` and `scripts/service-virtio.example`, only once and from a
clean native service namespace. No desktop filesystem export or SSH keys are
needed. Fixed driver data/status permissions are eve-only; do not chmod #Y.

## Host smoke tests

Use the scripts from the separate QGA repository on PVE:

```sh
qm agent <VMID> ping
qm agent <VMID> info
perl ./test-proxmox.pl <VMID>
perl ./test-proxmox-exec.pl <VMID>
```

They use synthetic test data, not existing host files. The file script retains
its guest fixture. Do not use a second raw socket reader alongside PVE.
Repeated host backend connections must retain the same guest owner/epoch;
only actual guest retirement/removal/fault follows reset/drain rules.

## Rollback

Stop only the exact QGA service process. Restore the byte-verified stock
`plan9.ini` backup (which selects untouched `9pc64`) and the verified ISA QGA
service/helper configuration. Restore PVE's `type=isa` agent option with the
other safety flags unchanged, then perform a controlled full Stop/Start.
If already stuck or filesystem-halted, use the PVE console/host recovery path;
do not attempt an unverified in-guest write. Keep alternate images and backups
until rollback is verified. Do not blindly overwrite a running executable.

The stock `can't find bus type 12, number 0` warning is PCI interrupt routing;
it is not by itself a boot failure. The tested driver intentionally leaves
INTx masked. Do not disable global ACPI/APIC/MP merely to hide the warning.
