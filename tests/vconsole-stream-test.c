/* SPDX-License-Identifier: MIT */
#include "../kernel/vconsole-stream.h"
#define VC_FIXTURE_BUDGET vcsbudget
#include "vconsole-fixture.h"

static void
runbudget(VcStream *s, int budget)
{
	int i, result;
	for(i = 0; i < 4096; i++){
		result = vcsstep(s, budget);
		check(result >= 0, "bounded serialized stream iteration");
		if(!(result&VcPumpAgain)) return;
	}
	check(0, "no busy-loop on data/control backpressure");
}

static void
run(VcStream *s)
{
	runbudget(s, 256);
}

static VcStream*
stream(Fixture *f)
{
	VcStream *s;
	s = malloc(sizeof *s); check(s != nil, "checked heap stream state");
	check(vcsinit(s, &f->p, 77, VcPumpBudgetMax) == 0 && f->p.deferred,
		"stream budget checked and data publication deferred before startup");
	check(vcsstep(s, 1) == VcInvalid && vcpumpstart(&f->p) == 0,
		"no stream work before control startup");
	tx(f, 0, VcDeviceReady, 1); run(s);
	return s;
}

static VcStreamToken
connect(Fixture *f, VcStream *s, int id)
{
	VcStreamToken token;
	Vcu8 bytes[1];
	event(f, id, VcAdd, 0, nil); drain(f); tx(f, id, VcPortReady, 1); drain(f);
	event(f, id, VcName, 0, "org.qemu.guest_agent.0"); drain(f);
	event(f, id, VcOpen, 1, nil); drain(f);
	check(vcsopen(s, &token) == 0 && !vcsalive(s, &token) &&
		vcsread(s, &token, bytes, 1) == VcStreamWait && vcswrite(s, &token, bytes, 1) == VcStreamWait,
		"owner-tagged RX before OPEN, pending owner cannot read/write");
	run(s); check(vcsalive(s, &token), "only successfully advertised owner is live");
	tx(f, id, VcOpen, 1); run(s);
	return token;
}

static void
finish(Fixture *f, VcStream *s)
{
	check(vcsstop(s) == 0 && vcsstop(s) == 0 && s->count == 0 && f->frees == 0,
		"logical stream stop invalidates/wipes bytes, never frees DMA");
	destroy(f); /* fixture has no other token/IRQ/Chan callers */
	free(s);
}

static void
readtests(void)
{
	Fixture *f;
	VcStream *s;
	VcStreamToken token, wrong;
	Vcu8 data[256], out[4096], expected[8192];
	int i, j, length, done;
	f = fixture(2, 32); s = stream(f); token = connect(f, s, 1);
	check(vcsread(s, &token, out, sizeof out) == VcStreamWait && vcsread(s, &token, nil, 0) == 0,
		"empty read waits, zero-length read succeeds");
	for(i = 0; i < 32; i++){
		for(j = 0; j < 256; j++) data[j] = i+j;
		data[0] = 0; data[1] = 0xff;
		memmove(expected+i*256, data, 256); complete(f, s->rxqueue, data, 256);
	}
	run(s);
	check(s->count == VcStreamFifo && s->rxcount-s->rxhead == 16 &&
		vcsstep(s, 1) == 0 && f->q[s->rxqueue].core.posted == 16,
		"full byte FIFO holds unread RX receipts without spinning/reuse");
	memset(out, 0xa5, sizeof out); wrong = token; wrong.identity++;
	check(vcsread(s, &wrong, out, 1) == VcStreamClosed && out[0] == 0xa5 && s->count == 4096,
		"foreign controller token cannot consume bytes");
	wrong = token; wrong.epoch++;
	check(vcsread(s, &wrong, out, 1) == VcStreamClosed, "foreign reset epoch rejected");
	wrong = token; wrong.port.generation++;
	check(vcsread(s, &wrong, out, 1) == VcStreamClosed, "stale/forged port generation rejected");
	done = 0;
	while(done < (int)sizeof expected){
		length = vcsread(s, &token, out, 17);
		check(length > 0 && length <= 17 && memcmp(out, expected+done, length) == 0,
			"short binary reads preserve NUL/ff and FIFO wrap/order");
		done += length; run(s);
	}
	check(s->count == 0 && f->q[s->rxqueue].core.posted == 32,
		"all held receipts resume and repost exactly once after reads");
	check(vcsread(s, &token, nil, 1) == VcInvalid && vcsread(s, &token, out, -1) == VcInvalid &&
		vcsread(s, &token, out, 4097) == VcInvalid, "read boundaries rejected before output access");
	complete(f, s->rxqueue, nil, 0); run(s);
	check(vcsread(s, &token, out, 1) == VcStreamWait, "zero-byte RX completion does not fabricate EOF/data");
	finish(f, s);
}

static void
writetests(void)
{
	Fixture *f;
	VcStream *s;
	VcStreamToken token, wrong;
	Vcu8 data[4096], *base, *payload;
	int i, slot, qi;
	f = fixture(1, 4); s = stream(f); token = connect(f, s, 0);
	for(i = 0; i < (int)sizeof data; i++) data[i] = i;
	qi = s->txqueue; base = f->q[qi].memory.base;
	for(i = 0; i < 4; i++) check(vcswrite(s, &token, data, sizeof data) == 256,
		"bounded short TX copies only descriptor capacity");
	check(vcswrite(s, &token, data, 1) == VcStreamWait && vcsstep(s, 1) == 0,
		"full TX queue waits instead of accepting/duplicating bytes");
	check(vcswrite(s, &token, nil, 0) == 0 && vcswrite(s, &token, nil, 1) == VcInvalid &&
		vcswrite(s, &token, data, -1) == VcInvalid && vcswrite(s, &token, data, 4097) == VcInvalid,
		"literal TX length/null boundaries");
	wrong = token; wrong.port.id++;
	check(vcswrite(s, &wrong, data, 1) == VcStreamClosed && vcsclose(s, &wrong) == VcStreamClosed &&
		vcsalive(s, &token), "wrong-port token cannot write/close actual owner");
	memset(data, 0xee, sizeof data);
	for(i = 0; i < 4; i++){
		slot = *(Vcu16*)(base+f->q[qi].layout.avail+4+2*(f->consumed[qi]&3));
		payload = base+f->q[qi].layout.buffers+slot*f->q[qi].layout.stride;
		check(payload[0] == 0 && payload[1] == 1 && payload[255] == 255,
			"caller mutation cannot change accepted literal DMA payload");
		complete(f, qi, nil, 0);
	}
	run(s); check(vcswrite(s, &token, data, 3) == 3, "TX completion releases backpressure once");
	complete(f, qi, nil, 0); run(s);
	finish(f, s);
}

static void
disconnecttests(void)
{
	Fixture *f;
	VcStream *s;
	VcStreamToken token, untouched, saved;
	Vcu8 data[8], out[8];
	int kind, before;
	memset(data, 0x5a, sizeof data);
	for(kind = 0; kind < 2; kind++){
		f = fixture(1, 2); s = stream(f); token = connect(f, s, 0);
		complete(f, s->rxqueue, data, sizeof data); run(s);
		check(s->count == sizeof data && vcswrite(s, &token, data, sizeof data) == sizeof data,
			"buffered RX and in-flight TX before close");
		before = f->q[s->rxqueue].avail;
		if(kind == 0) check(vcsclose(s, &token) == 0, "explicit close accepted");
		else{ event(f, 0, VcRemove, 0, nil); run(s); }
		check(!vcsalive(s, &token) && s->count == 0 && vcsread(s, &token, out, 1) == VcStreamClosed &&
			vcswrite(s, &token, data, 1) == VcStreamClosed, "guest close/remove wipes unread bytes and stops IO");
		complete(f, s->rxqueue, data, sizeof data); complete(f, s->txqueue, nil, 0); run(s);
		check(s->count == 0 && f->q[s->rxqueue].avail == before && f->frees == 0,
			"late receipts discarded/reaped, never retagged/reposted for new owner");
		memset(&untouched, 0xa5, sizeof untouched); saved = untouched;
		check(vcsopen(s, &untouched) == VcStreamReset && memcmp(&untouched, &saved, sizeof saved) == 0,
			"reopen demands full reset, no ABA token/posted-buffer reassignment");
		finish(f, s);
	}
}

static void
hostreconnects(void)
{
	Fixture *f;
	VcStream *s;
	VcStreamToken token, duplicate;
	Vcu8 data[8], out[8];
	int i, id, posted;
	Vcu64 serial;
	f = fixture(1, 4); s = stream(f);
	event(f, 0, VcAdd, 0, nil); drain(f); tx(f, 0, VcPortReady, 1); drain(f);
	event(f, 0, VcName, 0, "org.qemu.guest_agent.0"); run(s);
	check(vcsopen(s, &token) == 0, "guest opens before any host socket exists");
	run(s); tx(f, 0, VcOpen, 1); run(s);
	check(vcsalive(s, &token) && !f->p.ports.port[0].hostopen &&
		vcswrite(s, &token, (Vcu8*)"x", 1) == VcStreamWait, "closed backend blocks TX, not guest FD");
	memset(data, 0, sizeof data); data[1] = 0xff;
	for(i = 0; i < 257; i++){
		event(f, 0, VcOpen, 1, nil); run(s);
		check(vcswrite(s, &token, data, sizeof data) == sizeof data, "new RPC uses same open stream");
		id = (f->q[s->rxqueue].avail-f->q[s->rxqueue].core.posted)%4;
		serial = f->q[s->rxqueue].serial[id]; posted = f->q[s->rxqueue].avail;
		event(f, 0, VcOpen, 0, nil); run(s);
		check(vcsalive(s, &token) && vcsopen(s, &duplicate) == VcInvalid &&
			vcswrite(s, &token, data, 1) == VcStreamWait &&
			f->q[s->rxqueue].avail == posted && f->q[s->rxqueue].serial[id] == serial,
			"host close neither retags descriptors nor replaces guest owner");
		complete(f, s->rxqueue, data, sizeof data); complete(f, s->txqueue, nil, 0); run(s);
		check(vcsread(s, &token, out, sizeof out) == sizeof out && memcmp(out, data, sizeof out) == 0,
			"already received bytes survive backend close; no RPC cancellation");
		check(vcsread(s, &token, out, 1) == VcStreamWait && f->frees == 0,
			"between RPCs read waits, no EOF/reset/free");
	}
	check(vcsclose(s, &token) == 0 && !vcsalive(s, &token), "guest clunk while host closed still invalidates owner");
	finish(f, s);
}

static void
consoletests(void)
{
	Fixture *f;
	VcStream *s;
	VcStreamToken token;
	Vcu8 text[4], out[4];
	int id, i;
	f = fixture(3, 4); s = stream(f); token = connect(f, s, 2);
	for(id = 0; id < 2; id++){
		event(f, id, VcAdd, 0, nil); drain(f); tx(f, id, VcPortReady, 1); drain(f);
		event(f, id, VcConsole, 1, nil); run(s); tx(f, id, VcOpen, 1); run(s);
	}
	memset(text, 0x7f, sizeof text);
	for(i = 0; i < 8; i++){
		complete(f, 0, text, sizeof text); complete(f, 4, text, sizeof text);
		complete(f, s->rxqueue, text, sizeof text);
		/* Single-action iterations alternate sink and active work; no sink can
		 * starve the real QGA stream, or forward console bytes into its FIFO. */
		runbudget(s, 1);
		check(vcsread(s, &token, out, sizeof out) == sizeof out && memcmp(out, text, sizeof out) == 0 &&
			vcsread(s, &token, out, 1) == VcStreamWait, "fair console discard separate from exact QGA delivery");
	}
	check(f->q[0].core.posted == 4 && f->q[4].core.posted == 4, "console leases recycled without client ownership");
	finish(f, s);
	/* Removed console re-added as QGA cannot inherit its old posted sink leases. */
	f = fixture(1, 2); s = stream(f);
	event(f, 0, VcAdd, 0, nil); drain(f); tx(f, 0, VcPortReady, 1); drain(f);
	event(f, 0, VcConsole, 1, nil); run(s); tx(f, 0, VcOpen, 1); run(s);
	event(f, 0, VcRemove, 0, nil); run(s);
	event(f, 0, VcAdd, 0, nil); run(s); tx(f, 0, VcPortReady, 1); run(s);
	event(f, 0, VcName, 0, "org.qemu.guest_agent.0"); event(f, 0, VcOpen, 1, nil); run(s);
	check(vcsopen(s, &token) == VcStreamReset, "console-to-QGA ID reuse requires device reset");
	finish(f, s);
}

static void
faulttests(void)
{
	Fixture *f;
	VcStream *s;
	VcStreamToken token;
	Vcu8 data[4], *entry;
	int kind;
	memset(data, 0x33, sizeof data);
	for(kind = 0; kind < 6; kind++){
		f = fixture(1, 2); s = stream(f); token = connect(f, s, 0);
		if(kind < 3){
			if(kind < 2) f->failread = f->reads+kind+1;
			else f->failwrite = f->writes+1;
			check(vcswrite(s, &token, data, sizeof data) == VcFault,
				"each TX kick failure holds possibly published payload");
		}else{
			if(kind == 3){
				complete(f, s->rxqueue, data, sizeof data);
				entry = (Vcu8*)f->q[s->rxqueue].memory.base+f->q[s->rxqueue].layout.used+4;
				put32(entry, 256);
			}
			if(kind == 4){
				check(vcswrite(s, &token, data, sizeof data) == sizeof data, "malformed TX setup");
				complete(f, s->txqueue, nil, 0);
				entry = (Vcu8*)f->q[s->txqueue].memory.base+f->q[s->txqueue].layout.used+4;
				put32(entry+4, 1);
			}
			if(kind == 5) check(vcmmreset(&f->m) == 0, "external reset injection");
			check(vcsstep(s, 256) == VcFault, "malformed RX/TX/reset is fail-stop");
		}
		check(s->state == VcStreamFault && f->p.state == VcPumpFault && s->count == 0 &&
			vcsread(s, &token, data, 1) == VcFault && f->frees == 0,
			"fault propagation invalidates owner and preserves all DMA/mappings");
		f->failread = f->failwrite = 0;
		finish(f, s);
	}
}

static void
wraptests(void)
{
	Fixture *f;
	VcStream *s;
	VcStreamToken token;
	Vcu8 data[3], out[3];
	int i;
	f = fixture(1, 1); s = stream(f); token = connect(f, s, 0);
	for(i = 0; i < 65538; i++){
		data[0] = i; data[1] = 0; data[2] = 0xff;
		complete(f, s->rxqueue, data, sizeof data); run(s);
		check(vcsread(s, &token, out, sizeof out) == sizeof out && memcmp(data, out, sizeof data) == 0 &&
			vcswrite(s, &token, data, sizeof data) == sizeof data, "literal IO over repeated single-slot reuse");
		complete(f, s->txqueue, nil, 0); run(s);
	}
	check(f->q[s->rxqueue].avail == 3 && f->q[s->txqueue].avail == 2 &&
		f->q[s->txqueue].core.used == 2, "actual data RX/TX 16-bit wrap, never forged counters");
	finish(f, s);
}

static void
budgettests(void)
{
	VcQueueInfo info[VcQueuesMax];
	VcPumpBudget b, saved, base;
	int i;
	memset(info, 0, sizeof info);
	for(i = 0; i < VcQueuesMax; i++){
		info[i].id = i; info[i].size = 256; info[i].epoch = 1; info[i].notify = i*2;
	}
	check(vcsbudget(&b, info, VcQueuesMax, 64, 256, VcPumpBudgetMax) == 0 &&
		vcpumpbudget(&base, info, VcQueuesMax, 64, 256, VcPumpBudgetMax) == 0 &&
		b.total == base.total+sizeof(VcStream), "all stream scratch/FIFO/receipts included before allocation");
	saved = b;
	check(vcsbudget(&b, info, VcQueuesMax, 64, 256, b.total-1) == VcInvalid &&
		memcmp(&b, &saved, sizeof b) == 0, "stream budget rejection is atomic");
	check(vcsbudget(&b, info, VcQueuesMax, 64, 256, sizeof(VcStream)) == VcInvalid &&
		vcsbudget(&b, info, VcQueuesMax, 64, 256, (Vcu64)VcPumpBudgetMax+1) == VcInvalid,
		"small/expanded policy budget rejected without unsigned subtraction wrap");
}

static void
ordered(void)
{
	Fixture *f;
	VcStream *s;
	VcStreamToken token;
	Vcu8 first[2], second[2], out[4], saved[8], *entry;
	f = fixture(1, 4); s = stream(f); token = connect(f, s, 0);
	first[0] = 0; first[1] = 0x11; second[0] = 0xff; second[1] = 0x22;
	complete(f, s->rxqueue, first, 2); complete(f, s->rxqueue, second, 2);
	entry = (Vcu8*)f->q[s->rxqueue].memory.base+f->q[s->rxqueue].layout.used+4;
	memmove(saved, entry, 8); memmove(entry, entry+8, 8); memmove(entry+8, saved, 8);
	run(s);
	check(vcsread(s, &token, out, 4) == 4 && out[0] == 0xff && out[1] == 0x22 &&
		out[2] == 0 && out[3] == 0x11, "raw byte stream follows used order, never descriptor-ID sorting");
	finish(f, s);
}

static void
initialization(void)
{
	Fixture *f;
	VcStream *s, *other;
	VcStreamToken token;
	f = fixture(1, 2);
	s = malloc(sizeof *s); other = malloc(sizeof *other);
	check(s != nil && other != nil, "checked initialization fixtures");
	memset(s, 0, sizeof *s); memset(other, 0, sizeof *other);
	check(vcsinit(s, &f->p, 0, VcPumpBudgetMax) == VcInvalid && !f->p.deferred &&
		vcsinit(s, &f->p, 77, f->p.budget.total+sizeof *s-1) == VcInvalid && !f->p.deferred,
		"invalid identity/footprint leaves pump publication mode unchanged");
	check(vcsread(s, &token, nil, 0) == VcInvalid && vcsstop(s) == VcInvalid &&
		!vcsalive(s, nil), "uninitialized stream never acquires owner authority");
	check(vcsinit(s, &f->p, 77, VcPumpBudgetMax) == 0 &&
		vcsinit(other, &f->p, 78, VcPumpBudgetMax) == VcInvalid,
		"one data adapter per fresh pump, no competing lifecycle stopper");
	free(other);
	check(vcpumpstart(&f->p) == 0 && vcpumpdeferdata(&f->p) == VcInvalid,
		"cannot change RX startup mode on a running device");
	tx(f, 0, VcDeviceReady, 1); run(s);
	finish(f, s);
}

#ifdef QGA_HOST_TEST
int main(void)
#else
void main(void)
#endif
{
	budgettests(); initialization(); readtests(); writetests(); disconnecttests(); hostreconnects(); consoletests();
	faulttests(); ordered(); wraptests();
#ifdef QGA_HOST_TEST
	printf("virtio stream: %d checks passed (simulated DMA/MMIO, reset-required reopen)\n", checks); return 0;
#else
	print("virtio stream: %d checks passed (simulated DMA/MMIO, reset-required reopen)\n", checks); exits(nil);
#endif
}
