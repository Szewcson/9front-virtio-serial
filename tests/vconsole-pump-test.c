#include "vconsole-fixture.h"

static void
budgettests(void)
{
	VcQueueInfo info[VcQueuesMax];
	VcPumpBudget b, saved;
	int ports, i, count, size;
	memset(info, 0, sizeof info);
	count = VcQueuesMax;
	for(ports = 1; ports <= VcPortMax; ports++){
		count = 2*ports+2;
		for(size = 1; size <= VcQueueMax; size *= 2){
			for(i = 0; i < count; i++){
				info[i].id = i; info[i].epoch = 1; info[i].size = size; info[i].notify = i*2;
			}
			check(vcpumpbudget(&b, info, count, ports, VcPumpDataCapacity, VcPumpBudgetMax) == 0 &&
				b.total == b.dma+b.metadata && b.total <= VcPumpBudgetMax,
				"all prototype ports/sizes fit default bounded footprint");
			saved = b;
			check(vcpumpbudget(&b, info, count, ports, VcPumpDataCapacity, saved.total-1) == VcInvalid &&
				memcmp(&b, &saved, sizeof b) == 0, "budget rejection commits no partial output");
		}
	}
	check(vcpumpbudget(&b, info, count, ports-1, VcBufferMax, VcPumpBudgetMax) == VcInvalid,
		"large payload footprint rejected, not allocated optimistically");
	check(vcpumpbudget(&b, info, 1, 0, 1, 1) == VcInvalid &&
		vcpumpbudget(&b, info, count, 64, 0, 1) == VcInvalid &&
		vcpumpbudget(&b, info, count, 64, 256, (Vcu64)VcPumpBudgetMax+1) == VcInvalid,
		"invalid dimensions and policy expansion rejected");
	for(i = 0; i < count; i++){
		info[i].epoch = 2;
		check(vcpumpbudget(&b, info, count, 64, 256, VcPumpBudgetMax) == VcInvalid,
			"mixed queue epochs rejected");
		info[i].epoch = 1;
		info[i].id++;
		check(vcpumpbudget(&b, info, count, 64, 256, VcPumpBudgetMax) == VcInvalid,
			"missing/reordered/duplicate ID rejected");
		info[i].id--;
	}
}

static void
lifecycle(void)
{
	Fixture *f;
	VcToken token, newer;
	int i;
	f = fixture(3, 8);
	check(vcpumpstep(&f->p, 1) == VcInvalid && vcpumpclaim(&f->p, &token) == VcInvalid,
		"nothing runs/claims before startup");
	check(vcpumpstart(&f->p) == 0 && vcpumpstart(&f->p) == VcInvalid, "one-shot startup");
	tx(f, 0, VcDeviceReady, 1); drain(f);
	event(f, 1, VcAdd, 0, nil); drain(f); tx(f, 1, VcPortReady, 1); drain(f);
	event(f, 1, VcName, 0, "org.qemu.guest_agent.0");
	event(f, 1, VcOpen, 1, nil); drain(f);
	check(vcpumpclaim(&f->p, &token) == 0 && token.id == 1 && !vcpumpalive(&f->p, &token),
		"claim is pending until OPEN posted/notified");
	drain(f); check(vcpumpalive(&f->p, &token), "only notified exact QGA owner is alive");
	tx(f, 1, VcOpen, 1); drain(f);
	event(f, 1, VcOpen, 1, nil); drain(f);
	check(vcpumpalive(&f->p, &token), "duplicate host-open does not invalidate a live claim");
	check(vcpumprelease(&f->p, &token) == 0 && !vcpumpalive(&f->p, &token), "release invalidates immediately");
	drain(f); tx(f, 1, VcOpen, 0); drain(f);
	check(vcpumpclaim(&f->p, &newer) == 0 && vcpumprelease(&f->p, &token) == VcInvalid,
		"stale release cannot close new owner");
	drain(f); tx(f, 1, VcOpen, 1); drain(f);
	event(f, 1, VcOpen, 0, nil); drain(f);
	check(vcpumpalive(&f->p, &newer), "host disconnect preserves advertised guest OPEN");
	event(f, 2, VcAdd, 0, nil); drain(f); tx(f, 2, VcPortReady, 1); drain(f);
	event(f, 2, VcConsole, 1, nil); drain(f); tx(f, 2, VcOpen, 1); drain(f);
	event(f, 2, VcName, 0, "text-console"); event(f, 2, VcOpen, 1, nil); drain(f);
	check(vcpumpclaim(&f->p, &token) == VcInvalid, "console is acknowledged but never claimed as QGA");
	for(i = 0; i < 65538; i++){
		event(f, 2, VcConsole, 1, nil); drain(f);
		tx(f, 2, VcOpen, 1); drain(f);
	}
	check(f->p.state == VcPumpRunning && vcpumpstep(&f->p, 0) == VcInvalid &&
		vcpumpstep(&f->p, 257) == VcInvalid && f->q[3].avail < 32 && f->q[3].core.used == f->q[3].avail,
		"real pump RX/TX/FIFO wrap and iteration bounds");
	destroy(f);
}

static void
backpressure(void)
{
	Fixture *f;
	VcToken token;
	int i, held, result;
	f = fixture(32, 32); check(vcpumpstart(&f->p) == 0, "backpressure startup");
	/* Fill TX with DEVICE_READY plus 31 PORT_READY replies, do not complete it. */
	for(i = 0; i < 31; i++){ event(f, i, VcAdd, 0, nil); drain(f); }
	for(i = 0; i < 17; i++) event(f, i, VcConsole, 1, nil);
	drain(f);
	held = f->p.rxcount-f->p.rxhead;
	check(f->p.count == VcPumpReplies && held == 1 && f->q[2].core.posted == 31 &&
		vcpumpclaim(&f->p, &token) == VcInvalid, "full reply FIFO holds RX receipt, no partial claim");
	result = vcpumpstep(&f->p, 1);
	check(result == 0 && f->p.rxcount-f->p.rxhead == held,
		"TX saturation sleeps, does not spin or recycle held RX");
	tx(f, 0, VcDeviceReady, 1); drain(f);
	for(i = 0; i < 31; i++){ tx(f, i, VcPortReady, 1); drain(f); }
	for(i = 0; i < 17; i++){ tx(f, i, VcOpen, 1); drain(f); }
	check(f->p.count == 0 && f->p.rxhead == f->p.rxcount && f->q[2].core.posted == 32,
		"replies preserve FIFO order and resume RX exactly once");
	destroy(f);
}

static void
stale(void)
{
	Fixture *f;
	VcToken token, saved;
	f = fixture(1, 1); check(vcpumpstart(&f->p) == 0, "single-slot startup");
	event(f, 0, VcAdd, 0, nil); drain(f);
	event(f, 0, VcRemove, 0, nil); drain(f);
	event(f, 0, VcAdd, 0, nil); drain(f);
	check(f->p.count == 1 && f->p.incarnation[0] == 3, "obsolete unsent PORT_READY discarded on ID reuse");
	tx(f, 0, VcDeviceReady, 1); drain(f); tx(f, 0, VcPortReady, 1); drain(f);
	event(f, 0, VcName, 0, "org.qemu.guest_agent.0"); drain(f);
	event(f, 0, VcOpen, 1, nil); drain(f);
	check(vcpumpclaim(&f->p, &token) == 0 && vcpumprelease(&f->p, &token) == 0,
		"pending OPEN may be released before device sees it");
	drain(f); /* keep CLOSE occupying the sole TX slot */
	check(vcpumpclaim(&f->p, &token) == 0, "new pending owner");
	saved = token;
	event(f, 0, VcOpen, 0, nil); drain(f);
	check(f->p.count == 1 && !vcpumpalive(&f->p, &saved),
		"host close preserves queued guest OPEN under TX backpressure");
	tx(f, 0, VcOpen, 0); drain(f);
	check(vcpumpalive(&f->p, &saved), "guest OPEN advertises even without host socket");
	event(f, 0, VcOpen, 1, nil); drain(f);
	check(vcpumpclaim(&f->p, &token) == VcInvalid && vcpumpalive(&f->p, &saved),
		"reconnect never publishes a second guest owner");
	check(vcpumprelease(&f->p, &saved) == 0, "guest close still advances ownership generation");
	tx(f, 0, VcOpen, 1); drain(f); tx(f, 0, VcOpen, 0); drain(f);
	check(vcpumpclaim(&f->p, &token) == 0, "new guest owner after explicit close");
	drain(f);
	check(vcpumpalive(&f->p, &token) && vcpumprelease(&f->p, &saved) == VcInvalid,
		"obsolete guest-close token cannot affect new advertised owner");
	tx(f, 0, VcOpen, 1); drain(f);
	destroy(f);
}

static void
faulttests(void)
{
	Fixture *f;
	VcToken token;
	Vcu8 bad[8];
	int i;
	for(i = 0; i < 5; i++){
		f = fixture(2, 2);
		if(i == 0){ f->badstart = 1; check(vcpumpstart(&f->p) == VcFault, "startup write failure holds all leases"); }
		else{
			check(vcpumpstart(&f->p) == 0, "fault fixture startup");
			if(i == 1) f->failread = f->reads+1;
			if(i == 2){ event(f, 0, VcAdd, 0, nil); f->failkick = 1; }
			if(i == 3){ memset(bad, 0, sizeof bad); put32(bad, 2); put16(bad+4, VcAdd); complete(f, 2, bad, 8); }
			if(i == 4) f->status |= VcNeedsReset;
			check(vcpumpstep(&f->p, 256) == VcFault, "malformed input/health/kick failure is fail-stop");
		}
		check(f->p.state == VcPumpFault && vcpumpstep(&f->p, 1) == VcFault &&
			vcpumpclaim(&f->p, &token) == VcFault && !vcpumpalive(&f->p, &token) && f->frees == 0,
			"sticky fault prevents ownership/progress, never frees ambiguous exposure");
		f->failread = f->failwrite = f->failkick = f->badstart = 0;
		destroy(f);
	}
}

static void
iofaults(void)
{
	Fixture *f;
	int reads, writes, startreads, startwrites, kind, i, r, w;
	f = fixture(2, 2); r = f->reads; w = f->writes;
	check(vcpumpstart(&f->p) == 0, "startup fault-count probe");
	startreads = f->reads-r; startwrites = f->writes-w;
	tx(f, 0, VcDeviceReady, 1); event(f, 0, VcAdd, 0, nil);
	r = f->reads; w = f->writes;
	check(vcpumpstep(&f->p, 256) >= 0, "receive/reply fault-count probe");
	reads = f->reads-r; writes = f->writes-w;
	destroy(f);
	for(kind = 0; kind < 4; kind++){
		for(i = 1; i <= (kind == 0 ? startreads : kind == 1 ? startwrites : kind == 2 ? reads : writes); i++){
			f = fixture(2, 2);
			if(kind >= 2){
				check(vcpumpstart(&f->p) == 0, "iteration fault fixture startup");
				tx(f, 0, VcDeviceReady, 1); event(f, 0, VcAdd, 0, nil);
			}
			if(kind == 0 || kind == 2) f->failread = f->reads+i;
			else f->failwrite = f->writes+i;
			check((kind < 2 ? vcpumpstart(&f->p) : vcpumpstep(&f->p, 256)) == VcFault &&
				f->p.state == VcPumpFault && f->frees == 0,
				"every startup/receive/reply MMIO failure retains ambiguous exposure");
			f->failread = f->failwrite = 0;
			destroy(f);
		}
	}
}

static void
validation(void)
{
	Fixture *f;
	VcPump *fresh;
	VcToken token, saved;
	VcQueueInfo info;
	int i, old;
	f = fixture(2, 4);
	fresh = malloc(sizeof *fresh);
	check(fresh != nil, "checked fresh validation object");
	memset(fresh, 0, sizeof *fresh);
	check(vcpumpinit(nil, &f->m, f->queue, f->count, VcPumpBudgetMax) == VcInvalid &&
		vcpumpinit(fresh, &f->m, f->queue, f->count-1, VcPumpBudgetMax) == VcInvalid &&
		vcpumpstart(fresh) == VcInvalid && vcpumpstep(fresh, 1) == VcInvalid &&
		vcpumpstop(fresh) == VcInvalid && !vcpumpalive(fresh, nil), "invalid or incomplete pump stays unusable");
	for(i = 0; i < f->count; i++){
		info = f->q[i].info;
		f->q[i].info.epoch++;
		check(vcpumpinit(fresh, &f->m, f->queue, f->count, VcPumpBudgetMax) == VcInvalid,
			"foreign epoch rejected before startup/publication");
		f->q[i].info = info;
		f->q[i].serial[0] = 1;
		check(vcpumpinit(fresh, &f->m, f->queue, f->count, VcPumpBudgetMax) == VcInvalid,
			"already-used queue cannot be adopted as fresh");
		f->q[i].serial[0] = 0;
		old = f->q[i].layout.capacity; f->q[i].layout.capacity++;
		check(vcpumpinit(fresh, &f->m, f->queue, f->count, VcPumpBudgetMax) == VcInvalid,
			"heterogeneous/wrong control payload capacity rejected");
		f->q[i].layout.capacity = old;
	}
	check(vcpumpinit(fresh, &f->m, f->queue, f->count, 1) == VcInvalid && fresh->state == VcPumpEmpty,
		"insufficient footprint leaves no usable pump");
	free(fresh);
	check(vcpumpstart(&f->p) == 0, "validation lifecycle startup");
	tx(f, 0, VcDeviceReady, 1); drain(f);
	event(f, 1, VcAdd, 0, nil); drain(f); tx(f, 1, VcPortReady, 1); drain(f);
	event(f, 1, VcName, 0, "org.qemu.guest_agent.0.evil");
	event(f, 1, VcOpen, 1, nil); drain(f);
	memset(&token, 0xa5, sizeof token); saved = token;
	check(vcpumpclaim(&f->p, &token) == VcInvalid && memcmp(&token, &saved, sizeof token) == 0,
		"QGA prefix lookalike is unavailable, output unchanged");
	event(f, 0, VcAdd, 0, nil); drain(f); tx(f, 0, VcPortReady, 1); drain(f);
	event(f, 0, VcName, 0, "org.qemu.guest_agent.0"); event(f, 0, VcOpen, 1, nil); drain(f);
	check(vcpumpclaim(&f->p, &token) == 0 && vcpumpclaim(&f->p, &saved) == VcInvalid,
		"exactly one owner before OPEN advertisement");
	drain(f); check(vcpumpalive(&f->p, &token), "valid advertised owner before reset");
	check(vcmmreset(&f->m) == 0 && !vcpumpalive(&f->p, &token) &&
		vcpumpstep(&f->p, 1) == VcFault && f->frees == 0,
		"external reset invalidates ownership but is not implicit reference/free authority");
	destroy(f);
}

static void
hostile(void)
{
	Fixture *f;
	Vcu8 bad[8], *entry;
	int i, j, id;
	for(i = 0; i < 7; i++){
		f = fixture(1, 2); check(vcpumpstart(&f->p) == 0, "hostile frame startup");
		if(i == 0){ event(f, 0, VcRemove, 0, nil); }
		if(i == 1){ event(f, 0, VcAdd, 0, nil); event(f, 0, VcAdd, 0, nil); }
		if(i == 2){
			tx(f, 0, VcDeviceReady, 1); event(f, 0, VcAdd, 0, nil); drain(f);
			event(f, 0, VcName, 0, "org.qemu.guest_agent.0"); event(f, 0, VcConsole, 1, nil);
		}
		if(i == 3){ memset(bad, 0, sizeof bad); put16(bad+4, VcDeviceReady); complete(f, 2, bad, 8); }
		if(i == 4){ memset(bad, 0, sizeof bad); complete(f, 2, bad, 7); }
		if(i == 5){
			id = complete(f, 3, nil, 0);
			entry = (Vcu8*)f->q[3].memory.base+f->q[3].layout.used+4;
			put32(entry+4, 1); check(get32(entry) == (Vcu32)id, "injected forbidden modern TX length");
		}
		if(i == 6){
			event(f, 0, VcAdd, 0, nil);
			entry = (Vcu8*)f->q[2].memory.base+f->q[2].layout.used+4;
			put32(entry, 256); /* hostile used ID, never a CPU-state mutation */
		}
		check(vcpumpstep(&f->p, 256) == VcFault && f->frees == 0,
			"direction/truncation/console/lifecycle/used-ring violation fails closed");
		for(j = 0; j < f->count; j++) check(f->q[j].memory.base != nil, "every allocation retained on fault");
		destroy(f);
	}
	f = fixture(1, 2); check(vcpumpstart(&f->p) == 0, "incarnation exhaustion startup");
	f->p.incarnation[0] = ~(Vcu64)0; /* serialized owner fault injection */
	event(f, 0, VcAdd, 0, nil);
	check(vcpumpstep(&f->p, 256) == VcFault && !f->p.ports.port[0].present,
		"incarnation never wraps or partially admits an add");
	destroy(f);
}

static void
ordered(void)
{
	Fixture *f;
	Vcu8 saved[8], *entry;
	f = fixture(2, 4); check(vcpumpstart(&f->p) == 0, "ordered receipt startup");
	tx(f, 0, VcDeviceReady, 1);
	event(f, 0, VcAdd, 0, nil); event(f, 1, VcAdd, 0, nil);
	/* Device completes the second buffer first. Used-ring order, not slot ID,
	 * determines the control-message stream. No CPU-owned receipt is changed. */
	entry = (Vcu8*)f->q[2].memory.base+f->q[2].layout.used+4;
	memmove(saved, entry, 8); memmove(entry, entry+8, 8); memmove(entry+8, saved, 8);
	check(vcpumpstep(&f->p, 1) == (VcPumpProgress|VcPumpAgain) && f->p.rxhead == 0,
		"one action budget reaps TX without partially consuming captured RX");
	check(vcpumpstep(&f->p, 1) == (VcPumpProgress|VcPumpAgain) &&
		f->p.ports.port[1].present && !f->p.ports.port[0].present,
		"first completed message is processed first, not lowest slot ID");
	check(vcpumpstep(&f->p, 1) == (VcPumpProgress|VcPumpAgain), "reply counts as one bounded action");
	tx(f, 1, VcPortReady, 1);
	drain(f); tx(f, 0, VcPortReady, 1); drain(f);
	check(f->p.ports.port[0].present && f->p.ports.port[1].present && f->q[2].core.posted == 4,
		"whole ordered receipt batch completes/reposts exactly once");
	destroy(f);
}

#ifdef QGA_HOST_TEST
int main(void)
#else
void main(void)
#endif
{
	budgettests(); lifecycle(); backpressure(); stale(); faulttests();
	iofaults(); validation(); hostile(); ordered();
#ifdef QGA_HOST_TEST
	printf("virtio control pump: %d checks passed (simulated DMA/MMIO, no hardware)\n", checks); return 0;
#else
	print("virtio control pump: %d checks passed (simulated DMA/MMIO, no hardware)\n", checks); exits(nil);
#endif
}
