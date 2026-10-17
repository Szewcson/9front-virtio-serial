# Focused PC64 reliability milestone

The revised scope is the existing single-controller QGA port on coherent PC64,
not new drivers, general PCI hot-unplug or every possible device fault. Preserve
the ownership/reset contracts; narrower scope does not make unsafe reclamation
acceptable or prove absence of untested races. See STATUS for executed evidence.

## Acceptance checks

1. Cold boot publishes one endpoint. The separate QGA helper launches once,
   survives its launching shell and serves the actual Proxmox client.
2. Backend reconnect keeps the same identity/epoch/owner. Interrupted native
   transfers abandon only their own request, not a still-borrowed buffer.
3. Pending read/write receives an error on named-port removal. A live data Chan
   prevents replacement/reset until final clunk; a nonfinal dup close does not
   retire ownership. Do not require the entire controller already to be stopped
   when a removed port first returns its I/O error.
4. After final close, recovery publishes a fresh identity/epoch pair. An opened
   old status descriptor keeps its old Qid/identity instead of following the new
   pathname. Retaining CPU-only metadata must not prevent fresh publication.
5. After four context retirements, the endpoint disappears and the kernel logs
   exhaustion/reboot required. There is no fifth allocation, silent recycling
   or forced reference release. A clean cold boot restores service.

The real Proxmox startup, UI/IP, shutdown/restart and rollback check remains
separate from isolated local QEMU tests. Failed reset/unreadable MMIO, physical
cache ordering and arbitrary delayed callbacks remain limitations, not claims
of complete hardware certification. Existing host failure fixtures remain useful;
new emulator fault infrastructure is not a prerequisite for this milestone.

## Explicit native lifetime fixture

Build only, on native 9front:

```rc
mk qga9-native-vconsole-lifetime
```

It is not in default `mk test`, installs nothing and does not edit boot files.
It requires an unowned live #Y0 endpoint and an independent coordinator using
the serial console or a separate management channel, not a second #Y consumer.
Run only on a disposable guest with console recovery. Each final data close
consumes a retained context, including termination after a failed test.
Retained control files can collide with a reused PID after another boot;
exclusive creation then fails safely before data open. Preserve or deliberately
rename that known old test artifact before repeating, rather than deleting
arbitrary files or treating a collision as a driver failure.

Invocation modes are `notes`, `read-fault` and `write-fault`. The fixture creates
`/tmp/vconsole-lifetime-PID` exclusively, mode 0600, and retains it as evidence.
This is test IPC, not a new kernel interface. The two-byte file initially holds
`..`: byte 0 is written only by the fixture; byte 1 only by the coordinator.
Write the coordinator byte at offset 1 without truncating the file.

All modes check second-owner denial and nonfinal dup close. Notes use a private
process group and alarm handler; four reads and four writes must each return
the native `interrupted` error, with the backend disconnected. The same owner
then performs two binary round trips across a backend disconnect/reconnect.
The exact eight-byte payload is `00 ff 39 66 72 6f 6e 74`.

| Mode/phase | Coordinator action |
| --- | --- |
| `notes`, `R` (first) | Connect the raw backend, send the payload, read its exact echo. |
| `notes`, `C` | Close the backend, allow the close event to arrive, write `g` at offset 1. |
| `notes`, `R` (second) | Reconnect; repeat the exact binary round trip. |
| `read-fault`, `R` | Verify a queued/busy read, then remove the exact named port. |
| `write-fault`, `W` | Keep backend disconnected; write `g`, verify queued/busy write, remove the exact port. |
| Either fault, `H` | Operation has failed without alarm expiry; verify same identity and a retained data handle. Write `h` to permit final clunk. |
| Either fault, `D` | Data has closed but old status remains open. Re-add the port, verify fresh publication, then write `t`. |
| Any mode, `P` | Fixture passed; collect native exit status and bounded captured output. |

Verify real QMP/QOM IDs and exact `org.qemu.guest_agent.0` name before port
mutation. Never select a guessed controller/unrelated device. The fixture's
10-second I/O alarms and 400 coordinator polls are test watchdogs, not production
latency guarantees. Failed coordination may terminate the fixture and consume
its context; do not interpret that as a driver failure or blindly rerun on an
already consumed pool. Keep test output and the serial log when diagnosing it.

One cold-boot sequence is notes, read-fault, write-fault, notes: contexts 1–4.
After the last final close, require namespace withdrawal, the explicit exhaustion
message and continued unrelated guest responsiveness. Then halt the filesystem
cleanly and cold boot; no host forced stop while storage is active.
