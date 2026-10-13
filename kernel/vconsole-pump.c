/* SPDX-License-Identifier: MIT */
#include "vconsole-pump.h"

int
vcpumpbudget(VcPumpBudget *out, const VcQueueInfo *info, int count, int ports,
	int capacity, Vcu64 limit)
{
	VcPumpBudget result;
	VcDmaLayout layout;
	Vcu64 epoch;
	int i, size;

	if(out == nil || info == nil || ports < 1 || ports > VcPortMax ||
	   count != 2*ports+2 || capacity < 1 || capacity > VcBufferMax ||
	   limit == 0 || limit > VcPumpBudgetMax)
		return VcInvalid;
	memset(&result, 0, sizeof result);
	result.ports = ports;
	result.count = count;
	result.metadata = (Vcu64)count*sizeof(VcDmaQueue)+sizeof(VcPump)+sizeof(VcMmio);
	epoch = info[0].epoch;
	for(i = 0; i < count; i++){
		size = i == 2 ? VcControlMax : i == 3 ? 8 : capacity;
		if(epoch == 0 || info[i].epoch != epoch || info[i].id != i ||
		   (info[i].notify&1) != 0 || vcdmalayout(&layout, info[i].size, size) < 0)
			return VcInvalid;
		result.capacity[i] = size;
		result.dma += layout.bytes; /* bounded count/layout, no input-size multiplication */
	}
	result.total = result.metadata+result.dma;
	if(result.total > limit) return VcInvalid;
	*out = result;
	return 0;
}

static int
valid(const VcPump *p)
{
	return p != nil && p->epoch != 0 && p->mmio != nil &&
		p->ports.size >= 1 && p->ports.size <= VcPortMax &&
		p->state >= VcPumpReady && p->state <= VcPumpStopped;
}

static int
pumpfault(VcPump *p)
{
	p->state = VcPumpFault;
	p->ports.failed = 1;
	return VcFault;
}

static int
running(VcPump *p)
{
	if(!valid(p)) return VcInvalid;
	if(p->state == VcPumpFault) return VcFault;
	if(p->state != VcPumpRunning) return VcInvalid;
	if(p->mmio->state != VcMmRunning || p->mmio->epoch != p->epoch)
		return pumpfault(p);
	return 0;
}

int
vcpumpinit(VcPump *p, VcMmio *m, VcDmaQueue *const *queues, int count, Vcu64 limit)
{
	VcDmaQueue *q;
	int i, j, capacity;

	if(p == nil || m == nil || queues == nil || m->state != VcMmFeatures ||
	   m->epoch == 0 || m->ports < 1 || m->ports > VcPortMax || count != 2*m->ports+2)
		return VcInvalid;
	memset(p, 0, sizeof *p);
	capacity = 0;
	for(i = 0; i < count; i++){
		q = queues[i];
		if(q == nil || q->state != VcDmaBound || q->mmio != m || !q->exposed ||
		   q->info.id != i || q->info.epoch != m->epoch || q->info.size != q->layout.size ||
		   q->core.receive != ((i&1) == 0) || q->core.posted != 0 || q->core.used != 0 ||
		   q->avail != 0 || q->layout.size < 1 || q->layout.size > VcQueueMax ||
		   m->enabled[i] != 1 || m->configured[i].id != i ||
		   m->configured[i].epoch != q->info.epoch || m->configured[i].size != q->info.size ||
		   m->configured[i].notify != q->info.notify)
			return VcInvalid;
		for(j = 0; j < q->layout.size; j++)
			if(q->life[j] != VcDmaFree || q->serial[j] != 0 || q->core.slot[j].posted)
				return VcInvalid;
		if(i != 2 && i != 3){
			if(i == 0) capacity = q->layout.capacity;
			if(q->layout.capacity != capacity) return VcInvalid;
		}else if(q->layout.capacity != (i == 2 ? VcControlMax : 8)) return VcInvalid;
		p->info[i] = q->info;
	}
	if(vcpumpbudget(&p->budget, p->info, count, m->ports, capacity, limit) < 0)
		return VcInvalid;
	vcportsinit(&p->ports, m->ports); /* prevalidated fresh object */
	p->mmio = m;
	p->epoch = m->epoch;
	for(i = 0; i < count; i++) p->queue[i] = queues[i];
	p->state = VcPumpReady;
	return 0;
}

static void
enqueue(VcPump *p, Vcu32 id, int event, int value, Vcu64 generation)
{
	VcPumpReply *r;

	r = &p->reply[(p->head+p->count)%VcPumpReplies];
	memset(r, 0, sizeof *r);
	r->id = id;
	r->event = event;
	r->value = value;
	if(event != VcDeviceReady) r->incarnation = p->incarnation[id];
	r->generation = generation;
	p->count++;
}

static int
eligible(const VcPump *p, const VcPumpReply *r)
{
	const VcPort *port;

	if(r->event == VcDeviceReady) return 1;
	port = &p->ports.port[r->id];
	if(!port->present || r->incarnation != p->incarnation[r->id]) return 0;
	if(r->event == VcPortReady) return 1;
	if(r->generation == 0) return port->console; /* mandatory console OPEN=1 */
	return !port->console && port->generation == r->generation && port->guestopen == r->value;
}

static int
freeslot(const VcDmaQueue *q)
{
	int i;
	for(i = 0; i < q->layout.size; i++)
		if(q->life[i] == VcDmaFree) return i;
	return -1;
}

/* Returns one action, zero for actual TX backpressure, or a sticky fault. */
static int
send(VcPump *p)
{
	VcPumpReply *r;
	VcDmaQueue *q;
	int id;

	if(p->count == 0) return 0;
	r = &p->reply[p->head];
	if(eligible(p, r)){
		q = p->queue[3];
		id = freeslot(q);
		if(id < 0) return 0;
		if(vcencode(p->frame, sizeof p->frame, r->id, r->event, r->value) != 8 ||
		   vcdmapost(q, id, p->frame, 8, p->epoch) < 0 || vcmmkick(p->mmio, &q->info) < 0)
			return pumpfault(p);
		if(r->event == VcOpen && r->generation != 0)
			p->advertised[r->id] = r->value ? r->generation : 0;
	}
	memset(r, 0, sizeof *r);
	p->head = (p->head+1)%VcPumpReplies;
	p->count--;
	return 1;
}

int
vcpumpdeferdata(VcPump *p)
{
	if(!valid(p) || p->state != VcPumpReady || p->mmio->state != VcMmFeatures ||
	   p->mmio->epoch != p->epoch) return VcInvalid;
	p->deferred = 1;
	return 0;
}

int
vcpumpstart(VcPump *p)
{
	VcDmaQueue *q;
	int i, j;

	if(!valid(p) || p->state != VcPumpReady) return VcInvalid;
	if(p->mmio->state != VcMmFeatures || p->mmio->epoch != p->epoch) return pumpfault(p);
	for(i = 0; i < p->budget.count; i += 2){
		if(p->deferred && i != 2) continue;
		q = p->queue[i];
		for(j = 0; j < q->layout.size; j++)
			if(vcdmapost(q, j, nil, q->layout.capacity, i == 2 ? p->epoch : 0) < 0)
				return pumpfault(p);
	}
	if(vcmmstart(p->mmio) < 0) return pumpfault(p);
	p->state = VcPumpRunning;
	for(i = 0; i < p->budget.count; i += 2)
		if((!p->deferred || i == 2) && vcmmkick(p->mmio, &p->queue[i]->info) < 0)
			return pumpfault(p);
	enqueue(p, 0, VcDeviceReady, 1, 0);
	return send(p) == 1 ? 0 : pumpfault(p);
}

static int
receive(VcPump *p)
{
	VcDmaDone *done;
	VcControl control;
	VcDmaQueue *q;
	int length;

	if(p->rxhead == p->rxcount || p->count == VcPumpReplies) return 0;
	q = p->queue[2];
	done = &p->rx[p->rxhead];
	length = vcdmatake(q, done, p->frame, sizeof p->frame);
	if(length < 0 || vcdecode(&control, p->frame, length) < 0 ||
	   control.id >= (Vcu32)p->ports.size)
		return pumpfault(p);
	if((control.event == VcAdd || control.event == VcRemove) &&
	   p->incarnation[control.id] == ~(Vcu64)0)
		return pumpfault(p);
	if(vcreceive(&p->ports, p->frame, length) < 0) return pumpfault(p);
	if(control.event == VcAdd || control.event == VcRemove){
		p->incarnation[control.id]++;
		p->advertised[control.id] = 0;
	}
	if(control.event == VcAdd) enqueue(p, control.id, VcPortReady, 1, 0);
	if(control.event == VcConsole) enqueue(p, control.id, VcOpen, 1, 0);
	/* Consume before reusing this RX lease; keep receipt FIFO in used order. */
	if(vcdmapost(q, done->used.id, nil, q->layout.capacity, p->epoch) < 0 ||
	   vcmmkick(p->mmio, &q->info) < 0)
		return pumpfault(p);
	p->rxhead++;
	return 1;
}

int
vcpumpstep(VcPump *p, int budget)
{
	VcDmaQueue *q;
	int result, actions, work;

	if(budget < 1 || budget > VcQueueMax) return VcInvalid;
	if((result = running(p)) < 0) return result;
	if(vcmmhealth(p->mmio) < 0) return pumpfault(p);
	if(p->txhead == p->txcount){
		p->txhead = 0;
		p->txcount = vcdmapoll(p->queue[3], p->tx, VcQueueMax);
		if(p->txcount < 0) return pumpfault(p);
	}
	if(p->rxhead == p->rxcount){
		p->rxhead = 0;
		p->rxcount = vcdmapoll(p->queue[2], p->rx, VcQueueMax);
		if(p->rxcount < 0) return pumpfault(p);
	}
	actions = 0;
	while(actions < budget){
		if(p->txhead < p->txcount){
			if(vcdmatake(p->queue[3], &p->tx[p->txhead], nil, 0) != 0) return pumpfault(p);
			p->txhead++;
			work = 1;
		}else{
			work = send(p);
			if(work == 0) work = receive(p);
			if(work < 0) return work;
		}
		if(work == 0) break;
		actions++;
	}
	result = actions > 0 ? VcPumpProgress : 0;
	q = p->queue[3];
	if(p->txhead < p->txcount ||
	   (p->rxhead < p->rxcount && p->count < VcPumpReplies) ||
	   (p->count > 0 && (!eligible(p, &p->reply[p->head]) || freeslot(q) >= 0)))
		result |= VcPumpAgain;
	return result;
}

int
vcpumpclaim(VcPump *p, VcToken *out)
{
	VcToken token;
	int result;

	if((result = running(p)) < 0) return result;
	if(out == nil || p->count == VcPumpReplies) return VcInvalid;
	memset(&token, 0, sizeof token);
	if((result = vcclaim(&p->ports, &token)) < 0)
		return result == VcFault ? pumpfault(p) : result;
	p->advertised[token.id] = 0;
	enqueue(p, token.id, VcOpen, 1, token.generation);
	*out = token;
	return 0;
}

int
vcpumpalive(const VcPump *p, const VcToken *token)
{
	return valid(p) && p->state == VcPumpRunning && p->mmio->state == VcMmRunning &&
		p->mmio->epoch == p->epoch && vcalive(&p->ports, token) &&
		p->advertised[token->id] == token->generation;
}

int
vcpumprelease(VcPump *p, const VcToken *token)
{
	int result;

	if((result = running(p)) < 0) return result;
	if(p->count == VcPumpReplies) return VcInvalid;
	if((result = vcrelease(&p->ports, token)) < 0)
		return result == VcFault ? pumpfault(p) : result;
	p->advertised[token->id] = 0;
	enqueue(p, token->id, VcOpen, 0, p->ports.port[token->id].generation);
	return 0;
}

int
vcpumpstop(VcPump *p)
{
	if(!valid(p)) return VcInvalid;
	p->state = VcPumpStopped;
	p->ports.failed = 1;
	return 0;
}

int
vcpumpfail(VcPump *p)
{
	if(!valid(p) || p->state == VcPumpStopped) return VcInvalid;
	return pumpfault(p);
}
