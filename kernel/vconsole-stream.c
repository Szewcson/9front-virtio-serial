/* SPDX-License-Identifier: MIT */
#ifndef QGA_HOST_TEST
#ifndef VCONSOLE_USER_TEST
#ifndef QGA_VCONSOLE_KERNEL
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#define QGA_VCONSOLE_KERNEL
#endif
#endif
#endif
#include "vconsole-stream.h"

int
vcsbudget(VcPumpBudget *out, const VcQueueInfo *info, int count, int ports,
	int capacity, Vcu64 limit)
{
	VcPumpBudget b;
	if(out == nil || limit > VcPumpBudgetMax || limit <= sizeof(VcStream) ||
	   vcpumpbudget(&b, info, count, ports, capacity, limit-sizeof(VcStream)) < 0)
		return VcInvalid;
	b.metadata += sizeof(VcStream);
	b.total += sizeof(VcStream);
	*out = b;
	return 0;
}

static int
streamvalid(const VcStream *s)
{
	return s != nil && s->identity != 0 && s->epoch != 0 && s->pump != nil &&
		s->state >= VcStreamPrepared && s->state <= VcStreamStopped;
}

static void
clearbytes(VcStream *s)
{
	memset(s->bytes, 0, sizeof s->bytes);
	s->head = s->count = 0;
}

static int
streamfault(VcStream *s)
{
	clearbytes(s);
	s->state = VcStreamFault;
	vcpumpfail(s->pump);
	return VcFault;
}

static int
syncstate(VcStream *s)
{
	VcPump *p;
	if(!streamvalid(s)) return VcInvalid;
	if(s->state == VcStreamFault) return VcFault;
	if(s->state == VcStreamStopped) return VcStreamClosed;
	p = s->pump;
	if(p->epoch != s->epoch || p->mmio->epoch != s->epoch || p->state == VcPumpFault)
		return streamfault(s);
	if(p->state == VcPumpStopped){
		clearbytes(s); s->state = VcStreamStopped; return VcStreamClosed;
	}
	if(s->claimed && (s->state == VcStreamOpening || s->state == VcStreamOpen)){
		if(!vcalive(&p->ports, &s->token.port)){
			clearbytes(s); s->state = VcStreamDisconnected;
		}else if(vcpumpalive(p, &s->token.port)) s->state = VcStreamOpen;
	}
	return 0;
}

int
vcsinit(VcStream *s, VcPump *p, Vcu64 identity, Vcu64 limit)
{
	if(s == nil || p == nil || identity == 0 || p->state != VcPumpReady || p->deferred ||
	   limit > VcPumpBudgetMax || limit <= sizeof *s || p->budget.total > limit-sizeof *s ||
	   vcpumpdeferdata(p) < 0)
		return VcInvalid;
	memset(s, 0, sizeof *s);
	s->pump = p; s->identity = identity; s->epoch = p->epoch;
	s->state = VcStreamPrepared;
	return 0;
}

static int
matches(const VcStream *s, const VcStreamToken *token)
{
	return token != nil && s->claimed && token->identity == s->identity &&
		token->epoch == s->epoch && token->port.id == s->token.port.id &&
		token->port.generation == s->token.port.generation;
}

static int
freshqueue(const VcDmaQueue *q)
{
	int i;
	if(q->state != VcDmaBound || q->avail != 0 || q->core.used != 0 || q->core.posted != 0)
		return 0;
	for(i = 0; i < q->layout.size; i++)
		if(q->life[i] != VcDmaFree || q->serial[i] != 0) return 0;
	return 1;
}

int
vcsopen(VcStream *s, VcStreamToken *out)
{
	VcPump *p;
	VcPort *port;
	VcToken token;
	VcDmaQueue *q;
	int i, rx, tx, result;
	if((result = syncstate(s)) < 0) return result;
	if(out == nil || s->pump->state != VcPumpRunning) return VcInvalid;
	if(s->claimed) return s->state == VcStreamDisconnected ? VcStreamReset : VcInvalid;
	p = s->pump;
	for(i = 0; i < p->ports.size; i++){
		port = &p->ports.port[i];
		if(!port->present || !port->named || port->console || port->guestopen ||
		   strcmp(port->name, "org.qemu.guest_agent.0") != 0) continue;
		vcportqueues(i, &rx, &tx); /* bounded known port ID */
		if(!freshqueue(p->queue[rx]) || !freshqueue(p->queue[tx]) || s->sink[i])
			return VcStreamReset;
		memset(&token, 0, sizeof token);
		if((result = vcpumpclaim(p, &token)) < 0) return result;
		if(token.id != (Vcu32)i) return streamfault(s);
		s->token.identity = s->identity; s->token.epoch = s->epoch; s->token.port = token;
		s->claimed = 1; s->rxqueue = rx; s->txqueue = tx;
		s->state = VcStreamOpening;
		q = p->queue[rx];
		for(i = 0; i < q->layout.size; i++)
			if(vcdmapost(q, i, nil, q->layout.capacity, token.generation) < 0)
				return streamfault(s);
		if(vcmmkick(p->mmio, &q->info) < 0) return streamfault(s);
		*out = s->token;
		return 0;
	}
	return VcInvalid;
}

int
vcsalive(VcStream *s, const VcStreamToken *token)
{
	return syncstate(s) == 0 && matches(s, token) && s->state == VcStreamOpen;
}

static int
streamrx(VcStream *s)
{
	VcDmaQueue *q;
	VcDmaDone *done;
	int length, i, deliver;
	if(s->rxhead == s->rxcount) return 0;
	done = &s->rx[s->rxhead];
	deliver = s->state == VcStreamOpen;
	if(deliver && done->used.length > (Vcu32)(VcStreamFifo-s->count)) return 0;
	q = s->pump->queue[s->rxqueue];
	if(done->used.epoch != s->epoch || done->used.cookie != s->token.port.generation)
		return streamfault(s);
	length = vcdmatake(q, done, s->frame, sizeof s->frame);
	if(length < 0) return streamfault(s);
	if(deliver){
		for(i = 0; i < length; i++) s->bytes[(s->head+s->count+i)%VcStreamFifo] = s->frame[i];
		s->count += length;
	}
	memset(s->frame, 0, sizeof s->frame);
	if(s->state == VcStreamOpening || s->state == VcStreamOpen){
		if(vcdmapost(q, done->used.id, nil, q->layout.capacity, s->token.port.generation) < 0 ||
		   vcmmkick(s->pump->mmio, &q->info) < 0) return streamfault(s);
	}
	s->rxhead++;
	return 1;
}

static int
streamtx(VcStream *s)
{
	VcDmaDone *done;
	if(s->txhead == s->txcount) return 0;
	done = &s->tx[s->txhead];
	if(done->used.epoch != s->epoch || done->used.cookie != s->token.port.generation ||
	   vcdmatake(s->pump->queue[s->txqueue], done, nil, 0) != 0)
		return streamfault(s);
	s->txhead++;
	return 1;
}

static int
dataaction(VcStream *s)
{
	int result;
	result = s->dataturn ? streamtx(s) : streamrx(s);
	if(result == 0) result = s->dataturn ? streamrx(s) : streamtx(s);
	if(result > 0) s->dataturn ^= 1;
	return result;
}

static int
pollsinks(VcStream *s)
{
	VcPump *p;
	int id, rx, tx;
	p = s->pump;
	/* Snapshot every relevant queue before the action budget. Otherwise an IRQ
	 * shared by data and console could be consumed by one action and the other
	 * completed queue omitted from Again until the health timeout. */
	for(id = 0; id < p->ports.size; id++){
		if(!(p->ports.port[id].present && p->ports.port[id].console) && !s->sink[id]) continue;
		if(s->claimed && s->token.port.id == (Vcu32)id) return streamfault(s);
		vcportqueues(id, &rx, &tx);
		s->sink[id] = 1;
		if(vcdmapoll(p->queue[rx], s->scratch, VcQueueMax) < 0) return streamfault(s);
	}
	return 0;
}

static int
sinkaction(VcStream *s)
{
	VcPump *p;
	VcDmaQueue *q;
	VcDmaDone done;
	int visit, id, rx, tx, i, slot, console;
	p = s->pump;
	for(visit = 0; visit < p->ports.size; visit++){
		id = s->nextport; s->nextport = (id+1)%p->ports.size;
		console = p->ports.port[id].present && p->ports.port[id].console;
		if(!console && !s->sink[id]) continue;
		if(s->claimed && s->token.port.id == (Vcu32)id) return streamfault(s);
		vcportqueues(id, &rx, &tx);
		q = p->queue[rx];
		s->sink[id] = 1;
		for(i = 0; i < q->layout.size; i++){
			slot = (s->nextslot[id]+i)%q->layout.size;
			if(q->life[slot] == VcDmaReady){
				done = q->ready[slot]; /* private CPU receipt, not overlapping queue state */
				if(done.used.cookie != 0 || vcdmatake(q, &done, s->frame, sizeof s->frame) < 0)
					return streamfault(s);
				memset(s->frame, 0, sizeof s->frame);
			}else if(console && q->life[slot] == VcDmaFree){
				if(vcdmapost(q, slot, nil, q->layout.capacity, 0) < 0 ||
				   vcmmkick(p->mmio, &q->info) < 0) return streamfault(s);
			}else continue;
			s->nextslot[id] = (slot+1)%q->layout.size;
			return 1;
		}
	}
	return 0;
}

static int
sinkready(const VcStream *s)
{
	VcDmaQueue *q;
	VcPump *p;
	int id, rx, tx, i, console;
	p = s->pump;
	for(id = 0; id < p->ports.size; id++){
		console = p->ports.port[id].present && p->ports.port[id].console;
		if(!console && !s->sink[id]) continue;
		vcportqueues(id, &rx, &tx); q = p->queue[rx];
		for(i = 0; i < q->layout.size; i++)
			if(q->life[i] == VcDmaReady || (console && q->life[i] == VcDmaFree)) return 1;
	}
	return 0;
}

int
vcsstep(VcStream *s, int budget)
{
	int result, control, actions, work;
	if(budget < 1 || budget > VcQueueMax) return VcInvalid;
	if((result = syncstate(s)) < 0) return result;
	control = vcpumpstep(s->pump, budget);
	if(control < 0) return control == VcFault ? streamfault(s) : control;
	if((result = syncstate(s)) < 0) return result;
	if(s->claimed){
		if(s->txhead == s->txcount){
			s->txhead = 0;
			s->txcount = vcdmapoll(s->pump->queue[s->txqueue], s->tx, VcQueueMax);
			if(s->txcount < 0) return streamfault(s);
		}
		if(s->rxhead == s->rxcount){
			s->rxhead = 0;
			s->rxcount = vcdmapoll(s->pump->queue[s->rxqueue], s->rx, VcQueueMax);
			if(s->rxcount < 0) return streamfault(s);
		}
	}
	if(pollsinks(s) < 0) return VcFault;
	for(actions = 0; actions < budget; actions++){
		work = s->turn ? sinkaction(s) : dataaction(s);
		if(work == 0) work = s->turn ? dataaction(s) : sinkaction(s);
		if(work < 0) return work;
		if(work == 0) break;
		s->turn ^= 1;
	}
	result = control | (actions > 0 ? VcPumpProgress : 0);
	if(s->txhead < s->txcount || sinkready(s) ||
	   (s->rxhead < s->rxcount && (s->state != VcStreamOpen ||
	    s->rx[s->rxhead].used.length <= (Vcu32)(VcStreamFifo-s->count))))
		result |= VcPumpAgain;
	return result;
}

static int
owner(VcStream *s, const VcStreamToken *token)
{
	int result;
	if((result = syncstate(s)) < 0) return result;
	if(!matches(s, token) || s->state == VcStreamDisconnected) return VcStreamClosed;
	return s->state == VcStreamOpen ? 0 : VcStreamWait;
}

int
vcsread(VcStream *s, const VcStreamToken *token, Vcu8 *out, int capacity)
{
	int result, length, i;
	if((result = owner(s, token)) < 0) return result;
	if(capacity < 0 || capacity > VcStreamFifo || (capacity > 0 && out == nil)) return VcInvalid;
	if(capacity == 0) return 0;
	if(s->count == 0) return VcStreamWait;
	length = capacity < s->count ? capacity : s->count;
	for(i = 0; i < length; i++){
		out[i] = s->bytes[s->head]; s->bytes[s->head] = 0;
		s->head = (s->head+1)%VcStreamFifo;
	}
	s->count -= length;
	return length;
}

int
vcswrite(VcStream *s, const VcStreamToken *token, const Vcu8 *input, int length)
{
	VcDmaQueue *q;
	int result, id, count;
	if((result = owner(s, token)) < 0) return result;
	if(length < 0 || length > VcBufferMax || (length > 0 && input == nil)) return VcInvalid;
	if(length == 0) return 0;
	/* Backend close is flow control, not a new guest owner or cancellation.
	 * Queued bytes/requests remain bounded; QGA delimited sync handles stale
	 * protocol replies on the next connection. No blind replay or retagging. */
	if(!s->pump->ports.port[s->token.port.id].hostopen) return VcStreamWait;
	q = s->pump->queue[s->txqueue];
	for(id = 0; id < q->layout.size; id++) if(q->life[id] == VcDmaFree) break;
	if(id == q->layout.size) return VcStreamWait;
	count = length < q->layout.capacity ? length : q->layout.capacity;
	if(vcdmapost(q, id, input, count, s->token.port.generation) < 0 ||
	   vcmmkick(s->pump->mmio, &q->info) < 0) return streamfault(s);
	return count;
}

int
vcsclose(VcStream *s, const VcStreamToken *token)
{
	int result;
	if((result = syncstate(s)) < 0) return result;
	if(!matches(s, token)) return VcStreamClosed;
	if(s->state == VcStreamDisconnected) return 0;
	if((result = vcpumprelease(s->pump, &s->token.port)) < 0)
		return result == VcFault ? streamfault(s) : result;
	clearbytes(s); s->state = VcStreamDisconnected;
	return 0;
}

int
vcsstop(VcStream *s)
{
	if(!streamvalid(s)) return VcInvalid;
	clearbytes(s); s->state = VcStreamStopped;
	vcpumpstop(s->pump);
	return 0;
}
