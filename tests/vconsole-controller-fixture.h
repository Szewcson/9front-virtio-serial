#ifndef QGA9_VCONSOLE_CONTROLLER_FIXTURE_H
#define QGA9_VCONSOLE_CONTROLLER_FIXTURE_H
/* SPDX-License-Identifier: MIT */
#include "../kernel/vconsole-controller.h"
#ifdef QGA_HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#define USED(x) ((void)(x))
#endif
typedef struct Bus Bus;
struct Bus {
	VcController c;
	VcMmio mm;
	VcIrq irq[2];
	Vcu64 common[7], notify[VcQueuesMax], isr, device;
	Vcu32 regs[VcQueuesMax][14], status;
	void *metadata[2], *raw[VcQueuesMax];
	int metabytes[2], metacalls, metafrees, dmaattempts, dmacalls, dmafrees;
	int ports, count, size, select, feature, reads, writes, kicks, resets, unmaps;
	int failmeta, faildma, failread, failwrite, failreset, failbind, failstart;
	int badmeta, badirq, bound, freereset, lockdepth, irqdepth;
	Vcu16 consumed[VcQueuesMax];
#ifdef QGA_HOST_TEST
	pthread_mutex_t gate, irqlock, wait;
	pthread_cond_t cond;
	int block, entered, released, threadresult;
	VcCtlRef work;
#endif
};
static int checks;

static void
check(int ok, const char *name)
{
	/* This counter is used by the supervisor only in threaded cases. */
	checks++;
	if(ok) return;
#ifdef QGA_HOST_TEST
	fprintf(stderr, "FAIL: %s\n", name); exit(1);
#else
	sysfatal("FAIL: %s", (char*)name);
#endif
}

static void
must(int ok)
{
	if(ok) return;
#ifdef QGA_HOST_TEST
	abort();
#else
	sysfatal("fixture invariant");
#endif
}

static void
gatelock(void *aux)
{
	Bus *b;
	b = aux;
#ifdef QGA_HOST_TEST
	must(pthread_mutex_lock(&b->gate) == 0);
#endif
	must(b->lockdepth++ == 0);
}

static void
gateunlock(void *aux)
{
	Bus *b;
	b = aux; must(--b->lockdepth == 0);
#ifdef QGA_HOST_TEST
	must(pthread_mutex_unlock(&b->gate) == 0);
#endif
}

static void
irqlock(void *aux)
{
	Bus *b;
	b = aux;
#ifdef QGA_HOST_TEST
	must(pthread_mutex_lock(&b->irqlock) == 0);
#endif
	must(b->irqdepth++ == 0);
}

static void
irqunlock(void *aux)
{
	Bus *b;
	b = aux; must(--b->irqdepth == 0);
#ifdef QGA_HOST_TEST
	must(pthread_mutex_unlock(&b->irqlock) == 0);
#endif
}

static void
fence(void *aux)
{
	USED(aux);
}

static int
isrread(void *aux, Vcu32 *out)
{
	Bus *b;
	b = aux; *out = b->badirq ? 255 : VcIrqQueue; return 0;
}

/* Deterministic blocked operation/wake to test real joining, not sleeps. */
static void
block(Bus *b, int kind)
{
#ifdef QGA_HOST_TEST
	must(pthread_mutex_lock(&b->wait) == 0);
	if(b->block == kind){
		b->entered = 1; must(pthread_cond_broadcast(&b->cond) == 0);
		while(!b->released) must(pthread_cond_wait(&b->cond, &b->wait) == 0);
	}
	must(pthread_mutex_unlock(&b->wait) == 0);
#else
	USED(b); USED(kind);
#endif
}

static void
wake(void *aux)
{
	block(aux, 3);
}

static void*
metaalloc(void *aux, int bytes)
{
	Bus *b;
	void *p;
	int i;
	b = aux; must(bytes > 0 && b->c.budget.total <= VcPumpBudgetMax);
	if(++b->metacalls == b->failmeta) return nil;
	for(i = 0; i < 2; i++) if(b->metadata[i] == nil) break;
	must(i < 2);
	p = malloc(bytes+16); must(p != nil);
	if(b->badmeta) p = (Vcu8*)p+1;
	b->metadata[i] = p; b->metabytes[i] = bytes;
	return p;
}

static void
metafree(void *aux, void *p, int bytes)
{
	Bus *b;
	int i;
	b = aux;
	for(i = 0; i < 2; i++) if(b->metadata[i] == p) break;
	must(i < 2 && b->metabytes[i] == bytes && b->raw[0] == nil);
	if(b->badmeta) p = (Vcu8*)p-1;
	free(p); b->metadata[i] = nil; b->metafrees++;
}

static int
dmaalloc(void *aux, int bytes, int align, VcDmaMemory *out)
{
	Bus *b;
	int i;
	b = aux;
	must(bytes > 0 && align == 16 && b->bound == 0);
	if(++b->dmaattempts == b->faildma) return VcFault;
	for(i = 0; i < b->count; i++) if(b->raw[i] == nil) break;
	must(i < b->count);
	b->raw[i] = malloc(bytes+32); must(b->raw[i] != nil);
	out->base = (void*)(((Vcuptr)b->raw[i]+15)&~(Vcuptr)15);
	out->bytes = bytes; out->physical = 0x1234000000ULL+(Vcu64)i*0x200000;
	b->dmacalls++;
	return 0;
}

static void
dmafree(void *aux, const VcDmaMemory *m)
{
	Bus *b;
	int i;
	b = aux; i = (m->physical-0x1234000000ULL)/0x200000;
	must(i >= 0 && i < b->count && b->raw[i] != nil);
	/* A bind attempt (even one returning failure) requires NEW observed reset. */
	must(!b->bound || (b->status == 0 && b->mm.state == VcMmReset && b->resets > b->freereset));
	free(b->raw[i]); b->raw[i] = nil; b->dmafrees++;
}

static void*
map(void *aux, Vcu64 address, int bytes)
{
	USED(aux); USED(address); USED(bytes); return nil;
}

static void
unmap(void *aux, void *p, int bytes)
{
	Bus *b;
	b = aux; must(p != nil && bytes > 0); b->unmaps++;
}

static void
pausems(void *aux)
{
	USED(aux);
}

static void
checkaccess(Bus *b, void *base, int off, int width)
{
	int bytes;
	bytes = base == b->common ? 56 : base == b->notify ? sizeof b->notify : 8;
	must((base == b->common || base == b->notify || base == &b->device) &&
		off >= 0 && off <= bytes-width && (width == 1 || width == 2 || width == 4) &&
		(off&(width-1)) == 0);
}

static int
readreg(void *aux, void *base, int off, int width, Vcu32 *out)
{
	Bus *b;
	b = aux; checkaccess(b, base, off, width);
	if(++b->reads == b->failread) return VcFault;
	block(b, 1);
	if(base == &b->device) *out = b->ports;
	else switch(off){
	case VcRegStatus: *out = b->status; break;
	case VcRegDevFeature: *out = b->feature == 0 ? 2 : 1; break;
	case VcRegGeneration: *out = 0; break;
	case VcRegQueues: *out = b->count; break;
	case VcRegQueueSize: *out = b->regs[b->select][off/4]; break;
	case VcRegQueueNotify: *out = b->select; break;
	default: *out = b->regs[b->select][off/4]; break;
	}
	return 0;
}

static int
writereg(void *aux, void *base, int off, int width, Vcu32 v)
{
	Bus *b;
	int i;
	b = aux; checkaccess(b, base, off, width); b->writes++;
	if(base == b->notify){
		must(width == 2 && off == (int)v*2 && v < (Vcu32)b->count && b->status == 15);
		b->kicks++;
	}else switch(off){
	case VcRegStatus:
		if(v == 0){
			if(b->failreset) return VcFault;
			b->resets++; b->bound = 0; b->select = 0;
			memset(b->regs, 0, sizeof b->regs);
			memset(b->consumed, 0, sizeof b->consumed);
			for(i = 0; i < b->count; i++) b->regs[i][VcRegQueueSize/4] = b->size;
		}
		if(v == 15){ block(b, 2); if(b->failstart) return VcFault; }
		b->status = v; break;
	case VcRegDevSelect: b->feature = v; break;
	case VcRegQueueSelect: must(v < (Vcu32)b->count); b->select = v; break;
	case VcRegQueueDesc:
		b->bound++;
		if(b->failbind == b->bound){ b->failreset = 1; return VcFault; }
		b->freereset = b->resets;
		b->regs[b->select][off/4] = v; break;
	default: b->regs[b->select][off/4] = v; break;
	}
	return b->writes == b->failwrite ? VcFault : 0;
}

static Bus*
bus(int ports, int size)
{
	static const VcMmOps mmops = {map, unmap, readreg, writereg, fence, pausems};
	static const VcDmaOps dmaops = {dmaalloc, dmafree, fence};
	static const VcCtlOps ops = {gatelock, gateunlock, metaalloc, metafree};
	Bus *b;
	b = malloc(sizeof *b); check(b != nil, "heap controller fixture");
	memset(b, 0, sizeof *b);
#ifdef QGA_HOST_TEST
	must(pthread_mutex_init(&b->gate, nil) == 0 && pthread_mutex_init(&b->irqlock, nil) == 0 &&
		pthread_mutex_init(&b->wait, nil) == 0 && pthread_cond_init(&b->cond, nil) == 0);
#endif
	b->ports = ports; b->count = 2*ports+2; b->size = size;
	check(vcmminit(&b->mm, &mmops, b) == 0, "fresh mapped controller input");
	b->mm.state = VcMmMapped;
	b->mm.map[0] = b->common; b->mm.length[0] = 56;
	b->mm.map[1] = b->notify; b->mm.length[1] = sizeof b->notify;
	b->mm.map[2] = &b->isr; b->mm.length[2] = 1;
	b->mm.map[3] = &b->device; b->mm.length[3] = 8;
	b->mm.pci.region[1].address = 0x100000;
	b->mm.pci.region[1].length = sizeof b->notify; b->mm.pci.multiplier = 2;
	check(vccinit(&b->c, &b->mm, &ops, b, &dmaops, b, 91) == 0, "fresh lifetime manager");
	return b;
}

static VcIrq*
irq(Bus *b, int index)
{
	static const VcIrqOps ops = {irqlock, irqunlock, isrread, fence, wake};
	check(vcirqinit(&b->irq[index], &ops, b, b->mm.epoch) == 0, "fresh pinned IRQ epoch");
	return &b->irq[index];
}

static void
destroy(Bus *b)
{
	int i;
	b->failreset = b->failread = b->failwrite = 0;
	check(vccreset(&b->c) == 0 && vccdetach(&b->c) == 0, "confirmed reset and map detach");
	check(b->dmafrees == b->dmacalls && b->unmaps == 4, "all DMA/maps released exactly once");
	for(i = 0; i < VcQueuesMax; i++) check(b->raw[i] == nil, "no live DMA arena");
	check(b->metadata[0] == nil && b->metadata[1] == nil, "no live metadata blocks");
#ifdef QGA_HOST_TEST
	must(pthread_cond_destroy(&b->cond) == 0 && pthread_mutex_destroy(&b->wait) == 0 &&
		pthread_mutex_destroy(&b->irqlock) == 0 && pthread_mutex_destroy(&b->gate) == 0);
#endif
	free(b);
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

static void
complete(Bus *b, int qi, const Vcu8 *bytes, int length)
{
	VcDmaQueue *q;
	Vcu8 *p;
	int id, pos;
	must(b != nil && qi >= 0 && qi < b->c.count && b->c.queue != nil);
	q = &b->c.queue[qi]; p = q->memory.base;
	id = p[q->layout.avail+4+2*(b->consumed[qi]&(q->layout.size-1))];
	check(b->consumed[qi] != q->avail && q->life[id] == VcDmaPosted,
		"device only consumes published current lease");
	b->consumed[qi]++;
	if(q->core.receive){
		check(length >= 0 && length <= q->layout.capacity && (length == 0 || bytes != nil),
			"bounded simulated RX payload");
		if(length > 0) memmove(p+q->layout.buffers+id*q->layout.stride, (void*)bytes, length);
	}else length = 0;
	pos = q->layout.used+4+8*((b->consumed[qi]-1)&(q->layout.size-1));
	put32(p+pos, id); put32(p+pos+4, length);
	put16(p+q->layout.used+2, b->consumed[qi]);
}

#endif
