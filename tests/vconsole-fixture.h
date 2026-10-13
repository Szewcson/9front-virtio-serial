#ifndef QGA9_VCONSOLE_FIXTURE_H
#define QGA9_VCONSOLE_FIXTURE_H
/* SPDX-License-Identifier: MIT */
/* Real pump/DMA/MMIO helpers with simulated coherent rings and registers. */
#include "../kernel/vconsole-pump.h"
#ifdef QGA_HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#define USED(x) ((void)(x))
#endif
typedef struct Fixture Fixture;
struct Fixture {
	VcPump p;
	VcMmio m;
	VcDmaQueue q[VcQueuesMax], *queue[VcQueuesMax];
	void *raw[VcQueuesMax];
	Vcu64 common[7], notify[VcQueuesMax], isr, device;
	Vcu32 regs[VcQueuesMax][14], status;
	int count, selected, allocs, frees, reads, writes, kicks, failread, failwrite;
	int badstart, failkick;
	Vcu16 consumed[VcQueuesMax];
};
static int checks;
#ifndef VC_FIXTURE_BUDGET
#define VC_FIXTURE_BUDGET vcpumpbudget
#endif

static void
check(int ok, const char *name)
{
	checks++;
	if(ok) return;
#ifdef QGA_HOST_TEST
	fprintf(stderr, "FAIL: %s\n", name); exit(1);
#else
	sysfatal("FAIL: %s", (char*)name);
#endif
}

static void
put16(Vcu8 *p, Vcu16 v)
{
	p[0] = v; p[1] = v>>8;
}

static void
put32(Vcu8 *p, Vcu32 v)
{
	int i;
	for(i = 0; i < 4; i++) p[i] = v>>(8*i);
}

static Vcu32
get32(const Vcu8 *p)
{
	return p[0]|(Vcu32)p[1]<<8|(Vcu32)p[2]<<16|(Vcu32)p[3]<<24;
}

static int
dmaalloc(void *aux, int bytes, int align, VcDmaMemory *out)
{
	Fixture *f;
	int id;
	Vcuptr base;
	f = aux; id = f->allocs++;
	check(id < f->count && bytes > 0 && bytes <= VcDmaMax && align == 16,
		"bounded planned allocation");
	f->raw[id] = malloc(bytes+32);
	check(f->raw[id] != nil, "checked fixture allocation");
	base = ((Vcuptr)f->raw[id]+15)&~(Vcuptr)15;
	out->base = (void*)base; out->bytes = bytes;
	out->physical = 0x1234000000ULL+(Vcu64)id*0x200000;
	return 0;
}

static void
dmafree(void *aux, const VcDmaMemory *memory)
{
	Fixture *f;
	int id;
	f = aux;
	id = (memory->physical-0x1234000000ULL)/0x200000;
	check(id >= 0 && id < f->count && f->raw[id] != nil, "each acquired allocation freed once");
	free(f->raw[id]); f->raw[id] = nil; f->frees++;
}

static void
fence(void *aux)
{
	USED(aux);
}

static void*
mmmap(void *aux, Vcu64 address, int bytes)
{
	USED(aux); USED(address); USED(bytes); return nil;
}

static void
mmunmap(void *aux, void *base, int bytes)
{
	USED(aux); USED(base); USED(bytes);
}

static void
pausems(void *aux)
{
	USED(aux);
}

static void
accesscheck(Fixture *f, void *base, int offset, int width)
{
	int bytes;
	bytes = base == f->common ? 56 : base == f->notify ? sizeof f->notify : 8;
	check((base == f->common || base == f->notify || base == &f->device) &&
		offset >= 0 && offset <= bytes-width && (width == 1 || width == 2 || width == 4) &&
		(offset&(width-1)) == 0, "checked MMIO extent and width");
}

static int
mmread(void *aux, void *base, int offset, int width, Vcu32 *out)
{
	Fixture *f;
	f = aux; accesscheck(f, base, offset, width);
	if(++f->reads == f->failread) return VcFault;
	if(base == &f->device){ check(offset == 4 && width == 4, "stable max ports read"); *out = f->m.ports; }
	else if(offset == VcRegStatus) *out = f->status;
	else if(offset == VcRegGeneration) *out = 0;
	else if(offset == VcRegQueueNotify) *out = f->selected;
	else *out = f->regs[f->selected][offset/4];
	return 0;
}

static int
mmwrite(void *aux, void *base, int offset, int width, Vcu32 value)
{
	Fixture *f;
	int i, j;
	f = aux; accesscheck(f, base, offset, width);
	f->writes++;
	if(base == f->notify){
		check(width == 2 && value < (Vcu32)f->count && offset == (int)value*2 && f->status == 15,
			"notify exact queue ID only after DRIVER_OK");
		f->kicks++;
		if(f->failkick) return VcFault; /* write may already have applied */
	}else if(offset == VcRegStatus){
		if(value == 15){
			for(i = 0; i < f->count; i += 2)
				for(j = 0; j < f->q[i].layout.size; j++)
					check(f->p.deferred && i != 2 ? f->q[i].life[j] == VcDmaFree :
						f->q[i].life[j] == VcDmaPosted &&
						f->q[i].core.slot[j].cookie == (i == 2 ? f->m.epoch : 0),
						"control RX ready; data RX either primed or explicitly deferred");
			if(f->badstart) return VcFault;
		}
		f->status = value;
	}else if(offset == VcRegQueueSelect){
		check(value < (Vcu32)f->count, "bounded selector"); f->selected = value;
	}else f->regs[f->selected][offset/4] = value;
	return f->writes == f->failwrite ? VcFault : 0;
}

static Fixture*
fixture(int ports, int size)
{
	static const VcMmOps mmops = {mmmap, mmunmap, mmread, mmwrite, fence, pausems};
	static const VcDmaOps dmaops = {dmaalloc, dmafree, fence};
	Fixture *f;
	VcQueueInfo info[VcQueuesMax];
	VcPumpBudget budget;
	int i;
	f = malloc(sizeof *f);
	check(f != nil, "heap fixture avoids native kernel-sized stack objects");
	memset(f, 0, sizeof *f);
	f->count = 2*ports+2;
	check(vcmminit(&f->m, &mmops, f) == 0, "fresh MMIO");
	f->m.state = VcMmFeatures; f->m.ports = ports; f->m.queues = f->count;
	f->m.map[0] = f->common; f->m.map[1] = f->notify;
	f->m.map[2] = &f->isr; f->m.map[3] = &f->device;
	f->m.length[0] = 56; f->m.length[1] = sizeof f->notify;
	f->m.length[2] = 1; f->m.length[3] = 8;
	f->m.pci.region[1].address = 0x900000;
	f->m.pci.region[1].length = sizeof f->notify; f->m.pci.multiplier = 2;
	f->status = 11;
	memset(info, 0, sizeof info);
	for(i = 0; i < f->count; i++){
		f->regs[i][VcRegQueueSize/4] = size;
		info[i].id = i; info[i].size = size; info[i].notify = i*2; info[i].epoch = f->m.epoch;
	}
	check(VC_FIXTURE_BUDGET(&budget, info, f->count, ports, VcPumpDataCapacity, VcPumpBudgetMax) == 0 &&
		f->allocs == 0, "budget checked BEFORE any acquisition");
	for(i = 0; i < f->count; i++){
		check(vcdmainit(&f->q[i], &dmaops, f) == 0 &&
			vcdmaalloc(&f->q[i], &info[i], (i&1) == 0, budget.capacity[i]) == 0 &&
			vcdmabind(&f->q[i], &f->m) == 0, "owned configured queue");
		f->queue[i] = &f->q[i];
	}
	check(vcpumpinit(&f->p, &f->m, f->queue, f->count, VcPumpBudgetMax) == 0,
		"pump accepts genuinely bound fresh queue set");
	return f;
}

static void
destroy(Fixture *f)
{
	int i;
	check(vcpumpstop(&f->p) == 0 && vcpumpstop(&f->p) == 0, "idempotent logical stop");
	check(f->frees == 0, "stop/fault never frees an exposed queue");
	check(vcmmreset(&f->m) == 0, "confirmed full reset before release");
	for(i = f->count-1; i >= 0; i--)
		check(vcdmadrop(&f->q[i], 1) == 0, "test has no IRQ/Chan refs, reset gates DMA release");
	check(f->frees == f->allocs, "all acquired storage released exactly once");
	free(f);
}

/* Simulated device consumes avail in order; tests never modify CPU-owned state. */
static int
complete(Fixture *f, int qi, const Vcu8 *data, int length)
{
	VcDmaQueue *q;
	Vcu8 *base, *entry;
	Vcu16 index, available;
	Vcu32 id;
	q = &f->q[qi]; base = q->memory.base;
	available = *(Vcu16*)(base+q->layout.avail+2);
	if(f->consumed[qi] == available){
#ifdef QGA_HOST_TEST
		fprintf(stderr, "unpublished fixture queue %d: avail=%d consumed=%d\n", qi, (int)available, (int)f->consumed[qi]);
#else
		fprint(2, "unpublished fixture queue %d: avail=%d consumed=%d\n", qi, (int)available, (int)f->consumed[qi]);
#endif
	}
	check(f->consumed[qi] != available, "device only consumes a published descriptor");
	id = *(Vcu16*)(base+q->layout.avail+4+2*(f->consumed[qi]&(q->layout.size-1)));
	check(id < (Vcu32)q->layout.size && q->life[id] == VcDmaPosted, "device targets a posted lease");
	f->consumed[qi]++;
	if(q->core.receive){
		check(length >= 0 && length <= q->layout.capacity, "device payload bounded to posted capacity");
		if(length) memmove(base+q->layout.buffers+id*q->layout.stride, (void*)data, length);
	}else check(length == 0, "modern TX used length is zero");
	index = *(Vcu16*)(base+q->layout.used+2);
	entry = base+q->layout.used+4+8*(index&(q->layout.size-1));
	put32(entry, id); put32(entry+4, length);
	*(Vcu16*)(base+q->layout.used+2) = index+1;
	return id;
}

static void
event(Fixture *f, int id, int eventid, int value, const char *name)
{
	Vcu8 frame[VcControlMax];
	int length;
	memset(frame, 0, sizeof frame);
	put32(frame, id); put16(frame+4, eventid); put16(frame+6, value);
	length = 8;
	if(name != nil){ length += strlen((char*)name); memmove(frame+8, (void*)name, length-8); }
	complete(f, 2, frame, length);
}

static void
drain(Fixture *f)
{
	int i, result;
	for(i = 0; i < 1024; i++){
		result = vcpumpstep(&f->p, VcQueueMax);
		check(result >= 0, "bounded worker step");
		if(!(result&VcPumpAgain)) return;
	}
	check(0, "no CPU busy loop on TX backpressure");
}

static void
tx(Fixture *f, int id, int eventid, int value)
{
	VcDmaQueue *q;
	Vcu8 *base, *frame;
	int slot;
	q = &f->q[3]; base = q->memory.base;
	check(f->consumed[3] != q->avail, "outgoing control reply exists");
	slot = *(Vcu16*)(base+q->layout.avail+4+2*(f->consumed[3]&(q->layout.size-1)));
	check(slot >= 0 && slot < q->layout.size, "bounded outgoing slot");
	frame = base+q->layout.buffers+slot*q->layout.stride;
	check(get32(frame) == (Vcu32)id && frame[4] == eventid && frame[5] == 0 &&
		frame[6] == value && frame[7] == 0, "outgoing control reply bytes/order");
	complete(f, 3, nil, 0);
}

#endif
