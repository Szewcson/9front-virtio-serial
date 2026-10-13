/* SPDX-License-Identifier: MIT */
/* Exercises the real PC adapter with fake native primitives, not a kernel. */
#include "vconsole-pc-shim.h"
#include "../kernel/vconsole-pc.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct Fixture Fixture;
struct Fixture {
	VcPc pc;
	Pcidev device;
	Vcu8 cfg[VcPciSize];
	void *mapping[VcRegionMax];
	int length[VcRegionMax], maps, unmaps, failmap, failcfg, fences;
};
static Fixture *current;
static int checks;
static void *dma;
static unsigned long dmabytes;
static int dmafail, dmalast;
static const int offsets[VcRegionMax] = {0, 4096, 64, 68};

static void
check(int ok, const char *name)
{
	checks++;
	if(ok) return;
	fprintf(stderr, "FAIL: %s\n", name);
	exit(1);
}

static void
barrier(void)
{
	current->fences++;
}
void (*coherence)(void) = barrier;

void*
mallocalign(unsigned long bytes, unsigned long align, long offset, unsigned long span)
{
	check(bytes > 0 && bytes <= VcDmaMax && align == 16 && offset == 0 && span == 0,
		"native DMA allocation uses bounded aligned kernel API");
	if(dmafail) return nil;
	dma = malloc(bytes);
	check(dma != nil && ((uintptr)dma&15) == 0, "test allocator has native alignment");
	dmabytes = bytes;
	return dma;
}

uintptr_t
testpaddr(void *p)
{
	uintptr offset;
	check(dma != nil && (uintptr)p >= (uintptr)dma &&
		(uintptr)p < (uintptr)dma+dmabytes, "physical translation stays in owned RAM");
	offset = (uintptr)p-(uintptr)dma;
	return 0x100000+offset+(dmalast && offset != 0 ? 16 : 0);
}

void
delay(int n)
{
	check(n == 1, "native delay is one millisecond");
}

int
pcicfgr8(Pcidev *p, int at)
{
	check(p == &current->device && at >= 0 && at < VcPciSize, "bounded PCI snapshot read");
	return at == current->failcfg ? -1 : current->cfg[at];
}

void*
vmap(uint64_t address, int64_t size)
{
	int i;
	i = current->maps++;
	check(i < VcRegionMax && size > 0 && size <= VcMmMapMax &&
		address == (current->device.mem[4].bar&~(Vcu64)15)+offsets[i], "native map only checked BAR ranges");
	if(i == current->failmap) return nil;
	current->length[i] = size;
	current->mapping[i] = calloc(1, size);
	check(current->mapping[i] != nil, "checked test-only MMIO allocation");
	return current->mapping[i];
}

void
vunmap(void *base, int64_t size)
{
	int i;
	for(i = 0; i < VcRegionMax; i++)
		if(base != nil && current->mapping[i] == base){
			check(size == current->length[i], "native unmap exact reference");
			free(base);
			current->mapping[i] = nil;
			current->unmaps++;
			return;
		}
	check(0, "no foreign/double unmap");
}

static void
word(Vcu8 *p, Vcu32 n)
{
	int i;
	for(i = 0; i < 4; i++) p[i] = n>>(8*i);
}

static void
init(Fixture *f)
{
	int i, at;

	memset(f, 0, sizeof *f);
	current = f;
	f->failcfg = f->failmap = -1;
	f->device.vid = 0x1af4;
	f->device.did = 0x1043;
	f->device.mem[4].bar = 0x100004; /* 64-bit BAR, upper slot is NOT another BAR. */
	f->device.mem[4].size = 32768;
	f->cfg[0] = 0xf4; f->cfg[1] = 0x1a;
	f->cfg[2] = 0x43; f->cfg[3] = 0x10;
	f->cfg[4] = 2; f->cfg[6] = 16; f->cfg[0x34] = 0x40;
	for(i = 0; i < VcRegionMax; i++){
		at = 0x40+32*i;
		f->cfg[at] = 9;
		f->cfg[at+1] = i == 3 ? 0 : at+32;
		f->cfg[at+2] = i == 1 ? 20 : 16;
		f->cfg[at+3] = i+1;
		f->cfg[at+4] = 4;
		word(f->cfg+at+8, offsets[i]);
		word(f->cfg+at+12, i == 0 ? 56 : i == 1 ? 8192 : i == 2 ? 1 : 8);
	}
	word(f->cfg+0x60+16, 4);
	check(vcpcinit(&f->pc) == 0, "native PC ops initializer");
}

static void
reject(Fixture *f)
{
	check(vcpcmap(&f->pc, &f->device) == VcInvalid && f->maps == 0 &&
		f->pc.device == nil && f->pc.mmio.state == VcMmEmpty, "reject PCI snapshot before mapping or register writes");
}

int
main(void)
{
	Fixture *f;
	VcDmaOps dmaops;
	VcDmaMemory memory;
	VcMmio *m;
	Vcu32 value;
	Vcu8 foreign[8], saved[VcPciSize];
	int i;

	f = malloc(sizeof *f);
	check(f != nil, "bounded PC fixture allocation");
	init(f); coherence = nil;
	check(vcpcinit(&f->pc) == VcInvalid, "barrier primitive required before initialization");
	coherence = barrier;
	check(vcpcdmaops(nil) == VcInvalid && vcpcdmaops(&dmaops) == 0,
		"native DMA ops require valid output and barrier");
	dmafail = 1;
	check(dmaops.alloc(nil, 4096, 16, &memory) == VcFault, "native allocation failure");
	dmafail = 0;
	check(dmaops.alloc(nil, 0, 16, &memory) == VcInvalid &&
		dmaops.alloc(nil, VcDmaMax+1, 16, &memory) == VcInvalid &&
		dmaops.alloc(nil, 4096, 8, &memory) == VcInvalid &&
		dmaops.alloc(nil, 4096, 16, nil) == VcInvalid, "native DMA input bounds");
	dmalast = 1;
	check(dmaops.alloc(nil, 4096, 16, &memory) == VcFault, "noncontiguous physical extent rejected");
	dmalast = 0;
	for(i = 16; i <= VcDmaMax; i *= 2){
		check(dmaops.alloc(nil, i, 16, &memory) == 0 && memory.bytes == i &&
			memory.physical == 0x100000, "native RAM physical extent checks");
		dmaops.free(nil, &memory);
		dma = nil;
	}
	for(i = 0; i < VcPciSize; i++){
		init(f); f->failcfg = i; reject(f);
	}
	init(f); f->cfg[4] = 0; reject(f);
	init(f); f->cfg[0] = 0; reject(f);
	init(f); f->device.vid = 0; reject(f);
	init(f); f->device.did = 0x1042; reject(f);
	init(f); f->device.mem[4].size = -1; reject(f);
	init(f); f->device.mem[4].bar |= 1; reject(f);
	init(f); f->device.mem[4].bar |= 2; reject(f); /* reserved BAR width */
	init(f); f->device.mem[5].bar = 0x200000; f->device.mem[5].size = 32768;
	f->cfg[0x44] = 5; reject(f); /* high half of 64-bit BAR */
	init(f); f->device.mem[4].bar = 0x100000;
	f->device.mem[5].bar = 0x200004; f->device.mem[5].size = 32768; reject(f);
	for(i = 0; i < 2; i++){
		init(f); f->cfg[0x34] = 0xc0; f->cfg[0xc0] = i == 0 ? 5 : 0x11;
		f->cfg[0xc1] = 0x40;
		if(i == 0) f->cfg[0xc2] = 1;
		else f->cfg[0xc3] = 0x80;
		reject(f);
	}
	init(f); f->device.mem[4].bar = 4;
	check(vcpcmap(&f->pc, &f->device) == VcFault && f->maps == 0, "reject page-zero MMIO");
	init(f); f->device.mem[4].bar = VMAPSIZE|4;
	check(vcpcmap(&f->pc, &f->device) == VcFault && f->maps == 0, "bounded physical mapping window");
	init(f); f->device.mem[4].bar = (VMAPSIZE-4096)|4;
	check(vcpcmap(&f->pc, &f->device) == VcFault && f->maps == 1 && f->unmaps == 1,
		"mapping prefix crossing direct window unwinds acquired reference");
	for(i = 0; i < 4; i++){
		init(f); f->failmap = i;
		check(vcpcmap(&f->pc, &f->device) == VcFault && f->unmaps == i && f->pc.device == nil,
			"native mapping failure consumes only acquired references");
	}
	init(f);
	memmove(saved, f->cfg, sizeof saved);
	check(vcpcmap(&f->pc, &f->device) == 0 && f->pc.device == &f->device && f->maps == 4,
		"actual PC adapter maps fake native primitives");
	m = &f->pc.mmio;
	check(vcpcmap(&f->pc, &f->device) == VcInvalid && f->maps == 4, "no remap of active owner");
	for(i = 1; i <= 4; i *= 2){
		check(m->ops.write(m->aux, m->map[0], 0, i, 127) == 0 &&
			m->ops.read(m->aux, m->map[0], 0, i, &value) == 0 && value == 127,
			"volatile byte/word/dword MMIO operations");
	}
	check(m->ops.read(m->aux, m->map[0], 55, 2, &value) == VcInvalid &&
		m->ops.read(m->aux, m->map[0], 56, 1, &value) == VcInvalid &&
		m->ops.read(m->aux, m->map[0], 1, 4, &value) == VcInvalid &&
		m->ops.read(m->aux, m->map[0], -1, 1, &value) == VcInvalid &&
		m->ops.read(m->aux, m->map[0], 0, 8, &value) == VcInvalid &&
		m->ops.read(m->aux, foreign, 0, 1, &value) == VcInvalid &&
		m->ops.read(m->aux, m->map[0], 0, 1, nil) == VcInvalid,
		"backend checks bounds, widths, alignment and mapping identity");
	check(m->ops.write(m->aux, m->map[0], 0, 1, 256) == VcInvalid &&
		m->ops.write(m->aux, m->map[0], 0, 2, 65536) == VcInvalid &&
		m->ops.write(m->aux, foreign, 0, 1, 0) == VcInvalid,
		"narrow write values and foreign pointers rejected");
	i = m->length[0];
	m->length[0] = (-2147483647-1);
	check(m->ops.read(m->aux, m->map[0], 0, 1, &value) == VcInvalid,
		"corrupt owner extent cannot overflow backend bounds arithmetic");
	m->length[0] = i;
	check(vcmmreset(m) == 0 && f->fences == 4 && vcmmdrop(m, 1) == 0 && f->unmaps == 4,
		"PC callbacks confirm reset and release owned mapping references");
	check(memcmp(saved, f->cfg, sizeof saved) == 0, "no PCI config/bus-master/interrupt changes");
	free(f);
	printf("virtio PC adapter: %d checks passed (fake native primitives, no hardware)\n", checks);
	return 0;
}
