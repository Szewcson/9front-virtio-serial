/* SPDX-License-Identifier: MIT */
#include "../kernel/vconsole-mmio.h"
#ifdef QGA_HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#endif

typedef struct Bus Bus;
struct Bus {
	VcMmio mm;
	Vcu8 cfg[VcPciSize];
	VcBar bars[VcBarMax];
	Vcu64 space[VcRegionMax][VcMmMapMax/8];
	int maps, unmaps, mapfail, reads, writes, fences, pauses, bad;
	int readfail, writefail, delay, pending, reject, changes, generation;
	int freeselect, drvselect, select;
	Vcu32 features[2], accepted[2], status, ports, queues, size, notify, enable;
	int drop[VcRegionMax], lengths[VcRegionMax];
};
static int checks;
static const int offsets[VcRegionMax] = {0, 4096, 64, 68};

static void
check(int ok, const char *name)
{
	checks++;
	if(ok) return;
#ifdef QGA_HOST_TEST
	fprintf(stderr, "FAIL: %s\n", name);
	exit(1);
#else
	sysfatal("FAIL: %s", (char*)name);
#endif
}

static void
word(Vcu8 *p, Vcu32 n)
{
	int i;
	for(i = 0; i < 4; i++) p[i] = n>>(8*i);
}

static int
region(Bus *b, void *base)
{
	int i;
	for(i = 0; i < VcRegionMax; i++) if(base == b->space[i]) return i;
	return -1;
}

static void*
map(void *aux, Vcu64 address, int size)
{
	Bus *b;
	int i;

	b = aux;
	i = b->maps++;
	check(i < VcRegionMax && address == b->bars[4].address+offsets[i], "map assigned capability range only");
	check(size > 0 && size <= VcMmMapMax, "bounded mapping size");
	b->lengths[i] = size;
	return i == b->mapfail ? nil : b->space[i];
}

static void
unmap(void *aux, void *base, int length)
{
	Bus *b;
	int i;

	b = aux;
	i = region(b, base);
	check(i >= 0 && b->unmaps < VcRegionMax, "unmap known owned reference");
	check(length == b->lengths[i], "unmap exact mapped extent");
	b->drop[b->unmaps++] = i;
}

static void
fence(void *aux)
{
	Bus *b;
	b = aux;
	b->fences++;
}

static void
pause(void *aux)
{
	Bus *b;
	b = aux;
	b->pauses++;
}

static int
busaccess(Bus *b, void *base, int offset, int width)
{
	int r;
	r = region(b, base);
	check(r >= 0 && offset >= 0 && (width == 1 || width == 2 || width == 4) &&
		offset <= b->lengths[r]-width && (offset&(width-1)) == 0, "register access stays in mapping at exact width");
	check(b->fences == 2*(b->reads+b->writes)+1, "barrier before every register access");
	return r;
}

static int
busread(void *aux, void *base, int offset, int width, Vcu32 *out)
{
	Bus *b;
	int r;

	b = aux;
	r = busaccess(b, base, offset, width);
	if(++b->reads == b->readfail) return VcFault;
	*out = 0;
	if(r == VcMmDevice){
		check(offset == 4 && width == 4, "read max_nr_ports, not console dimensions");
		*out = b->ports;
		return 0;
	}
	check(r == VcMmCommon, "no IRQ-clear or notify reads during initialization");
	switch(offset){
	case VcRegStatus:
		check(width == 1, "byte status read");
		if(b->pending > 0 && --b->pending == 0) b->status = 0;
		*out = b->status;
		break;
	case VcRegGeneration:
		if(b->changes > 0){ b->changes--; b->generation++; }
		*out = b->generation&255;
		break;
	case VcRegDevFeature:
		check(b->freeselect < 2 && width == 4, "selected feature bank");
		*out = b->features[b->freeselect];
		break;
	case VcRegQueues: *out = b->queues; break;
	case VcRegQueueSize: *out = b->size; break;
	case VcRegQueueNotify: *out = b->notify; break;
	case VcRegQueueEnable: *out = b->enable; break;
	default: b->bad++; break;
	}
	return 0;
}

static int
buswrite(void *aux, void *base, int offset, int width, Vcu32 value)
{
	Bus *b;
	int r;

	b = aux;
	r = busaccess(b, base, offset, width);
	if(++b->writes == b->writefail) return VcFault;
	check(r == VcMmCommon, "no notify, device-config or ISR write during setup");
	switch(offset){
	case VcRegStatus:
		check(width == 1 && !(value&VcDriverOk), "never enable DRIVER_OK in this layer");
		if(value == 0){
			b->pending = b->delay;
			b->status = b->pending == 0 ? 0 : VcNeedsReset;
		}else b->status = b->reject && (value&VcFeaturesOk) ? value&~VcFeaturesOk : value;
		break;
	case VcRegDevSelect: b->freeselect = value; break;
	case VcRegDrvSelect: b->drvselect = value; break;
	case VcRegDrvFeature:
		check(b->drvselect < 2 && width == 4, "driver feature bank write");
		b->accepted[b->drvselect] = value;
		break;
	case VcRegConfigVector:
		check(value == 65535 && width == 2, "no MSI-X vector selection");
		break;
	case VcRegQueueSelect:
		check(value < 2*b->ports+2 && width == 2, "checked queue selector");
		b->select = value;
		break;
	default: b->bad++; break; /* Includes all queue DMA/address/enable writes. */
	}
	return 0;
}

static const VcMmOps ops = {map, unmap, busread, buswrite, fence, pause};

static void
init(Bus *b)
{
	int i, at;

	memset(b, 0, sizeof *b);
	b->mapfail = b->readfail = b->writefail = -1;
	b->features[0] = b->features[1] = ~(Vcu32)0;
	b->ports = 31;
	b->queues = 64;
	b->size = 1024;
	b->notify = 2;
	b->status = VcNeedsReset;
	b->bars[4].memory = 1;
	b->bars[4].address = 0x100000;
	b->bars[4].size = 32768;
	b->cfg[0] = 0xf4; b->cfg[1] = 0x1a;
	b->cfg[2] = 0x43; b->cfg[3] = 0x10;
	b->cfg[4] = 2; b->cfg[6] = 16; b->cfg[0x34] = 0x40;
	for(i = 0; i < VcRegionMax; i++){
		at = 0x40+32*i;
		b->cfg[at] = 9;
		b->cfg[at+1] = i == 3 ? 0 : at+32;
		b->cfg[at+2] = i == 1 ? 20 : 16;
		b->cfg[at+3] = i+1;
		b->cfg[at+4] = 4;
		word(b->cfg+at+8, offsets[i]);
		word(b->cfg+at+12, i == 0 ? 56 : i == 1 ? 8192 : i == 2 ? 1 : 8);
	}
	word(b->cfg+0x60+16, 4);
	check(vcmminit(&b->mm, &ops, b) == 0, "fresh transport initialization");
}

static void
mapped(Bus *b)
{
	check(vcmmmap(&b->mm, b->cfg, 256, b->bars) == 0 && b->mm.state == VcMmMapped,
		"map complete capability layout");
	check(b->maps == 4 && b->lengths[0] == 56 && b->lengths[1] == 4096 &&
		b->lengths[2] == 1 && b->lengths[3] == 8 && b->writes == 0, "map minimum windows without hardware writes");
}

static void
prepared(Bus *b)
{
	init(b);
	mapped(b);
	check(vcmmreset(&b->mm) == 0 && b->mm.state == VcMmReset, "confirmed reset");
	check(vcmmnegotiate(&b->mm) == 0 && b->mm.state == VcMmFeatures &&
		b->mm.ports == 31 && b->mm.queues == 64, "required features and stable port configuration");
}

static void
tests(Bus *b)
{
	VcMmOps invalid;
	VcQueueInfo info, saved;
	int i, j, r, count, writes, reads;
	Vcu32 seed, size;

	invalid = ops;
	invalid.fence = nil;
	check(vcmminit(&b->mm, &invalid, b) == VcInvalid && vcmminit(nil, &ops, b) == VcInvalid,
		"reject incomplete platform ops before mutation");
	for(i = 0; i < 4; i++){
		init(b); b->mapfail = i;
		check(vcmmmap(&b->mm, b->cfg, 256, b->bars) == VcFault && b->mm.state == VcMmEmpty &&
			b->unmaps == i && b->reads == 0 && b->writes == 0, "partial map failure unwinds before MMIO");
		for(j = 0; j < i; j++) check(b->drop[j] == i-1-j, "reverse-order map unwind");
		for(j = 0; j < 4; j++) check(b->mm.map[j] == nil, "no retained partial mapping");
	}
	init(b); b->cfg[0x44] = 6;
	check(vcmmmap(&b->mm, b->cfg, 256, b->bars) == VcInvalid && b->maps == 0,
		"bad capability fails before mapping");
	init(b); mapped(b);
	check(vcmmmap(&b->mm, b->cfg, 256, b->bars) == VcInvalid &&
		vcmmnegotiate(&b->mm) == VcInvalid && vcmmdrop(&b->mm, 1) == VcInvalid,
		"no remap, negotiate or drop before reset");
	b->delay = 4;
	check(vcmmreset(&b->mm) == 0 && b->pauses == 3 && b->mm.epoch == 2 && b->status == 0,
		"wait for actual reset completion, not write acknowledgment");
	check(vcmmdrop(&b->mm, 0) == VcInvalid && b->unmaps == 0 && vcmmdrop(&b->mm, 1) == 0 &&
		b->unmaps == 4 && b->mm.state == VcMmEmpty, "drained-reference gate and exact mapping release");
	for(i = 0; i < 4; i++) check(b->drop[i] == 3-i, "reverse-order confirmed-reset release");
	check(vcmmdrop(&b->mm, 1) == VcInvalid, "no double unmap");
	init(b); mapped(b); b->delay = -1;
	check(vcmmreset(&b->mm) == VcFault && b->mm.state == VcMmFault &&
		b->pauses == VcMmResetPolls-1 && b->unmaps == 0 && b->mm.epoch == 1,
		"bounded stuck reset retains all references and epoch");
	check(vcmmdrop(&b->mm, 1) == VcInvalid && vcmmnegotiate(&b->mm) == VcInvalid,
		"FAILED is not quiescence");
	b->delay = 0;
	check(vcmmreset(&b->mm) == 0 && vcmmdrop(&b->mm, 1) == 0, "recover only after confirmed reset");
	for(i = 0; i < 2; i++){
		init(b); mapped(b);
		if(i == 0) b->writefail = 1;
		else b->readfail = 1;
		check(vcmmreset(&b->mm) == VcFault && b->mm.state == VcMmFault &&
			b->mm.epoch == 1 && b->unmaps == 0, "reset I/O failure never releases mappings or advances epoch");
	}
	prepared(b);
	check(b->accepted[0] == 2 && b->accepted[1] == 1 && b->status == 11 && b->bad == 0 &&
		b->fences == 2*(b->reads+b->writes), "only MULTIPORT/VERSION_1; all accesses fenced; no DMA activation");
	writes = b->writes; reads = b->reads;
	check(vcmmqueue(&b->mm, -1, &info) == VcInvalid && vcmmqueue(&b->mm, 64, &info) == VcInvalid &&
		vcmmqueue(&b->mm, 0, nil) == VcInvalid && b->writes == writes && b->reads == reads,
		"caller queue bounds checked before device I/O");
	for(i = 0; i < 64; i++)
		check(vcmmqueue(&b->mm, i, &info) == 0 && info.id == i && info.size == 256 &&
			info.notify == 8 && info.epoch == b->mm.epoch, "bounded queue discovery, not queue enable");
	check(vcmmdrop(&b->mm, 1) == VcInvalid && b->unmaps == 0, "negotiated mappings retained until reset");
	check(vcmmreset(&b->mm) == 0 && b->mm.ports == 0 && b->mm.queues == 0 &&
		b->mm.epoch != info.epoch && vcmmdrop(&b->mm, 1) == 0, "reset invalidates discovered queue epochs");
	for(i = 1; i <= 64; i *= 2){
		init(b); mapped(b);
		b->ports = i; b->queues = 2*i+2;
		check(vcmmreset(&b->mm) == 0 && vcmmnegotiate(&b->mm) == 0 &&
			vcmmqueue(&b->mm, 2*i+1, &info) == 0 && info.id == 2*i+1 &&
			vcmmqueue(&b->mm, 2*i+2, &info) == VcInvalid, "minimum to maximum port/queue boundaries");
	}
	for(i = 0; i < 7; i++){
		init(b); mapped(b); check(vcmmreset(&b->mm) == 0, "rejection setup reset");
		switch(i){
		case 0: b->features[0] &= ~2; break;
		case 1: b->features[1] &= ~1; break;
		case 2: b->reject = 1; break;
		case 3: b->ports = 0; break;
		case 4: b->ports = 65; break;
		case 5: b->queues = 63; break;
		case 6: b->changes = 255; break;
		}
		check(vcmmnegotiate(&b->mm) == VcFault && b->mm.state == VcMmFault &&
			b->mm.ports == 0 && b->mm.queues == 0 && b->unmaps == 0, "feature/config rejection without partial success");
	}
	init(b); mapped(b); b->changes = 2;
	check(vcmmreset(&b->mm) == 0 && vcmmnegotiate(&b->mm) == 0,
		"retry changed configuration snapshot then accept stable generation");
	/* Exercise every normal negotiation read/write failure position. */
	prepared(b); count = b->reads;
	for(i = 1; i <= count; i++){
		init(b); mapped(b); check(vcmmreset(&b->mm) == 0, "read-failure setup reset");
		b->readfail = i;
		r = vcmmnegotiate(&b->mm);
		check((r == VcFault && b->mm.state == VcMmFault && b->unmaps == 0) ||
			(r == 0 && i == 1), "injected read failure retains mappings");
	}
	prepared(b); count = b->writes;
	for(i = 2; i <= count; i++){
		init(b); mapped(b); check(vcmmreset(&b->mm) == 0, "write-failure setup reset");
		b->writefail = i;
		check(vcmmnegotiate(&b->mm) == VcFault && b->mm.state == VcMmFault && b->unmaps == 0,
			"injected write failure retains mappings");
	}
	for(i = 0; i < 6; i++){
		prepared(b);
		memset(&saved, 0xa5, sizeof saved); info = saved;
		switch(i){
		case 0: b->size = 0; break;
		case 1: b->size = 3; break;
		case 2: b->enable = 1; break;
		case 3: b->notify = 1024; break; /* Valid cap offset outside bounded mapping. */
		case 4: b->status |= VcNeedsReset; break;
		case 5: b->size = 65536; break; /* Invalid platform callback width. */
		}
		check(vcmmqueue(&b->mm, 3, &info) == VcFault && memcmp(&saved, &info, sizeof info) == 0 &&
			b->unmaps == 0, "bad queue/status leaves result untouched and holds mappings");
	}
	prepared(b); b->status = 255; writes = b->writes;
	check(vcmmqueue(&b->mm, 3, &info) == VcFault && b->writes == writes && b->unmaps == 0,
		"all-ones unplug status gets no best-effort FAILED write");
	prepared(b);
	/* Modify the owned validated layout, not a live device capability. */
	b->mm.pci.multiplier = 0;
	b->notify = 65535;
	check(vcmmqueue(&b->mm, 3, &info) == 0 && info.notify == 0, "shared notify register with zero multiplier");
	prepared(b); b->mm.epoch = ~(Vcu64)0; writes = b->writes;
	check(vcmmreset(&b->mm) == VcInvalid && b->writes == writes, "nonwrapping reset epoch");
	prepared(b); b->mm.length[1] = 1;
	check(vcmmqueue(&b->mm, 3, &info) == VcInvalid && vcmmreset(&b->mm) == VcInvalid,
		"invalid owner metadata cannot underflow notify bounds");
	seed = 160024;
	for(i = 0; i < 2000; i++){
		prepared(b);
		seed = seed*1664525+1013904223;
		size = seed&65535;
		b->size = size;
		b->notify = (seed>>16)&2047;
		r = vcmmqueue(&b->mm, 3, &info);
		check((r == 0 && size != 0 && size <= 32768 && (size&(size-1)) == 0 && b->notify <= 1023) ||
			(r == VcFault && b->mm.state == VcMmFault && b->unmaps == 0), "hostile queue metadata bounded and fail-closed");
	}
}

#ifdef QGA_HOST_TEST
int main(void)
#else
void main(void)
#endif
{
	Bus *b;
	b = malloc(sizeof *b);
	check(b != nil, "bounded fixture allocation");
	tests(b);
	free(b);
#ifdef QGA_HOST_TEST
	printf("virtio MMIO initialization: %d checks passed (simulated registers, no DMA)\n", checks);
	return 0;
#else
	print("virtio MMIO initialization: %d checks passed (simulated registers, no DMA)\n", checks);
	exits(nil);
#endif
}
