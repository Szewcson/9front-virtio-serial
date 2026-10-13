/* SPDX-License-Identifier: MIT */
#include "vconsole-dma.h"

static void
put16(Vcu8 *p, Vcu16 value)
{
	p[0] = value;
	p[1] = value>>8;
}

int
vcdmalayout(VcDmaLayout *out, int size, int capacity)
{
	VcDmaLayout l;

	if(out == nil || size < 1 || size > VcQueueMax || (size&(size-1)) != 0 ||
	   capacity < 1 || capacity > VcBufferMax)
		return VcInvalid;
	memset(&l, 0, sizeof l);
	l.size = size;
	l.capacity = capacity;
	l.stride = (capacity+15)&~15;
	l.avail = size*16;
	/* Reserve both optional event words, although EVENT_IDX is not negotiated. */
	l.used = (l.avail+6+size*2+3)&~3;
	l.buffers = (l.used+6+size*8+15)&~15;
	l.bytes = l.buffers+size*l.stride;
	if(l.bytes > VcDmaMax) return VcInvalid;
	*out = l;
	return 0;
}

static int
valid(const VcDmaQueue *q)
{
	VcDmaLayout l;

	return q != nil && q->state >= VcDmaAllocated && q->state <= VcDmaFault &&
		q->ops.free != nil && q->ops.fence != nil && q->memory.base != nil &&
		vcdmalayout(&l, q->layout.size, q->layout.capacity) == 0 &&
		memcmp(&l, &q->layout, sizeof l) == 0 && q->memory.bytes == l.bytes &&
		((Vcuptr)q->memory.base&15) == 0 &&
		(Vcuptr)q->memory.base <= ~(Vcuptr)0-(Vcu32)l.bytes &&
		q->memory.physical != 0 && (q->memory.physical&15) == 0 &&
		q->memory.physical <= ~(Vcu64)0-(Vcu32)l.bytes &&
		q->info.epoch != 0 && q->info.id >= 0 && q->info.id < 2*VcPortMax+2 &&
		q->info.size == l.size && q->core.epoch == q->info.epoch &&
		q->core.size == l.size && q->core.posted >= 0 && q->core.posted <= l.size &&
		q->core.receive == ((q->info.id&1) == 0);
}

static int
live(VcDmaQueue *q)
{
	if(!valid(q)) return VcInvalid;
	if(q->state == VcDmaFault) return VcFault;
	if(q->state != VcDmaBound) return VcInvalid;
	if(q->mmio == nil || (q->mmio->state != VcMmFeatures && q->mmio->state != VcMmRunning) ||
	   q->mmio->epoch != q->info.epoch || q->core.failed){
		q->state = VcDmaFault;
		return VcFault;
	}
	return 0;
}

int
vcdmainit(VcDmaQueue *q, const VcDmaOps *ops, void *aux)
{
	if(q == nil || ops == nil || ops->alloc == nil || ops->free == nil || ops->fence == nil)
		return VcInvalid;
	memset(q, 0, sizeof *q);
	q->ops = *ops;
	q->aux = aux;
	return 0;
}

int
vcdmaalloc(VcDmaQueue *q, const VcQueueInfo *info, int receive, int capacity)
{
	VcDmaLayout l;
	VcDmaMemory memory;
	int result;

	if(q == nil || q->state != VcDmaEmpty || q->memory.base != nil ||
	   q->ops.alloc == nil || q->ops.free == nil || q->ops.fence == nil ||
	   info == nil || info->epoch == 0 || info->id < 0 || info->id >= 2*VcPortMax+2 ||
	   (receive != 0 && receive != 1) || receive != ((info->id&1) == 0) ||
	   vcdmalayout(&l, info->size, capacity) < 0)
		return VcInvalid;
	memset(&memory, 0, sizeof memory);
	result = q->ops.alloc(q->aux, l.bytes, VcDmaAlign, &memory);
	if(result != 0 || memory.base == nil || memory.bytes != l.bytes ||
	   ((Vcuptr)memory.base&15) != 0 ||
	   (Vcuptr)memory.base > ~(Vcuptr)0-(Vcu32)l.bytes ||
	   memory.physical == 0 || (memory.physical&15) != 0 ||
	   memory.physical > ~(Vcu64)0-(Vcu32)l.bytes){
		if(memory.base != nil) q->ops.free(q->aux, &memory);
		return VcFault;
	}
	memset(memory.base, 0, l.bytes);
	vcqinit(&q->core, l.size, receive); /* prevalidated, fresh/unpublished */
	q->core.epoch = info->epoch;
	q->memory = memory;
	q->layout = l;
	q->info = *info;
	q->state = VcDmaAllocated;
	return 0;
}

int
vcdmabind(VcDmaQueue *q, VcMmio *m)
{
	int result;

	if(!valid(q) || q->state != VcDmaAllocated || m == nil ||
	   m->state != VcMmFeatures || m->epoch != q->info.epoch)
		return VcInvalid;
	/* Conservative before the first MMIO call: even a failed/partially applied
	 * register write must never make the allocation eligible for direct free. */
	q->mmio = m;
	q->exposed = 1;
	result = vcmmenable(m, &q->info, q->memory.physical,
		q->memory.physical+q->layout.avail, q->memory.physical+q->layout.used);
	q->state = result == 0 ? VcDmaBound : VcDmaFault;
	return result;
}

int
vcdmapost(VcDmaQueue *q, int id, const Vcu8 *input, int length, Vcu64 cookie)
{
	Vcu8 *base, *buffer;
	int result;

	if((result = live(q)) < 0) return result;
	if(id < 0 || id >= q->layout.size || q->life[id] != VcDmaFree || q->core.slot[id].posted ||
	   length < 1 || length > q->layout.capacity ||
	   (q->core.receive ? input != nil : input == nil))
		return VcInvalid;
	if(q->serial[id] == ~(Vcu64)0){ q->state = VcDmaFault; return VcFault; }
	base = q->memory.base;
	buffer = base+q->layout.buffers+id*q->layout.stride;
	memset(buffer, 0, q->layout.stride);
	if(!q->core.receive) memmove(buffer, (void*)input, length);
	result = vcqpost(&q->core, id, q->memory.physical+q->layout.buffers+id*q->layout.stride,
		length, cookie, base+id*16, 16);
	if(result < 0) return result;
	q->serial[id]++;
	q->life[id] = VcDmaPosted;
	put16(base+q->layout.avail+4+2*(q->avail&(q->layout.size-1)), id);
	q->ops.fence(q->aux);
	q->avail++;
	/* PC64 is little-endian. A single aligned store prevents a torn idx, unlike
	 * byte-wise LE encoding; descriptors/avail entries precede this publication. */
	*(volatile Vcu16*)(base+q->layout.avail+2) = q->avail;
	q->ops.fence(q->aux);
	return 0;
}

int
vcdmapoll(VcDmaQueue *q, VcDmaDone *out, int capacity)
{
	const volatile Vcu8 *p;
	VcUsed *used;
	Vcu16 index;
	int result, i, count;

	if((result = live(q)) < 0) return result;
	if(capacity < 0) return VcInvalid;
	p = (Vcu8*)q->memory.base+q->layout.used;
	index = *(const volatile Vcu16*)(p+2);
	q->ops.fence(q->aux); /* acquire entries/payload after the observed idx */
	count = (Vcu16)(index-q->core.used);
	if(count > q->layout.size || count > q->core.posted){
		q->state = VcDmaFault;
		q->core.failed = 1;
		return VcFault;
	}
	if(count == 0) return 0;
	if(out == nil || capacity < count) return VcInvalid;
	for(i = 0; i < q->layout.size*8; i++) q->snapshot[i] = p[4+i];
	q->ops.fence(q->aux);
	/* Only the immutable CPU copy reaches the core's two-pass validator. */
	used = q->completed;
	result = vcqconsume(&q->core, index, q->snapshot, q->layout.size*8, used, count);
	if(result < 0){ q->state = VcDmaFault; return VcFault; }
	for(i = 0; i < count; i++){
		q->life[used[i].id] = VcDmaReady;
		memset(&out[i], 0, sizeof out[i]);
		out[i].used = used[i];
		out[i].serial = q->serial[used[i].id];
		q->ready[used[i].id] = out[i];
	}
	return count;
}

int
vcdmatake(VcDmaQueue *q, const VcDmaDone *done, Vcu8 *out, int capacity)
{
	const volatile Vcu8 *p;
	VcDmaDone *saved;
	int result, id, length, i;

	if((result = live(q)) < 0) return result;
	if(done == nil || done->used.id >= (Vcu32)q->layout.size || capacity < 0)
		return VcInvalid;
	id = done->used.id;
	saved = &q->ready[id];
	if(q->life[id] != VcDmaReady || done->serial != saved->serial ||
	   done->used.epoch != saved->used.epoch || done->used.cookie != saved->used.cookie ||
	   done->used.length != saved->used.length)
		return VcInvalid;
	if(saved->used.length > (Vcu32)q->layout.capacity) return VcInvalid;
	length = saved->used.length;
	if(capacity < length || (length > 0 && out == nil))
		return VcInvalid;
	p = (Vcu8*)q->memory.base+q->layout.buffers+id*q->layout.stride;
	q->ops.fence(q->aux);
	for(i = 0; i < length; i++) out[i] = p[i];
	q->ops.fence(q->aux);
	q->life[id] = VcDmaFree;
	memset(saved, 0, sizeof *saved);
	return length;
}

int
vcdmadrop(VcDmaQueue *q, int drained)
{
	VcDmaOps ops;
	void *aux;

	if(!valid(q) || drained != 1 || (q->exposed &&
	   (q->mmio == nil || q->mmio->state != VcMmReset || q->mmio->epoch <= q->info.epoch)))
		return VcInvalid;
	q->ops.fence(q->aux);
	q->ops.free(q->aux, &q->memory);
	ops = q->ops;
	aux = q->aux;
	memset(q, 0, sizeof *q);
	q->ops = ops;
	q->aux = aux;
	return 0;
}
