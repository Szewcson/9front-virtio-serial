/* SPDX-License-Identifier: MIT */
#include "../kernel/vconsole.h"
#ifdef QGA_HOST_TEST
#include <stdlib.h>
#include <stdio.h>
#endif

typedef struct Fixture Fixture;
struct Fixture {
	VcPci pci, savedpci;
	VcBar bar[VcBarMax];
	VcQueue q;
	VcPorts ports, savedports;
	VcUsed used[VcQueueMax];
	Vcu8 cfg[VcPciSize], entries[VcQueueMax*8], wire[VcControlMax+2];
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

	for(i = 0; i < 4; i++) p[i] = value>>(i*8);
}

static void
packet(Fixture *f, Vcu32 id, int event, int value)
{
	memset(f->wire, 0, sizeof f->wire);
	word(f->wire, id);
	f->wire[4] = event;
	f->wire[5] = event>>8;
	f->wire[6] = value;
	f->wire[7] = value>>8;
}

static void
pciinput(Fixture *f)
{
	int i, at;
	static const int offsets[] = {0, 64, 320, 324};
	static const int lengths[] = {56, 256, 1, 8};

	memset(f->cfg, 0, sizeof f->cfg);
	memset(f->bar, 0, sizeof f->bar);
	f->cfg[0] = 0xf4; f->cfg[1] = 0x1a;
	f->cfg[2] = 0x43; f->cfg[3] = 0x10;
	f->cfg[6] = 16; f->cfg[0x34] = 0x40;
	f->bar[4].address = (Vcu64)1<<32;
	f->bar[4].size = 4096;
	f->bar[4].memory = 1;
	for(i = 0; i < 4; i++){
		at = 0x40+i*32;
		f->cfg[at] = 9;
		f->cfg[at+1] = i == 3 ? 0 : at+32;
		f->cfg[at+2] = i == 1 ? 20 : 16;
		f->cfg[at+3] = i+1;
		f->cfg[at+4] = 4;
		word(f->cfg+at+8, offsets[i]);
		word(f->cfg+at+12, lengths[i]);
	}
	word(f->cfg+0x60+16, 4);
}

static void
pcifail(Fixture *f)
{
	f->pci = f->savedpci;
	check(vcpci(&f->pci, f->cfg, sizeof f->cfg, f->bar) == VcInvalid, "reject PCI capability layout");
	check(memcmp(&f->pci, &f->savedpci, sizeof f->pci) == 0, "no partial PCI result");
}

static void
pcitests(Fixture *f)
{
	Vcu64 address;
	int i, rx, tx;
	Vcu32 seed;

	pciinput(f);
	check(vcpci(&f->pci, f->cfg, sizeof f->cfg, f->bar) == 0, "modern console MMIO capability decode");
	f->savedpci = f->pci;
	check(f->pci.region[0].address == ((Vcu64)1<<32) && f->pci.region[3].offset == 324,
		"64-bit physical addresses and region identity");
	check(vcnotify(&f->pci, 63, &address) == 0 && address == ((Vcu64)1<<32)+316, "last valid notify offset");
	check(vcnotify(&f->pci, 64, &address) == VcInvalid, "notify multiplication range");
	f->pci.multiplier = ~(Vcu32)0;
	check(vcnotify(&f->pci, 65535, &address) == VcInvalid, "wide notify multiplication");
	f->pci.multiplier = 0;
	check(vcnotify(&f->pci, 65535, &address) == 0, "shared notify register");
	f->pci = f->savedpci;
	f->pci.region[1].address++;
	check(vcnotify(&f->pci, 0, &address) == VcInvalid, "unaligned notify metadata");
	f->pci = f->savedpci;
	for(i = 6; i < 256; i++){
		pciinput(f); f->cfg[0x44] = i; pcifail(f);
	}
	pciinput(f); f->cfg[0x41] = 0x40; pcifail(f);
	pciinput(f); f->cfg[0x41] = 0x60; f->cfg[0x61] = 0x40; pcifail(f);
	pciinput(f); f->cfg[0x34] = 0x3c; pcifail(f);
	pciinput(f); f->cfg[0x34] = 0x41; pcifail(f);
	pciinput(f); f->cfg[0x34] = 0xfc; f->cfg[0xfc] = 9; f->cfg[0xfe] = 16; pcifail(f);
	pciinput(f); f->cfg[0x42] = 255; pcifail(f);
	pciinput(f); f->cfg[0x42] = 15; pcifail(f);
	pciinput(f); f->cfg[0x62] = 16; pcifail(f);
	pciinput(f); f->cfg[0xa3] = 1; pcifail(f);
	pciinput(f); f->cfg[0xa3] = 5; pcifail(f);
	pciinput(f); word(f->cfg+0x48, ~(Vcu32)0); pcifail(f);
	pciinput(f); word(f->cfg+0x4c, 55); pcifail(f);
	pciinput(f); word(f->cfg+0x6c, 4096); pcifail(f);
	pciinput(f); word(f->cfg+0x68, 16); pcifail(f);
	pciinput(f); word(f->cfg+0x68, 65); pcifail(f);
	pciinput(f); f->bar[4].memory = 0; pcifail(f);
	pciinput(f); f->bar[4].size = 0; pcifail(f);
	pciinput(f); f->bar[4].address = ~(Vcu64)0-4095; pcifail(f);
	pciinput(f); f->bar[4].address++; pcifail(f);
	pciinput(f); f->cfg[6] = 0; pcifail(f);
	pciinput(f); f->cfg[14] = 1; pcifail(f);
	pciinput(f); f->cfg[2] = 0x42; pcifail(f);
	pciinput(f); f->cfg[2] = 3;
	check(vcpci(&f->pci, f->cfg, 256, f->bar) == 0, "transitional device with modern capabilities");
	pciinput(f); f->cfg[0x34] = 0xc0; f->cfg[0xc0] = 5; f->cfg[0xc1] = 0x40;
	check(vcpci(&f->pci, f->cfg, 256, f->bar) == 0, "skip unrelated PCI capabilities");
	check(vcpci(&f->pci, f->cfg, 255, f->bar) == VcInvalid &&
		vcpci(nil, f->cfg, 256, f->bar) == VcInvalid, "short/null PCI input");
	for(i = 0; i < VcPortMax; i++)
		check(vcportqueues(i, &rx, &tx) == 0 && rx == (i == 0 ? 0 : 2*i+2) && tx == rx+1,
			"port queue mapping excludes control queues");
	check(vcportqueues(VcPortMax, &rx, &tx) == VcInvalid &&
		vcportqueues(~(Vcu32)0, &rx, &tx) == VcInvalid && vcportqueues(1, &rx, &rx) == VcInvalid,
		"invalid port/alias queue mapping");
	seed = 0x18597;
	for(i = 0; i < 2000; i++){
		pciinput(f);
		seed = seed*1664525+1013904223;
		f->cfg[(seed>>8)%256] = seed;
		f->pci = f->savedpci;
		rx = vcpci(&f->pci, f->cfg, 256, f->bar);
		check(rx == 0 || (rx == VcInvalid && memcmp(&f->pci, &f->savedpci, sizeof f->pci) == 0),
			"mutated PCI snapshot remains bounded/atomic");
	}
}

static void
completion(Fixture *f, int at, Vcu32 id, Vcu32 length)
{
	word(f->entries+at*8, id);
	word(f->entries+at*8+4, length);
}

static void
postedpair(Fixture *f)
{
	check(vcqinit(&f->q, 8, 1) == 0, "fresh receive queue");
	check(vcqpost(&f->q, 3, 0x1000, 4, 11, f->wire, 16) == 0 &&
		vcqpost(&f->q, 7, 0x2000, 4096, 22, f->wire, 16) == 0, "post bounded RX leases");
	completion(f, 0, 3, 4);
	completion(f, 1, 7, 4096);
}

static void
queuetests(Fixture *f)
{
	static const Vcu8 descriptor[] = {0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0,0x10,0,0,2,0,0,0};
	int i, at, result;
	Vcu32 seed;

	check(vcqinit(&f->q, 3, 1) == VcInvalid && vcqinit(&f->q, 512, 1) == VcInvalid &&
		vcqinit(&f->q, 0, 1) == VcInvalid && vcqinit(&f->q, 8, 2) == VcInvalid, "queue size/direction limits");
	check(vcqinit(&f->q, 256, 1) == 0, "maximum queue");
	memset(f->wire, 0xa5, sizeof f->wire);
	check(vcqpost(&f->q, 255, ((Vcu64)0x12345678<<32)|0x12345678, 4096, 42, f->wire+1, 16) == 0 &&
		memcmp(f->wire+1, (void*)descriptor, 16) == 0 && f->wire[0] == 0xa5 && f->wire[17] == 0xa5,
		"single RX descriptor wire layout and guards");
	check(vcqpost(&f->q, 255, 0x1000, 1, 0, f->wire, 16) == VcInvalid && f->q.posted == 1,
		"no descriptor reuse while device owns it");
	check(vcqpost(&f->q, -1, 0x1000, 1, 0, f->wire, 16) == VcInvalid &&
		vcqpost(&f->q, 256, 0x1000, 1, 0, f->wire, 16) == VcInvalid &&
		vcqpost(&f->q, 0, 0x1000, 0, 0, f->wire, 16) == VcInvalid &&
		vcqpost(&f->q, 0, 0x1000, 4097, 0, f->wire, 16) == VcInvalid &&
		vcqpost(&f->q, 0, ~(Vcu64)0, 1, 0, f->wire, 16) == VcInvalid &&
		vcqpost(&f->q, 0, 0x1000, 1, 0, f->wire, 15) == VcInvalid, "descriptor argument bounds");
	postedpair(f);
	check(vcqconsume(&f->q, 2, f->entries, 64, f->used, 1) == VcInvalid &&
		!f->q.failed && f->q.posted == 2 && f->q.used == 0, "capacity failure preserves leases");
	check(vcqconsume(&f->q, 2, f->entries, 63, f->used, 2) == VcInvalid && f->q.posted == 2,
		"short ring snapshot");
	check(vcqconsume(&f->q, 2, f->entries, 64, f->used, 2) == 2 &&
		f->used[0].id == 3 && f->used[0].cookie == 11 && f->used[1].id == 7 &&
		f->used[1].length == 4096 && f->used[1].cookie == 22 && f->used[1].epoch == 1 &&
		f->q.posted == 0 && f->q.used == 2, "whole valid completion batch");
	check(vcqconsume(&f->q, 2, nil, 0, nil, 0) == 0, "empty batch needs no buffers");
	postedpair(f); completion(f, 1, ~(Vcu32)0, 0);
	memset(f->used, 0xa5, sizeof f->used);
	check(vcqconsume(&f->q, 2, f->entries, 64, f->used, 2) == VcFault &&
		f->q.failed && f->q.posted == 2 && f->q.slot[3].posted && f->q.slot[7].posted &&
		f->q.used == 0 && ((Vcu8*)f->used)[0] == 0xa5, "bad second ID releases nothing");
	check(vcqpost(&f->q, 0, 0x1000, 1, 0, f->wire, 16) == VcFault, "faulted queue stops publication");
	check(vcqreset(&f->q, 0) == VcInvalid && f->q.posted == 2, "no reset before actual DMA quiescence");
	check(vcqreset(&f->q, 1) == 0 && f->q.posted == 0 && !f->q.failed && f->q.epoch == 2,
		"quiesced reset advances epoch");
	check(vcqconsume(&f->q, 1, f->entries, 64, f->used, 2) == VcFault, "unsolicited completion after reset");
	postedpair(f); completion(f, 1, 3, 0);
	check(vcqconsume(&f->q, 2, f->entries, 64, f->used, 2) == VcFault && f->q.posted == 2,
		"duplicate completion rejected as a whole batch");
	postedpair(f); completion(f, 1, 7, 4097);
	check(vcqconsume(&f->q, 2, f->entries, 64, f->used, 2) == VcFault && f->q.posted == 2,
		"device cannot overstate RX buffer length");
	postedpair(f);
	check(vcqconsume(&f->q, 0xffff, f->entries, 64, f->used, 2) == VcFault, "used-index jump bounded");
	check(vcqinit(&f->q, 8, 0) == 0 && vcqpost(&f->q, 0, 0x1000, 1, 4, f->wire, 16) == 0 &&
		f->wire[12] == 0, "TX descriptors are device-readable only");
	completion(f, 0, 0, 1);
	check(vcqconsume(&f->q, 1, f->entries, 64, f->used, 1) == VcFault, "modern TX has no device-written bytes");
	check(vcqreset(&f->q, 1) == 0 && vcqpost(&f->q, 0, 0x1000, 1, 4, f->wire, 16) == 0, "reset TX lease");
	completion(f, 0, 0, 0);
	check(vcqconsume(&f->q, 1, f->entries, 64, f->used, 1) == 1, "zero-length modern TX completion");
	f->q.epoch = ~(Vcu64)0;
	check(vcqreset(&f->q, 1) == VcInvalid, "reset epoch cannot wrap");
	check(vcqinit(&f->q, 8, 1) == 0, "wrap test queue");
	for(i = 0; i < 65538; i++){
		at = f->q.used&7;
		check(vcqpost(&f->q, 3, 0x1000, 1, i, f->wire, 16) == 0, "wrap post");
		completion(f, at, 3, 1);
		check(vcqconsume(&f->q, (Vcu16)(f->q.used+1), f->entries, 64, f->used, 1) == 1 &&
			f->used[0].cookie == (Vcu64)i && f->q.posted == 0, "16-bit index wrap with real leases");
	}
	check(f->q.used == 2, "complete index wrap");
	f->q.size = VcQueueMax+1;
	check(vcqpost(&f->q, 256, 0x1000, 1, 0, f->wire, 16) == VcInvalid &&
		vcqconsume(&f->q, 2, f->entries, 64, f->used, 1) == VcInvalid &&
		vcqreset(&f->q, 1) == VcInvalid, "invalid owner queue metadata cannot index arrays");
	seed = 7159;
	for(i = 0; i < 2000; i++){
		check(vcqinit(&f->q, 8, 1) == 0, "fuzz queue init");
		for(at = 0; at < 8; at++)
			check(vcqpost(&f->q, at, 0x1000+at*4096, 4096, at, f->wire, 16) == 0, "fuzz queue post");
		for(at = 0; at < 8; at++){
			seed = seed*1664525+1013904223;
			completion(f, at, seed%16, (seed>>8)%5000);
		}
		result = vcqconsume(&f->q, seed%17, f->entries, 64, f->used, 8);
		check((result >= 0 && f->q.posted == 8-result) ||
			(result == VcFault && f->q.posted == 8 && f->q.used == 0), "hostile batches cannot partly release");
	}
}

static int
event(Fixture *f, Vcu32 id, int kind, int value)
{
	packet(f, id, kind, value);
	return vcreceive(&f->ports, f->wire, 8);
}

static int
portname(Fixture *f, Vcu32 id, const char *name, int nul)
{
	int n;

	packet(f, id, VcName, 1);
	n = strlen((char*)name);
	check(n < VcNameMax, "fixture name bound");
	memmove(f->wire+8, (void*)name, n);
	return vcreceive(&f->ports, f->wire, 8+n+nul);
}

static void
porttests(Fixture *f)
{
	VcControl c, saved;
	VcToken old, token, other;
	Vcu64 generation;
	int i, n;
	Vcu32 seed;
	static const char *badutf[] = {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", "\x80", "a\0b"};
	static const int badsize[] = {2,3,4,2,1,3};

	check(vcportsinit(&f->ports, 0) == VcInvalid && vcportsinit(&f->ports, 65) == VcInvalid,
		"controller port bounds");
	check(vcportsinit(&f->ports, 31) == 0, "QEMU default port limit supported");
	check(event(f, 1, VcAdd, 1) == 0 && event(f, 1, VcOpen, 1) == 0,
		"port add and host connection");
	check(vcclaim(&f->ports, &token) == VcInvalid, "never guess port zero or an unnamed port");
	check(portname(f, 1, "org.qemu.guest_agent.0", 1) == 0 && vcclaim(&f->ports, &old) == 0 &&
		old.id == 1 && vcalive(&f->ports, &old), "exact QGA tag with QEMU terminal NUL");
	check(vcclaim(&f->ports, &other) == VcInvalid, "single reader ownership");
	check(vcrelease(&f->ports, &old) == 0 && !vcalive(&f->ports, &old) &&
		vcclaim(&f->ports, &token) == 0 && token.generation != old.generation, "close/reopen advances generation");
	check(vcrelease(&f->ports, &old) == VcInvalid && vcalive(&f->ports, &token), "stale close cannot close new reader");
	check(vcqinit(&f->q, 8, 1) == 0 &&
		vcqpost(&f->q, 0, 0x1000, 1, token.generation, f->wire, 16) == 0, "RX lease carries port generation cookie");
	other = token;
	check(event(f, 1, VcOpen, 0) == 0 && vcalive(&f->ports, &token) &&
		event(f, 1, VcOpen, 1) == 0 && vcclaim(&f->ports, &other) == VcInvalid &&
		f->ports.port[1].generation == token.generation, "host reconnect preserves exclusive guest owner");
	completion(f, 0, 0, 1);
	check(f->q.posted == 1 && vcqconsume(&f->q, 1, f->entries, 64, f->used, 1) == 1 &&
		f->used[0].cookie == token.generation && f->used[0].cookie == other.generation,
		"backend reconnect keeps same owner DMA lease without retagging");
	check(event(f, 1, VcRemove, 0) == 0 && !vcalive(&f->ports, &other) &&
		event(f, 1, VcAdd, 0) == 0 && portname(f, 1, "org.qemu.guest_agent.0", 0) == 0 &&
		event(f, 1, VcOpen, 1) == 0 && vcclaim(&f->ports, &token) == 0,
		"port remove/readd and spec name without NUL");
	check(vcrelease(&f->ports, &other) == VcInvalid && vcalive(&f->ports, &token), "stale removed-port owner is harmless");
	generation = f->ports.port[1].generation;
	check(portname(f, 1, "org.qemu.guest_agent.0", 0) == 0 && event(f, 1, VcOpen, 1) == 0 &&
		f->ports.port[1].generation == generation, "identical metadata notifications are idempotent");
	check(event(f, 2, VcAdd, 0) == 0 && portname(f, 2, "org.qemu.guest_agent.0", 0) == VcFault &&
		!vcalive(&f->ports, &token), "duplicate QGA names disable ambiguous controller");
	check(vcportsreset(&f->ports, 0) == VcInvalid && f->ports.failed,
		"controller recovery needs actual quiescence");
	check(vcportsreset(&f->ports, 1) == 0 && !f->ports.failed && !f->ports.port[1].present &&
		!vcalive(&f->ports, &token), "controller reset revokes all tokens");
	check(event(f, 0, VcAdd, 0) == 0 && event(f, 0, VcConsole, 1) == 0 &&
		portname(f, 0, "console", 1) == 0 && event(f, 0, VcOpen, 1) == 0 &&
		vcclaim(&f->ports, &token) == VcInvalid, "text console cannot become QGA");
	check(event(f, 1, VcAdd, 0) == 0 && portname(f, 1, "org.qemu.guest_agent.0.evil", 0) == 0 &&
		event(f, 1, VcOpen, 1) == 0 && vcclaim(&f->ports, &token) == VcInvalid, "no prefix-based QGA selection");
	check(portname(f, 1, "org.qemu.guest_agent.0", 0) == VcFault &&
		strcmp(f->ports.port[1].name, "org.qemu.guest_agent.0.evil") == 0, "conflicting rename does not partly change tag");
	check(vcportsreset(&f->ports, 1) == 0 && event(f, 1, VcAdd, 0) == 0 &&
		portname(f, 1, "org.qemu.guest_agent.0", 0) == 0 && event(f, 1, VcConsole, 1) == VcFault,
		"QGA cannot be nominated as a text console");
	check(vcportsreset(&f->ports, 1) == 0 && event(f, 1, VcRemove, 0) == VcFault,
		"remove of absent port rejected");
	check(vcportsreset(&f->ports, 1) == 0 && event(f, 1, VcAdd, 0) == 0 &&
		event(f, 1, VcAdd, 0) == VcFault, "duplicate add rejected");
	check(vcportsreset(&f->ports, 1) == 0 && event(f, ~(Vcu32)0, VcAdd, 0) == VcFault,
		"huge device port ID rejected before indexing");
	check(vcportsreset(&f->ports, 1) == 0 && event(f, 31, VcAdd, 0) == VcFault,
		"advertised max port is exclusive");
	check(vcportsreset(&f->ports, 1) == 0 && event(f, 1, VcAdd, 0) == 0 &&
		portname(f, 1, "org.qemu.guest_agent.0", 0) == 0 && event(f, 1, VcOpen, 1) == 0,
		"generation exhaustion setup");
	f->ports.port[1].generation = ~(Vcu64)0;
	check(vcclaim(&f->ports, &token) == VcFault && vcportsreset(&f->ports, 1) == VcInvalid,
		"channel generation never wraps");
	packet(f, 1, VcName, 1);
	memmove(f->wire+8, "caf\xc3\xa9", 5);
	check(vcdecode(&c, f->wire, 13) == 0 && strcmp(c.name, "caf\xc3\xa9") == 0, "UTF-8 port name");
	saved = c;
	for(i = 0; i < (int)(sizeof badutf/sizeof badutf[0]); i++){
		packet(f, 1, VcName, 0); memmove(f->wire+8, (void*)badutf[i], badsize[i]);
		check(vcdecode(&c, f->wire, 8+badsize[i]) == VcInvalid && memcmp(&c, &saved, sizeof c) == 0,
			"invalid UTF-8/NUL leaves parse output unchanged");
	}
	packet(f, 1, VcName, 0); f->wire[8] = 'a';
	check(vcdecode(&c, f->wire, 11) == VcInvalid, "two terminal NUL bytes are not a tag");
	check(vcdecode(&c, f->wire, 8) == VcInvalid, "empty name");
	memset(f->wire+8, 'a', VcNameMax);
	check(vcdecode(&c, f->wire, VcControlMax) == VcInvalid, "name bound includes terminator");
	f->wire[VcControlMax-1] = 0;
	check(vcdecode(&c, f->wire, VcControlMax) == 0, "maximum QEMU-style name");
	for(i = 0; i < 8; i++) check(vcdecode(&c, f->wire, i) == VcInvalid, "truncated control header");
	packet(f, 1, VcOpen, 2); check(vcdecode(&c, f->wire, 8) == VcInvalid, "host-open boolean range");
	packet(f, 1, VcDeviceReady, 1); check(vcdecode(&c, f->wire, 8) == VcInvalid, "wrong-direction event");
	packet(f, 1, 65535, 0); check(vcdecode(&c, f->wire, 8) == VcInvalid, "unknown event");
	packet(f, 0, VcResize, 0); f->wire[8] = 80; f->wire[10] = 24;
	check(vcdecode(&c, f->wire, 12) == 0 && c.cols == 80 && c.rows == 24 &&
		vcdecode(&c, f->wire, 11) == VcInvalid, "bounded resize layout");
	for(i = 0; i < 3; i++){
		n = i == 0 ? VcDeviceReady : (i == 1 ? VcPortReady : VcOpen);
		check(vcencode(f->wire, 8, i == 0 ? 0 : 1, n, 1) == 8 &&
			f->wire[4] == n && f->wire[5] == 0 && f->wire[6] == 1, "outgoing control codec");
	}
	check(vcencode(f->wire, 7, 0, VcDeviceReady, 1) == VcInvalid &&
		vcencode(f->wire, 8, 1, VcDeviceReady, 1) == VcInvalid &&
		vcencode(f->wire, 8, 64, VcPortReady, 1) == VcInvalid &&
		vcencode(f->wire, 8, 0, VcName, 1) == VcInvalid &&
		vcencode(f->wire, 8, 0, VcOpen, 2) == VcInvalid, "outgoing control validation");
	f->ports.size = VcPortMax+1;
	check(vcclaim(&f->ports, &token) == VcInvalid && !vcalive(&f->ports, &old) &&
		vcreceive(&f->ports, f->wire, 8) == VcInvalid && vcportsreset(&f->ports, 1) == VcInvalid,
		"invalid owner port metadata cannot index arrays");
	seed = 67394;
	for(i = 0; i < 2000; i++){
		check(vcportsinit(&f->ports, 31) == 0 && event(f, 1, VcAdd, 0) == 0 &&
			portname(f, 1, "org.qemu.guest_agent.0", 1) == 0 && event(f, 1, VcOpen, 1) == 0 &&
			vcclaim(&f->ports, &token) == 0, "fuzz lifecycle setup");
		f->savedports = f->ports;
		seed = seed*1664525+1013904223;
		packet(f, (seed>>8)%64, seed%9, (seed>>16)%4);
		n = 8;
		if(f->wire[4] == VcResize) n = 12;
		if(f->wire[4] == VcName){
			memmove(f->wire+8, "org.qemu.guest_agent.0", 21);
			n = 29+((seed>>24)%3);
		}
		n = vcreceive(&f->ports, f->wire, n);
		check(n == 0 || (n == VcFault && f->ports.failed &&
			memcmp(f->ports.port, f->savedports.port, sizeof f->ports.port) == 0 &&
			!vcalive(&f->ports, &token)), "hostile control events cannot partly mutate ports");
	}
}

#ifdef QGA_HOST_TEST
int main(void)
#else
void main(void)
#endif
{
	Fixture *f;

	check(sizeof(Vcu8) == 1 && sizeof(Vcu16) == 2 && sizeof(Vcu32) == 4 && sizeof(Vcu64) == 8,
		"fixed-width wire types");
	f = malloc(sizeof *f);
	check(f != nil, "bounded fixture allocation");
	pcitests(f);
	queuetests(f);
	porttests(f);
	free(f);
#ifdef QGA_HOST_TEST
	printf("virtio-serial foundations: %d checks passed (no hardware/DMA)\n", checks);
	return 0;
#else
	print("virtio-serial foundations: %d checks passed (native, no hardware/DMA)\n", checks);
	exits(nil);
#endif
}
