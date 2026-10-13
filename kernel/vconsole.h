/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_H
#define QGA9_VCONSOLE_H

#ifdef QGA_HOST_TEST
#include <stdint.h>
#include <string.h>
typedef uint8_t Vcu8;
typedef uint16_t Vcu16;
typedef uint32_t Vcu32;
typedef uint64_t Vcu64;
#define nil ((void*)0)
#else
#ifndef QGA_VCONSOLE_KERNEL
#include <u.h>
#include <libc.h>
#endif
typedef u8int Vcu8;
typedef u16int Vcu16;
typedef u32int Vcu32;
typedef u64int Vcu64;
#endif

enum {
	VcPciSize = 256, VcBarMax = 6, VcRegionMax = 4,
	VcQueueMax = 256, VcBufferMax = 4096,
	VcPortMax = 64, VcQueuesMax = 2*VcPortMax+2,
	VcNameMax = 128, VcControlMax = 8+VcNameMax,
	VcDeviceReady = 0, VcAdd, VcRemove, VcPortReady, VcConsole,
	VcResize, VcOpen, VcName,
	VcInvalid = -1, VcFault = -2,
};

typedef struct VcBar VcBar;
typedef struct VcRegion VcRegion;
typedef struct VcPci VcPci;
struct VcBar { Vcu64 address, size; int memory; };
/* region order: common, notify, ISR, device. MMIO only in this prototype. */
struct VcRegion { Vcu64 address; Vcu32 length, offset; int bar; };
struct VcPci { VcRegion region[VcRegionMax]; Vcu32 multiplier; };

typedef struct VcSlot VcSlot;
typedef struct VcQueue VcQueue;
typedef struct VcUsed VcUsed;
struct VcSlot { Vcu32 length; Vcu64 cookie; int posted; };
struct VcQueue {
	int size, receive, posted, failed;
	Vcu16 used;
	Vcu64 epoch;
	VcSlot slot[VcQueueMax];
};
struct VcUsed { Vcu32 id, length; Vcu64 cookie, epoch; };

typedef struct VcControl VcControl;
typedef struct VcPort VcPort;
typedef struct VcPorts VcPorts;
typedef struct VcToken VcToken;
struct VcControl {
	Vcu32 id;
	int event, value, cols, rows;
	char name[VcNameMax];
};
struct VcPort {
	int present, named, console, hostopen, guestopen;
	Vcu64 generation;
	char name[VcNameMax];
};
struct VcPorts { int size, failed; VcPort port[VcPortMax]; };
struct VcToken { Vcu32 id; Vcu64 generation; };

/* All state objects must be initialized, caller-owned and serialized by one
 * driver worker. No allocations,
 * locks, IRQ work, MMIO/DMA access or memory barriers are hidden here. Inputs are
 * immutable CPU-owned snapshots for the entire call, NOT live DMA memory.
 * Output, snapshots and state must not overlap; pointers are never retained. */
int vcpci(VcPci*, const Vcu8*, int, const VcBar*);
int vcnotify(const VcPci*, Vcu16, Vcu64*);
int vcportqueues(Vcu32, int*, int*);

/* init is ONLY for a fresh/unpublished object. Never re-init an active queue.
 * post fills one 16-byte descriptor; caller publishes avail only after a barrier.
 * consume validates a whole used-ring snapshot batch before releasing ANY slot.
 * Caller processes/copies RX data before reposting its buffer. Cookies can carry
 * port generations; epoch is a separate device-reset generation.
 * Fault is sticky: buffers remain leased until valid completion or a reset whose
 * caller has actually proved DMA quiescence and drained IRQ/worker references. */
int vcqinit(VcQueue*, int, int);
int vcqpost(VcQueue*, int, Vcu64, int, Vcu64, Vcu8*, int);
int vcqconsume(VcQueue*, Vcu16, const Vcu8*, int, VcUsed*, int);
int vcqreset(VcQueue*, int);

int vcdecode(VcControl*, const Vcu8*, int);
int vcencode(Vcu8*, int, Vcu32, int, int);
int vcportsinit(VcPorts*, int);
int vcportsreset(VcPorts*, int);
int vcreceive(VcPorts*, const Vcu8*, int);
/* Only the exact nonconsole org.qemu.guest_agent.0 port can be claimed.
 * One owner; stale close/data tokens cannot affect a new channel generation.
 * Claim/release do NOT send PORT_OPEN or release DMA buffers; driver must do so. */
int vcclaim(VcPorts*, VcToken*);
int vcalive(const VcPorts*, const VcToken*);
int vcrelease(VcPorts*, const VcToken*);

#endif
