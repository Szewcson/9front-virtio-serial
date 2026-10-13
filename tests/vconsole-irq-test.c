/* SPDX-License-Identifier: MIT */
#include "../kernel/vconsole-irq.h"
#ifdef QGA_HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#endif
typedef struct Fixture Fixture;
struct Fixture { VcIrq irq; int locked, reads, fences, wakes, error, scenario; Vcu32 status; };
static int checks;

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
mailboxlock(void *aux)
{
	Fixture *f;
	f = aux;
	check(!f->locked, "no recursive mailbox lock");
	f->locked = 1;
}

static void
mailboxunlock(void *aux)
{
	Fixture *f;
	f = aux;
	check(f->locked, "unlock owns mailbox lock");
	f->locked = 0;
}

static int
isrread(void *aux, Vcu32 *out)
{
	Fixture *f;
	f = aux;
	check(f->locked && f->fences == 2*f->reads+1, "one ISR read under lock after barrier");
	f->reads++;
	*out = f->status;
	f->status = 0;
	return f->error ? VcFault : 0;
}

static void
barrier(void *aux)
{
	Fixture *f;
	f = aux;
	check(f->locked, "ISR barriers under mailbox lock");
	f->fences++;
}

static void
workerwake(void *aux)
{
	Fixture *f;
	int bits;
	f = aux;
	check(!f->locked, "wake never holds mailbox lock");
	f->wakes++;
	check(!vcirqdrained(&f->irq), "wake callback retains a counted reference");
	if(f->scenario){
		f->scenario = 0;
		check(vcirqtake(&f->irq, &bits) == 0 && bits != 0, "worker may consume before wake returns");
		check(vcirqclose(&f->irq) == 0 && !vcirqdrained(&f->irq),
			"close cannot drain an outstanding callback even after pending consumption");
	}
}

static void
init(Fixture *f)
{
	static const VcIrqOps ops = {mailboxlock, mailboxunlock, isrread, barrier, workerwake};
	memset(f, 0, sizeof *f);
	check(vcirqinit(&f->irq, &ops, f, 7) == 0, "fresh copied-ops mailbox");
}

#ifdef QGA_HOST_TEST
int main(void)
#else
void main(void)
#endif
{
	Fixture f;
	VcIrqOps missing;
	int bits, i, reads;
	memset(&missing, 0, sizeof missing);
	check(vcirqinit(nil, &missing, nil, 1) == VcInvalid &&
		vcirqinit(&f.irq, &missing, nil, 0) == VcInvalid &&
		vcirqinterrupt(nil) == VcInvalid && !vcirqready(nil) && !vcirqdrained(nil),
		"invalid or incomplete mailbox rejected");
	init(&f);
	check(!vcirqready(&f.irq) && vcirqtake(&f.irq, &bits) == 0 && bits == 0,
		"empty mailbox predicate and consumption");
	check(vcirqinterrupt(&f.irq) == 0 && f.reads == 1 && f.wakes == 0, "spurious shared INTx read, no wake");
	for(i = 1; i <= 254; i++){
		f.status = i;
		check(vcirqinterrupt(&f.irq) == ((i&3) != 0), "only defined ISR bits report work");
		check(vcirqtake(&f.irq, &bits) == 0 && bits == (i&3), "queue/config union, reserved bits ignored");
	}
	for(i = 0; i < 10000; i++) check(vcirqraise(&f.irq, VcIrqQueue|VcIrqWork) == 0, "coalesced IRQ/work storm");
	check(vcirqready(&f.irq) && vcirqtake(&f.irq, &bits) == 0 && bits == (VcIrqQueue|VcIrqWork),
		"pending predicate survives wake-before-sleep and coalescing");
	check(vcirqraise(&f.irq, 0) == VcInvalid && vcirqraise(&f.irq, VcIrqStop) == VcInvalid &&
		vcirqraise(&f.irq, 32) == VcInvalid && vcirqtake(&f.irq, nil) == VcInvalid,
		"caller cannot inject terminal or unknown bits");
	reads = f.reads;
	check(vcirqclose(&f.irq) == 0 && vcirqclose(&f.irq) == 0 && vcirqdrained(&f.irq), "idempotent closed/drained gate");
	f.status = 3;
	check(vcirqinterrupt(&f.irq) == 0 && f.reads == reads && vcirqraise(&f.irq, 1) == VcInvalid,
		"late IRQ dispatch never touches closed mapping");
	check(vcirqtake(&f.irq, &bits) == 0 && bits == VcIrqStop && vcirqready(&f.irq), "stop is sticky for worker exit");
	init(&f); f.scenario = 1;
	check(vcirqraise(&f.irq, VcIrqQueue) == 0 && vcirqdrained(&f.irq),
		"last wake reference returns before drained becomes true");
	for(i = 0; i < 3; i++){
		init(&f);
		f.status = i == 0 ? 255 : i == 1 ? 256 : 0;
		f.error = i == 2;
		check(vcirqinterrupt(&f.irq) == VcFault && vcirqtake(&f.irq, &bits) == 0 && bits == VcIrqFault &&
			vcirqready(&f.irq) && vcirqtake(&f.irq, &bits) == 0 && bits == VcIrqFault,
			"absent/invalid/failed ISR read latches sticky fault without MMIO state mutation");
		check(vcirqclose(&f.irq) == 0 && vcirqdrained(&f.irq), "faulted mailbox can close/drain");
	}
#ifdef QGA_HOST_TEST
	printf("virtio IRQ mailbox: %d checks passed (simulated callbacks)\n", checks); return 0;
#else
	print("virtio IRQ mailbox: %d checks passed (simulated callbacks)\n", checks); exits(nil);
#endif
}
