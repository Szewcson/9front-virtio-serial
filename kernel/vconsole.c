/* SPDX-License-Identifier: MIT */
/* Native-compatible virtio-serial foundations, not a hardware driver yet. */
#include "vconsole.h"

static Vcu16
get16(const Vcu8 *p)
{
	return p[0] | (Vcu16)p[1]<<8;
}

static Vcu32
get32(const Vcu8 *p)
{
	return p[0] | (Vcu32)p[1]<<8 | (Vcu32)p[2]<<16 | (Vcu32)p[3]<<24;
}

static void
put16(Vcu8 *p, Vcu16 n)
{
	p[0] = n;
	p[1] = n>>8;
}

static void
put32(Vcu8 *p, Vcu32 n)
{
	put16(p, n);
	put16(p+2, n>>16);
}

static void
put64(Vcu8 *p, Vcu64 n)
{
	put32(p, n);
	put32(p+4, n>>32);
}

int
vcpci(VcPci *out, const Vcu8 *cfg, int size, const VcBar *bars)
{
	VcPci result;
	VcRegion *r;
	Vcu64 address;
	Vcu32 offset, length;
	Vcu8 seen[VcPciSize];
	int at, type, bar, i, j, count, found[VcRegionMax];
	static const int need[VcRegionMax] = {56, 2, 1, 8};

	if(out == nil || cfg == nil || bars == nil || size < VcPciSize ||
	   get16(cfg) != 0x1af4 || (get16(cfg+2) != 0x1043 && get16(cfg+2) != 0x1003) ||
	   (cfg[14]&127) != 0 || !(cfg[6]&16))
		return VcInvalid;
	memset(&result, 0, sizeof result);
	memset(seen, 0, sizeof seen);
	memset(found, 0, sizeof found);
	count = 0;
	for(at = cfg[0x34]; at != 0; at = cfg[at+1]){
		if(at < 0x40 || at > VcPciSize-4 || (at&3) != 0 || seen[at])
			return VcInvalid;
		seen[at] = 1;
		if(cfg[at] != 9)
			continue;
		if(cfg[at+2] < 16 || cfg[at+2] > VcPciSize-at)
			return VcInvalid;
		type = cfg[at+3];
		if(type < 1 || type > VcRegionMax)
			continue;
		i = type-1;
		if(found[i] || (type == 2 && cfg[at+2] < 20))
			return VcInvalid;
		bar = cfg[at+4];
		offset = get32(cfg+at+8);
		length = get32(cfg+at+12);
		/* Reject invalid BAR indices; never normalize one with modulo. */
		if(bar >= VcBarMax || bars[bar].memory != 1 || bars[bar].size == 0 ||
		   bars[bar].address > ~(Vcu64)0-bars[bar].size || length < (Vcu32)need[i] ||
		   offset > bars[bar].size || length > bars[bar].size-offset)
			return VcInvalid;
		address = bars[bar].address+offset;
		if((type == 1 || type == 4) && (address&3) != 0)
			return VcInvalid;
		if(type == 2 && (address&1) != 0)
			return VcInvalid;
		for(j = 0; j < VcRegionMax; j++)
			if(found[j] && address < result.region[j].address+result.region[j].length &&
			   result.region[j].address < address+length)
				return VcInvalid;
		r = &result.region[i];
		r->bar = bar;
		r->offset = offset;
		r->length = length;
		r->address = address;
		if(type == 2) result.multiplier = get32(cfg+at+16);
		found[i] = 1;
		count++;
	}
	if(count != VcRegionMax)
		return VcInvalid;
	*out = result;
	return 0;
}

int
vcnotify(const VcPci *pci, Vcu16 queueoff, Vcu64 *address)
{
	Vcu64 offset;
	const VcRegion *r;

	if(pci == nil || address == nil)
		return VcInvalid;
	r = &pci->region[1];
	offset = (Vcu64)queueoff*pci->multiplier;
	if(r->length < 2 || offset > r->length-2 || ((r->address|offset)&1) != 0 ||
	   r->address > ~(Vcu64)0-offset-2)
		return VcInvalid;
	*address = r->address+offset;
	return 0;
}

int
vcportqueues(Vcu32 id, int *receive, int *transmit)
{
	if(id >= VcPortMax || receive == nil || transmit == nil || receive == transmit)
		return VcInvalid;
	*receive = id == 0 ? 0 : 2*id+2;
	*transmit = *receive+1;
	return 0;
}

int
vcqinit(VcQueue *q, int size, int receive)
{
	if(q == nil || size < 1 || size > VcQueueMax || (size&(size-1)) != 0 ||
	   (receive != 0 && receive != 1))
		return VcInvalid;
	memset(q, 0, sizeof *q);
	q->size = size;
	q->receive = receive;
	q->epoch = 1;
	return 0;
}

static int
queuevalid(const VcQueue *q)
{
	return q != nil && q->size >= 1 && q->size <= VcQueueMax &&
		(q->size&(q->size-1)) == 0 && (q->receive == 0 || q->receive == 1) &&
		q->posted >= 0 && q->posted <= q->size && q->epoch != 0;
}

int
vcqpost(VcQueue *q, int id, Vcu64 address, int length, Vcu64 cookie, Vcu8 *wire, int size)
{
	VcSlot *slot;

	if(!queuevalid(q) || wire == nil || size < 16 || id < 0 || id >= q->size ||
	   length < 1 || length > VcBufferMax || address > ~(Vcu64)0-(Vcu32)length)
		return VcInvalid;
	if(q->failed) return VcFault;
	slot = &q->slot[id];
	if(slot->posted) return VcInvalid;
	put64(wire, address);
	put32(wire+8, length);
	put16(wire+12, q->receive ? 2 : 0); /* WRITE only for device-writable RX. */
	put16(wire+14, 0);               /* No chains, indirect or packed descriptors. */
	slot->length = length;
	slot->cookie = cookie;
	slot->posted = 1;
	q->posted++;
	return 0;
}

int
vcqconsume(VcQueue *q, Vcu16 used, const Vcu8 *entries, int bytes, VcUsed *out, int capacity)
{
	Vcu8 seen[VcQueueMax];
	Vcu32 id, length;
	const Vcu8 *entry;
	int n, i, at;

	if(!queuevalid(q) || capacity < 0) return VcInvalid;
	if(q->failed) return VcFault;
	n = (Vcu16)(used-q->used);
	if(n > q->size || n > q->posted) goto fault;
	if(n == 0) return 0;
	if(entries == nil || bytes < q->size*8 || out == nil || capacity < n)
		return VcInvalid;
	memset(seen, 0, sizeof seen);
	/* Snapshot is immutable throughout both passes. No slot is released if any
	 * completion is invalid, and duplicate IDs cannot slip through a repost. */
	for(i = 0; i < n; i++){
		at = (q->used+i)&(q->size-1);
		entry = entries+at*8;
		id = get32(entry);
		length = get32(entry+4);
		if(id >= (Vcu32)q->size || seen[id] || !q->slot[id].posted ||
		   (q->receive ? length > q->slot[id].length : length != 0))
			goto fault;
		seen[id] = 1;
	}
	for(i = 0; i < n; i++){
		at = (q->used+i)&(q->size-1);
		entry = entries+at*8;
		id = get32(entry);
		out[i].id = id;
		out[i].length = get32(entry+4);
		out[i].cookie = q->slot[id].cookie;
		out[i].epoch = q->epoch;
		q->slot[id].posted = 0;
	}
	q->posted -= n;
	q->used = used;
	return n;
fault:
	q->failed = 1;
	return VcFault;
}

int
vcqreset(VcQueue *q, int quiesced)
{
	if(!queuevalid(q) || quiesced != 1 || q->epoch == ~(Vcu64)0)
		return VcInvalid;
	memset(q->slot, 0, sizeof q->slot);
	q->posted = q->failed = 0;
	q->used = 0;
	q->epoch++;
	return 0;
}

/* Strict length-delimited UTF-8; embedded NUL, overlong forms, surrogates and
 * values beyond Unicode are rejected. Names are tags, never filesystem paths. */
static int
nameutf8(const Vcu8 *s, int n)
{
	Vcu32 r, min;
	int i, left;

	for(i = 0; i < n;){
		r = s[i++];
		if(r == 0) return 0;
		if(r < 0x80) continue;
		if(r >= 0xc2 && r <= 0xdf){ left = 1; min = 0x80; r &= 31; }
		else if(r >= 0xe0 && r <= 0xef){ left = 2; min = 0x800; r &= 15; }
		else if(r >= 0xf0 && r <= 0xf4){ left = 3; min = 0x10000; r &= 7; }
		else return 0;
		if(left > n-i) return 0;
		while(left-- > 0){
			if((s[i]&0xc0) != 0x80) return 0;
			r = (r<<6)|(s[i++]&63);
		}
		if(r < min || r > 0x10ffff || (r >= 0xd800 && r <= 0xdfff)) return 0;
	}
	return 1;
}

int
vcdecode(VcControl *out, const Vcu8 *wire, int size)
{
	VcControl c;
	int n;

	if(out == nil || wire == nil || size < 8 || size > VcControlMax)
		return VcInvalid;
	memset(&c, 0, sizeof c);
	c.id = get32(wire);
	c.event = get16(wire+4);
	c.value = get16(wire+6);
	switch(c.event){
	case VcAdd: case VcRemove: case VcConsole: case VcOpen:
		if(size != 8 || (c.event == VcOpen && c.value > 1)) return VcInvalid;
		break;
	case VcResize:
		if(size != 12) return VcInvalid;
		c.cols = get16(wire+8);
		c.rows = get16(wire+10);
		break;
	case VcName:
		n = size-8;
		/* Spec omits NUL, but QEMU sends exactly one final NUL. Accept both
		 * representations without accepting any embedded/extra NUL suffix. */
		if(n > 0 && wire[size-1] == 0) n--;
		if(n < 1 || n >= VcNameMax || !nameutf8(wire+8, n)) return VcInvalid;
		memmove(c.name, (void*)(wire+8), n);
		c.name[n] = 0;
		break;
	default: return VcInvalid; /* Unknown or wrong-direction event. */
	}
	*out = c;
	return 0;
}

int
vcencode(Vcu8 *wire, int size, Vcu32 id, int event, int value)
{
	if(wire == nil || size < 8 || value < 0 || value > 1 ||
	   (event != VcDeviceReady && event != VcPortReady && event != VcOpen) ||
	   (event == VcDeviceReady ? id != 0 : id >= VcPortMax))
		return VcInvalid;
	put32(wire, id);
	put16(wire+4, event);
	put16(wire+6, value);
	return 8;
}

int
vcportsinit(VcPorts *p, int size)
{
	if(p == nil || size < 1 || size > VcPortMax) return VcInvalid;
	memset(p, 0, sizeof *p);
	p->size = size;
	return 0;
}

static int
portsvalid(const VcPorts *p)
{
	return p != nil && p->size >= 1 && p->size <= VcPortMax;
}

int
vcportsreset(VcPorts *p, int quiesced)
{
	Vcu64 generation;
	int i;

	if(!portsvalid(p) || quiesced != 1) return VcInvalid;
	for(i = 0; i < p->size; i++)
		if(p->port[i].generation == ~(Vcu64)0) return VcInvalid;
	for(i = 0; i < p->size; i++){
		generation = p->port[i].generation+1;
		memset(&p->port[i], 0, sizeof p->port[i]);
		p->port[i].generation = generation;
	}
	p->failed = 0;
	return 0;
}

static int
isqga(const VcPort *port)
{
	return port->present && port->named && !port->console &&
		strcmp((char*)port->name, "org.qemu.guest_agent.0") == 0;
}

int
vcreceive(VcPorts *p, const Vcu8 *wire, int size)
{
	VcControl c;
	VcPort *port;
	Vcu64 generation;
	int i;

	if(!portsvalid(p)) return VcInvalid;
	if(p->failed) return VcFault;
	if(vcdecode(&c, wire, size) < 0 || c.id >= (Vcu32)p->size) goto fault;
	port = &p->port[c.id];
	if(c.event == VcAdd){
		if(port->present || port->generation == ~(Vcu64)0) goto fault;
		generation = port->generation+1;
		memset(port, 0, sizeof *port);
		port->generation = generation;
		port->present = 1;
		return 0;
	}
	if(!port->present) goto fault;
	switch(c.event){
	case VcRemove:
		if(port->generation == ~(Vcu64)0) goto fault;
		generation = port->generation+1;
		memset(port, 0, sizeof *port);
		port->generation = generation;
		break;
	case VcName:
		if(port->named && strcmp(port->name, c.name) != 0) goto fault;
		if(port->console && strcmp(c.name, "org.qemu.guest_agent.0") == 0) goto fault;
		for(i = 0; i < p->size; i++)
			if(i != (int)c.id && p->port[i].present && p->port[i].named &&
			   strcmp(p->port[i].name, c.name) == 0) goto fault;
		memmove(port->name, c.name, sizeof port->name);
		port->named = 1;
		break;
	case VcConsole:
		if(isqga(port)) goto fault;
		port->console = 1;
		break;
	case VcOpen:
		/* Host backend connection is NOT guest FD/port ownership. PVE closes
		 * its socket after each RPC. Keep the same owner's posted DMA leases
		 * and guest OPEN across these notifications; never retag buffers. */
		port->hostopen = c.value;
		break;
	case VcResize:
		break; /* Text-console dimensions do not change raw QGA data. */
	default: goto fault;
	}
	return 0;
fault:
	p->failed = 1;
	return VcFault;
}

int
vcalive(const VcPorts *p, const VcToken *token)
{
	const VcPort *port;

	if(!portsvalid(p) || token == nil || p->failed || token->id >= (Vcu32)p->size) return 0;
	port = &p->port[token->id];
	return isqga(port) && port->guestopen &&
		port->generation == token->generation;
}

int
vcclaim(VcPorts *p, VcToken *token)
{
	VcPort *port;
	int i;

	if(!portsvalid(p) || token == nil) return VcInvalid;
	if(p->failed) return VcFault;
	for(i = 0; i < p->size; i++){
		port = &p->port[i];
		if(!isqga(port) || port->guestopen) continue;
		if(port->generation == ~(Vcu64)0){
			p->failed = 1;
			return VcFault;
		}
		port->guestopen = 1;
		port->generation++;
		token->id = i;
		token->generation = port->generation;
		return 0;
	}
	return VcInvalid;
}

int
vcrelease(VcPorts *p, const VcToken *token)
{
	VcPort *port;

	if(!vcalive(p, token)) return VcInvalid;
	port = &p->port[token->id];
	if(port->generation == ~(Vcu64)0){
		p->failed = 1;
		return VcFault;
	}
	port->guestopen = 0;
	port->generation++;
	return 0;
}
