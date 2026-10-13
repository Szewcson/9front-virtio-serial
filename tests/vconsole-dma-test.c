/* SPDX-License-Identifier: MIT */
#include "../kernel/vconsole-dma.h"
#ifdef QGA_HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#define USED(x) ((void)(x))
#endif

typedef struct Fixture Fixture;
struct Fixture {
	VcDmaQueue q;
	VcMmio m;
	Vcu64 common[7], notify[8], isr, device;
	Vcu32 regs[2*VcPortMax+2][14];
	void *raw;
	int allocs, frees, allocfail, malformed, reads, writes, failread, failwrite;
	int fences, mmfences, stage, selected, stuck, mismatch, mutate;
	int runtime, notifications, notifyid, notifyoffset, generation, changes, portcount;
	Vcu32 status;
};
static int checks;

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
word(Vcu8 *p, Vcu32 value)
{
	int i;
	for(i = 0; i < 4; i++) p[i] = value>>(8*i);
}

static Vcu32
number(const Vcu8 *p)
{
	return p[0]|(Vcu32)p[1]<<8|(Vcu32)p[2]<<16|(Vcu32)p[3]<<24;
}

static int
dmaalloc(void *aux, int bytes, int align, VcDmaMemory *out)
{
	Fixture *f;
	Vcuptr base;
	f = aux;
	f->allocs++;
	check(bytes > 0 && bytes <= VcDmaMax && align == 16 && f->raw == nil,
		"bounded fresh DMA allocation");
	if(f->allocfail == 1) return VcFault;
	f->raw = malloc(bytes+32);
	check(f->raw != nil, "checked fixture allocation");
	base = ((Vcuptr)f->raw+15)&~(Vcuptr)15;
	out->base = (void*)base;
	out->bytes = bytes;
	out->physical = 0x123456780000ULL;
	if(f->malformed == 1) out->base = (Vcu8*)out->base+1;
	if(f->malformed == 2) out->bytes--;
	if(f->malformed == 3) out->physical++;
	if(f->malformed == 4) out->physical = 0;
	if(f->malformed == 5) out->physical = ~(Vcu64)15;
	if(f->malformed == 6) out->base = (void*)(~(Vcuptr)15);
	return f->allocfail == 2 ? VcFault : 0;
}

static void
dmafree(void *aux, const VcDmaMemory *memory)
{
	Fixture *f;
	f = aux;
	check(f->raw != nil && memory->base != nil, "consume only acquired DMA allocation once");
	free(f->raw);
	f->raw = nil;
	f->frees++;
}

static void
dmafence(void *aux)
{
	Fixture *f;
	Vcu8 *p;
	f = aux;
	f->fences++;
	p = f->q.memory.base;
	if(f->stage == 1){
		check(*(Vcu16*)(p+f->q.layout.avail+2) == f->q.avail &&
			f->q.core.posted > 0, "descriptor/buffer/avail entry before idx barrier");
		f->stage = 2;
	}else if(f->stage == 2){
		check(*(Vcu16*)(p+f->q.layout.avail+2) == f->q.avail,
			"published idx before trailing barrier");
		f->stage = 0;
	}
	if(f->mutate > 0 && --f->mutate == 0)
		word(p+f->q.layout.used+4, ~(Vcu32)0); /* after snapshot, before core validation */
}

static void*
mmmap(void *aux, Vcu64 address, int bytes)
{
	USED(aux); USED(address); USED(bytes);
	return nil;
}

static void
mmunmap(void *aux, void *base, int bytes)
{
	USED(aux); USED(base); USED(bytes);
}

static void
mmfence(void *aux)
{
	Fixture *f;
	f = aux;
	f->mmfences++;
}

static void
mmpause(void *aux)
{
	USED(aux);
}

static void
accesscheck(Fixture *f, void *base, int offset, int width)
{
	check((base == f->common || base == f->notify || base == &f->device) &&
		offset >= 0 && offset <= (base == f->notify ? 64 : base == &f->device ? 8 : 56)-width &&
		(width == 1 || width == 2 || width == 4) && (offset&(width-1)) == 0,
		"MMIO stays in common config at aligned field width");
	check(f->mmfences == 2*(f->reads+f->writes)+1, "MMIO barrier before access");
}

static int
mmread(void *aux, void *base, int offset, int width, Vcu32 *out)
{
	Fixture *f;
	f = aux;
	accesscheck(f, base, offset, width);
	if(++f->reads == f->failread) return VcFault;
	if(base == &f->device){ check(offset == 4 && width == 4, "bounded port-count health read"); *out = f->portcount; }
	else if(offset == VcRegStatus) *out = f->status;
	else if(offset == VcRegGeneration){
		if(f->changes > 0){ f->changes--; f->generation++; }
		*out = f->generation&255;
	}
	else if(offset == VcRegQueueNotify) *out = f->selected;
	else *out = f->regs[f->selected][offset/4];
	if(f->mismatch == offset+1) (*out)++;
	return 0;
}

static int
mmwrite(void *aux, void *base, int offset, int width, Vcu32 value)
{
	Fixture *f;
	int i;
	f = aux;
	accesscheck(f, base, offset, width);
	f->writes++;
	if(base == f->notify){
		check(width == 2 && value < VcQueuesMax, "runtime notification is a 16-bit queue ID");
		f->notifications++; f->notifyid = value; f->notifyoffset = offset;
	}else if(offset == VcRegStatus){
		check(width == 1 && (f->runtime || !(value&VcDriverOk)), "DRIVER_OK only via explicit runtime test");
		f->status = value == 0 && f->stuck ? VcNeedsReset : value;
		if(f->status == 0)
			for(i = 0; i < 2*VcPortMax+2; i++){
				memset(f->regs[i], 0, sizeof f->regs[i]);
				f->regs[i][VcRegQueueSize/4] = f->q.info.size;
			}
	}else if(offset == VcRegQueueSelect){
		check(value < 2*VcPortMax+2 && width == 2, "bounded queue selector");
		f->selected = value;
	}else if(offset == VcRegQueueVector){
		check(value == 65535 && width == 2, "INTx-only queue vector");
	}else if(offset == VcRegQueueEnable){
		check(value == 1 && width == 2, "never disable queue with zero");
		f->regs[f->selected][offset/4] = value;
	}else{
		check((offset == VcRegQueueSize && width == 2) ||
			(offset >= VcRegQueueDesc && width == 4), "only size and DMA-address writes");
		f->regs[f->selected][offset/4] = value;
	}
	/* Failure may happen AFTER the device applied the write. */
	return f->writes == f->failwrite ? VcFault : 0;
}

static void
init(Fixture *f, int size, int receive, int capacity)
{
	VcDmaOps ops;
	VcMmOps mmops;
	VcQueueInfo info;
	int i;

	memset(f, 0, sizeof *f);
	ops.alloc = dmaalloc; ops.free = dmafree; ops.fence = dmafence;
	check(vcdmainit(&f->q, &ops, f) == 0, "DMA queue fresh init");
	mmops.map = mmmap; mmops.unmap = mmunmap; mmops.read = mmread;
	mmops.write = mmwrite; mmops.fence = mmfence; mmops.pause = mmpause;
	check(vcmminit(&f->m, &mmops, f) == 0, "mock MMIO fresh init");
	f->m.map[0] = f->common; f->m.length[0] = 56;
	f->m.map[1] = f->notify; f->m.length[1] = 64;
	f->m.map[2] = &f->isr; f->m.length[2] = 1;
	f->m.map[3] = &f->device; f->m.length[3] = 8;
	f->m.pci.region[1].address = 0x1000; f->m.pci.region[1].length = 64;
	f->m.pci.multiplier = 2;
	f->m.state = VcMmFeatures; f->m.epoch = 7; f->m.ports = 1; f->m.queues = 4;
	f->status = VcAck|VcDriver|VcFeaturesOk;
	f->portcount = 1;
	for(i = 0; i < 2*VcPortMax+2; i++) f->regs[i][VcRegQueueSize/4] = size;
	memset(&info, 0, sizeof info);
	info.id = receive ? 2 : 3; info.size = size; info.epoch = 7; info.notify = 2*info.id;
	/* Allocation is optional so allocation-failure tests can set injection first. */
	f->q.info = info;
	f->q.layout.capacity = capacity;
}

static int
allocqueue(Fixture *f, int receive)
{
	VcQueueInfo info;
	info = f->q.info;
	return vcdmaalloc(&f->q, &info, receive, f->q.layout.capacity);
}

static void
allocate(Fixture *f, int receive)
{
	check(allocqueue(f, receive) == 0,
		"allocate bounded owned ring and buffer arena");
}

static void
bindqueue(Fixture *f, int receive)
{
	allocate(f, receive);
	check(vcdmabind(&f->q, &f->m) == 0 && f->q.exposed && f->q.state == VcDmaBound,
		"bind owned storage to selected queue");
	check(f->regs[f->selected][VcRegQueueDesc/4] == (Vcu32)f->q.memory.physical &&
		f->regs[f->selected][VcRegQueueDesc/4+1] == f->q.memory.physical>>32,
		"64-bit DMA address not truncated");
}

static void
releasequeue(Fixture *f)
{
	check(vcdmadrop(&f->q, 1) == VcInvalid && f->frees == 0, "exposed memory retained before reset");
	f->failread = f->failwrite = f->mismatch = f->stuck = 0;
	check(vcmmreset(&f->m) == 0, "confirmed device reset before DMA free");
	check(vcdmadrop(&f->q, 0) == VcInvalid && vcdmadrop(&f->q, 1) == 0 && f->frees == 1,
		"reset epoch plus drained references release arena exactly once");
	check(vcdmadrop(&f->q, 1) == VcInvalid, "no double DMA release");
}

static void
complete(Fixture *f, int id, int length)
{
	Vcu8 *p;
	Vcu16 idx;
	p = (Vcu8*)f->q.memory.base+f->q.layout.used;
	idx = *(Vcu16*)(p+2);
	word(p+4+(idx&(f->q.layout.size-1))*8, id);
	word(p+8+(idx&(f->q.layout.size-1))*8, length);
	*(Vcu16*)(p+2) = idx+1;
}

static void
layouts(void)
{
	VcDmaLayout l, saved;
	int size, capacity;
	memset(&saved, 0x5a, sizeof saved);
	l = saved;
	check(vcdmalayout(&l, 0, 1) == VcInvalid && vcdmalayout(&l, 3, 1) == VcInvalid &&
		vcdmalayout(&l, 512, 1) == VcInvalid && vcdmalayout(&l, 1, 0) == VcInvalid &&
		vcdmalayout(&l, 1, 4097) == VcInvalid && memcmp(&l, &saved, sizeof l) == 0,
		"invalid layout rejection is atomic");
	for(size = 1; size <= 256; size *= 2)
		for(capacity = 1; capacity <= 4096; capacity++){
			check(vcdmalayout(&l, size, capacity) == 0 && l.bytes <= VcDmaMax &&
				l.avail == size*16 && (l.used&3) == 0 && (l.buffers&15) == 0 &&
				l.used >= l.avail+6+size*2 && l.buffers >= l.used+6+size*8 &&
				l.stride >= capacity && l.bytes == l.buffers+size*l.stride,
				"every bounded layout has disjoint aligned ring/buffer extents");
		}
}

static void
allocations(Fixture *f)
{
	int i;
	for(i = 1; i <= 8; i++){
		init(f, 4, 1, 128);
		if(i <= 6) f->malformed = i;
		else f->allocfail = i-6;
		check(allocqueue(f, 1) == VcFault &&
			f->q.state == VcDmaEmpty && f->raw == nil && f->frees == (i == 7 ? 0 : 1),
			"malformed/error allocations unwind before exposure without dereference");
	}
	init(f, 4, 1, 128);
	check(allocqueue(f, 0) == VcInvalid && f->allocs == 0,
		"queue parity determines DMA direction");
	allocate(f, 1);
	for(i = 0; i < f->q.memory.bytes; i++) check(((Vcu8*)f->q.memory.base)[i] == 0, "arena fully zeroed");
	check(allocqueue(f, 1) == VcInvalid && f->allocs == 1,
		"no overwrite/reallocation of active owned storage");
	check(vcdmadrop(&f->q, 1) == 0 && f->frees == 1, "unexposed allocation can unwind without device reset");
}

static void
setupfaults(Fixture *f)
{
	int i, reads, writes, registers[5];
	init(f, 4, 1, 128); bindqueue(f, 1);
	reads = f->reads; writes = f->writes;
	releasequeue(f);
	for(i = 1; i <= reads; i++){
		init(f, 4, 1, 128); allocate(f, 1); f->failread = i;
		check(vcdmabind(&f->q, &f->m) < 0 && f->q.state == VcDmaFault && f->frees == 0,
			"every setup read failure retains exposed DMA storage");
		releasequeue(f);
	}
	for(i = 1; i <= writes; i++){
		init(f, 4, 1, 128); allocate(f, 1); f->failwrite = i;
		check(vcdmabind(&f->q, &f->m) < 0 && f->q.state == VcDmaFault && f->frees == 0,
			"every ambiguously applied setup write requires confirmed reset");
		releasequeue(f);
	}
	registers[0] = VcRegQueueSize; registers[1] = VcRegQueueEnable;
	registers[2] = VcRegQueueDesc; registers[3] = VcRegQueueAvail+4; registers[4] = VcRegQueueUsed;
	for(i = 0; i < 5; i++){
		init(f, 4, 1, 128); allocate(f, 1); f->mismatch = registers[i]+1;
		check(vcdmabind(&f->q, &f->m) < 0, "register readback mismatch faults setup");
		releasequeue(f);
	}
	init(f, 4, 1, 128); bindqueue(f, 1); f->stuck = 1;
	check(vcmmreset(&f->m) == VcFault && vcdmadrop(&f->q, 1) == VcInvalid && f->frees == 0,
		"stuck reset never frees DMA memory");
	releasequeue(f);
}

static void
roundtrip(Fixture *f, int size, int receive, int capacity)
{
	VcDmaDone done[VcQueueMax], saved;
	Vcu8 input[VcBufferMax], output[VcBufferMax], *buffer;
	int i;

	memset(input, 0xa5, sizeof input);
	init(f, size, receive, capacity); bindqueue(f, receive);
	for(i = 0; i < size; i++){
		f->stage = 1;
		check(vcdmapost(&f->q, i, receive ? nil : input, capacity, 42) == 0 &&
			vcdmapost(&f->q, i, receive ? nil : input, capacity, 42) == VcInvalid,
			"post exactly once with ordered avail publication");
		buffer = (Vcu8*)f->q.memory.base+f->q.layout.buffers+i*f->q.layout.stride;
		if(receive) memmove(buffer, input, capacity); /* test device writes before used.idx */
		else check(memcmp(buffer, input, capacity) == 0, "TX copies exact caller bytes");
		check(number((Vcu8*)f->q.memory.base+16*i+8) == (Vcu32)capacity &&
			((Vcu8*)f->q.memory.base)[16*i+12] == (receive ? 2 : 0), "bounded descriptor direction and length");
		complete(f, size-1-i, receive ? capacity : 0); /* out-of-order IDs */
	}
	memset(done, 0x5a, sizeof done); saved = done[0];
	check(vcdmapoll(&f->q, done, size-1) == VcInvalid && f->q.core.posted == size &&
		memcmp(done, &saved, sizeof saved) == 0, "short output cannot release a partial batch");
	check(vcdmapoll(&f->q, done, size) == size && vcdmapoll(&f->q, done, size) == 0,
		"snapshot validates complete batch once");
	for(i = 0; i < size; i++){
		check(vcdmapost(&f->q, done[i].used.id, receive ? nil : input, capacity, 42) == VcInvalid,
			"completed payload stays held until copied/reaped");
		if(receive) check(vcdmatake(&f->q, &done[i], output, capacity-1) == VcInvalid,
			"short RX output retains completion lease");
		check(vcdmatake(&f->q, &done[i], output, capacity) == (receive ? capacity : 0) &&
			(!receive || memcmp(output, input, capacity) == 0), "copy RX within validated used length; TX just reaps");
		check(vcdmatake(&f->q, &done[i], output, capacity) == VcInvalid, "completion receipt is one-shot");
	}
	releasequeue(f);
}

static void
completionfaults(Fixture *f)
{
	VcDmaDone done[4], saved[4];
	Vcu8 output[32];
	int i;
	for(i = 0; i < 5; i++){
		init(f, 4, 1, 32); bindqueue(f, 1);
		check(vcdmapost(&f->q, 0, nil, 32, 1) == 0 && vcdmapost(&f->q, 1, nil, 32, 1) == 0, "post fault-fixture RX");
		complete(f, 0, 1);
		complete(f, i == 0 ? 0 : i == 1 ? 4 : i == 2 ? 2 : 1, i == 3 ? 33 : 1);
		if(i == 4) *(Vcu16*)((Vcu8*)f->q.memory.base+f->q.layout.used+2) = 65535;
		memset(saved, 0x5a, sizeof saved); memmove(done, saved, sizeof done);
		check(vcdmapoll(&f->q, done, 4) == VcFault && f->q.core.posted == 2 &&
			memcmp(done, saved, sizeof done) == 0, "duplicate/unposted/out-of-range/long/jumped completion is atomic fault");
		check(vcdmapost(&f->q, 2, nil, 32, 1) == VcFault && vcdmatake(&f->q, done, output, 32) == VcFault,
			"faulted queue does not recycle or copy DMA payloads");
		releasequeue(f);
	}
	init(f, 1, 1, 32); bindqueue(f, 1);
	check(vcdmapost(&f->q, 0, nil, 32, 1) == 0, "snapshot-isolation fixture");
	complete(f, 0, 0); f->mutate = 2;
	check(vcdmapoll(&f->q, done, 1) == 1 && vcdmatake(&f->q, done, nil, 0) == 0,
		"core sees immutable snapshot despite later source mutation; zero RX supported");
	releasequeue(f);
}

static void
arguments(Fixture *f)
{
	VcDmaDone done, forged;
	VcQueueInfo info;
	VcDmaOps ops;
	Vcu8 bytes[32];
	int saved, writes;

	memset(bytes, 0x7e, sizeof bytes);
	memset(&ops, 0, sizeof ops);
	check(vcdmainit(nil, &ops, f) == VcInvalid && vcdmainit(&f->q, nil, f) == VcInvalid &&
		vcdmainit(&f->q, &ops, f) == VcInvalid, "missing owner/callbacks rejected");
	init(f, 4, 1, 32); allocate(f, 1);
	info = f->q.info; writes = f->writes;
	check(vcmmenable(&f->m, &info, 0, 0x2000, 0x3000) == VcInvalid &&
		vcmmenable(&f->m, &info, 0x1001, 0x2000, 0x3000) == VcInvalid &&
		vcmmenable(&f->m, &info, 0x1000, 0x1000, 0x3000) == VcInvalid &&
		vcmmenable(&f->m, &info, 0x1000, 0x2000, ~(Vcu64)3) == VcInvalid &&
		f->writes == writes, "invalid/null/overlapping/overflow DMA addresses rejected before MMIO");
	check(vcdmapost(&f->q, 0, nil, 32, 1) == VcInvalid &&
		vcdmabind(&f->q, nil) == VcInvalid && !f->q.exposed, "unbound queues cannot publish");
	f->m.epoch++;
	check(vcdmabind(&f->q, &f->m) == VcInvalid && !f->q.exposed, "stale discovery before bind");
	f->m.epoch--;
	check(vcdmabind(&f->q, &f->m) == 0, "valid bind after caller-argument rejection");
	check(vcdmapost(&f->q, -1, nil, 32, 1) == VcInvalid &&
		vcdmapost(&f->q, 4, nil, 32, 1) == VcInvalid &&
		vcdmapost(&f->q, 0, nil, 0, 1) == VcInvalid &&
		vcdmapost(&f->q, 0, nil, 33, 1) == VcInvalid &&
		vcdmapost(&f->q, 0, bytes, 32, 1) == VcInvalid && f->q.avail == 0,
		"post caller bounds and RX/TX input direction before publication");
	check(vcdmapoll(&f->q, nil, 0) == 0 && vcdmapoll(&f->q, &done, -1) == VcInvalid,
		"empty and invalid-capacity polling");
	saved = f->q.layout.used; f->q.layout.used = (-2147483647-1);
	check(vcdmapoll(&f->q, &done, 1) == VcInvalid, "corrupt layout cannot reach DMA pointer arithmetic");
	f->q.layout.used = saved;
	check(vcdmapost(&f->q, 0, nil, 32, 77) == 0, "receipt forgery fixture");
	complete(f, 0, 1);
	check(vcdmapoll(&f->q, nil, 1) == VcInvalid && f->q.core.posted == 1 &&
		vcdmapoll(&f->q, &done, 1) == 1, "nil result cannot consume completed payload");
	for(saved = 0; saved < 5; saved++){
		forged = done;
		if(saved == 0) forged.used.id = 4;
		if(saved == 1) forged.used.cookie++;
		if(saved == 2) forged.used.epoch++;
		if(saved == 3) forged.used.length++;
		if(saved == 4) forged.serial++;
		check(vcdmatake(&f->q, &forged, bytes, 32) == VcInvalid && f->q.life[0] == VcDmaReady,
			"forged ID/cookie/epoch/length/serial cannot recycle payload");
	}
	check(vcdmatake(&f->q, &done, nil, 32) == VcInvalid &&
		vcdmatake(&f->q, &done, bytes, -1) == VcInvalid &&
		vcdmatake(&f->q, &done, bytes, 32) == 1, "RX null/negative output retains valid receipt");
	check(vcmmreset(&f->m) == 0 && vcdmapost(&f->q, 0, nil, 32, 1) == VcFault &&
		vcdmatake(&f->q, &done, bytes, 32) == VcFault && vcdmadrop(&f->q, 1) == 0,
		"reset invalidates software queue before reuse/free");
	init(f, 1, 0, 32); bindqueue(f, 0);
	check(vcdmapost(&f->q, 0, nil, 32, 1) == VcInvalid &&
		vcdmapost(&f->q, 0, bytes, 32, 1) == 0, "TX input required and copied");
	complete(f, 0, 1);
	check(vcdmapoll(&f->q, &done, 1) == VcFault && f->q.core.posted == 1,
		"modern TX completion must report zero used length");
	releasequeue(f);
}

static void
runtimeprepare(Fixture *f, VcQueueInfo *info)
{
	int i;

	init(f, 4, 1, 32); f->runtime = 1; bindqueue(f, 1);
	check(vcmmstart(&f->m) == VcInvalid && vcmmkick(&f->m, &f->q.info) == VcInvalid &&
		f->notifications == 0, "no DRIVER_OK with incomplete queues and no early notifications");
	for(i = 0; i < 4; i++){
		if(i == 2) info[i] = f->q.info;
		else{
			check(vcmmqueue(&f->m, i, &info[i]) == 0, "runtime queue discovery");
			check(vcmmenable(&f->m, &info[i], 0x10000+i*0x1000,
				0x10100+i*0x1000, 0x10200+i*0x1000) == 0, "configure all mock data/control rings");
		}
	}
}

static void
runtime(Fixture *f)
{
	VcQueueInfo info[4], forged;
	VcDmaDone done;
	Vcu8 output[32];
	int i, reads, writes;

	runtimeprepare(f, info);
	check(vcmmhealth(&f->m) == 0 && vcdmapost(&f->q, 0, nil, 32, 1) == 0 &&
		vcmmstart(&f->m) == 0 && f->m.state == VcMmRunning &&
		vcmmstart(&f->m) == VcInvalid, "ordered one-shot start after owned RX publication");
	for(i = 0; i < 4; i++) check(vcmmkick(&f->m, &info[i]) == 0 && f->notifyid == i &&
		f->notifyoffset == 2*i, "notifications use cached exact queue location");
	for(i = 0; i < 5; i++){
		forged = info[2]; reads = f->reads; writes = f->writes;
		if(i == 0) forged.epoch++;
		if(i == 1) forged.id = 4;
		if(i == 2) forged.size++;
		if(i == 3) forged.notify++;
		if(i == 4) forged.notify = 64;
		check(vcmmkick(&f->m, &forged) == VcInvalid && f->reads == reads && f->writes == writes,
			"forged/stale notify metadata rejected before MMIO");
	}
	check(vcmmhealth(&f->m) == 0, "stable running health");
	complete(f, 0, 1);
	check(vcdmapoll(&f->q, &done, 1) == 1 && vcdmatake(&f->q, &done, output, 32) == 1,
		"running DMA state remains usable after DRIVER_OK");
	f->status |= VcNeedsReset;
	check(vcmmkick(&f->m, &info[2]) == VcFault && f->notifications == 4 &&
		vcdmapost(&f->q, 0, nil, 32, 1) == VcFault,
		"NEEDS_RESET blocks notification and leases remain until full reset");
	releasequeue(f);
	check(!f->m.enabled[0] && !f->m.enabled[2] && f->m.configured[2].epoch == 0,
		"reset clears configured queue identities");
	for(i = 0; i < 3; i++){
		init(f, 4, 1, 32); bindqueue(f, 1);
		if(i == 0) f->portcount = 2;
		if(i == 1) f->changes = 100;
		if(i == 2) f->status = 0;
		check(vcmmhealth(&f->m) == VcFault && f->m.state == VcMmFault,
			"changed layout, unstable generation and unexpected reset are health faults");
		releasequeue(f);
	}
}

static void
runtimefaults(Fixture *f)
{
	VcQueueInfo info[4];
	int command, at, count, result;

	for(command = 0; command < 3; command++){
		count = command == 2 ? 5 : 3; /* start/kick: two reads + one write; health: five reads */
		for(at = 0; at < count; at++){
			runtimeprepare(f, info);
			if(command == 1) check(vcmmstart(&f->m) == 0, "prepare notify failure fixture");
			if(command != 2 && at == 2) f->failwrite = f->writes+1;
			else f->failread = f->reads+at+1;
			if(command == 0) result = vcmmstart(&f->m);
			else if(command == 1) result = vcmmkick(&f->m, &info[2]);
			else result = vcmmhealth(&f->m);
			check(result == VcFault && f->m.state == VcMmFault && f->frees == 0 &&
				vcdmapost(&f->q, 0, nil, 32, 1) == VcFault,
				"every runtime access failure retains arenas and blocks worker DMA operations");
			releasequeue(f);
		}
	}
}

static void
wrap(Fixture *f)
{
	VcDmaDone done, stale;
	Vcu8 output;
	int i;
	init(f, 1, 1, 1); bindqueue(f, 1);
	memset(&stale, 0, sizeof stale);
	for(i = 0; i < 65538; i++){
		check(vcdmapost(&f->q, 0, nil, 1, 42) == 0, "wrap fixture post");
		*((Vcu8*)f->q.memory.base+f->q.layout.buffers) = i;
		complete(f, 0, 1);
		check(vcdmapoll(&f->q, &done, 1) == 1, "wrap fixture completion");
		if(i == 0) stale = done;
		else check(vcdmatake(&f->q, &stale, &output, 1) == VcInvalid,
			"old receipt cannot reap same slot/cookie/length after repost");
		check(vcdmatake(&f->q, &done, &output, 1) == 1 && output == (Vcu8)i,
			"wrapped avail/used indices retain correct payload");
	}
	check(f->q.avail == 2 && f->q.core.used == 2, "actual 16-bit index wraparound");
	f->q.serial[0] = ~(Vcu64)0;
	check(vcdmapost(&f->q, 0, nil, 1, 42) == VcFault, "nonwrapping receipt serial exhaustion");
	releasequeue(f);
}

#ifdef QGA_HOST_TEST
int main(void)
#else
void main(void)
#endif
{
	Fixture *f;
	int size;
	f = malloc(sizeof *f);
	check(f != nil, "checked heap fixture avoids large native stack state");
	layouts(); allocations(f); setupfaults(f);
	for(size = 1; size <= VcQueueMax; size *= 2){
		roundtrip(f, size, 1, size == 256 ? 4096 : 136);
		roundtrip(f, size, 0, size == 256 ? 4096 : 136);
	}
	completionfaults(f); arguments(f); runtime(f); runtimefaults(f); wrap(f);
	free(f);
#ifdef QGA_HOST_TEST
	printf("virtio DMA queues: %d checks passed (simulated coherent memory, no hardware)\n", checks);
	return 0;
#else
	print("virtio DMA queues: %d checks passed (simulated coherent memory, no hardware)\n", checks);
	exits(nil);
#endif
}
