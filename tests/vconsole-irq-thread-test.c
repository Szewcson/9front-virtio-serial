/* SPDX-License-Identifier: MIT */
/* Real pthread concurrency of the mailbox; not a native hardware IRQ test. */
#include "../kernel/vconsole-irq.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct Fixture Fixture;
struct Fixture {
	VcIrq irq;
	pthread_mutex_t mailbox, wait;
	pthread_cond_t cond;
	int reads, fences, wakes, observed, hold, entered;
};

static void
check(int ok)
{
	if(!ok){ fputs("FAIL: threaded IRQ mailbox\n", stderr); abort(); }
}

static void
mailboxlock(void *aux)
{
	Fixture *f;
	f = aux; check(pthread_mutex_lock(&f->mailbox) == 0);
}

static void
mailboxunlock(void *aux)
{
	Fixture *f;
	f = aux; check(pthread_mutex_unlock(&f->mailbox) == 0);
}

static int
isrread(void *aux, Vcu32 *out)
{
	Fixture *f;
	f = aux;
	check(f->fences == 2*f->reads+1);
	f->reads++;
	*out = VcIrqQueue|VcIrqConfig;
	return 0;
}

static void
barrier(void *aux)
{
	Fixture *f;
	f = aux; f->fences++;
}

static void
workerwake(void *aux)
{
	Fixture *f;
	f = aux;
	check(pthread_mutex_lock(&f->wait) == 0);
	f->wakes++; f->entered = 1;
	check(pthread_cond_broadcast(&f->cond) == 0);
	while(f->hold) check(pthread_cond_wait(&f->cond, &f->wait) == 0);
	check(pthread_mutex_unlock(&f->wait) == 0);
}

static void
init(Fixture *f)
{
	static const VcIrqOps ops = {mailboxlock, mailboxunlock, isrread, barrier, workerwake};
	memset(f, 0, sizeof *f);
	check(pthread_mutex_init(&f->mailbox, nil) == 0 && pthread_mutex_init(&f->wait, nil) == 0 &&
		pthread_cond_init(&f->cond, nil) == 0 && vcirqinit(&f->irq, &ops, f, 7) == 0);
}

static void
destroy(Fixture *f)
{
	check(vcirqdrained(&f->irq));
	check(pthread_cond_destroy(&f->cond) == 0 && pthread_mutex_destroy(&f->wait) == 0 &&
		pthread_mutex_destroy(&f->mailbox) == 0);
}

static void*
oneirq(void *aux)
{
	Fixture *f;
	f = aux; check(vcirqinterrupt(&f->irq) == 1); return nil;
}

static void*
irqs(void *aux)
{
	Fixture *f;
	int i;
	f = aux;
	for(i = 0; i < 50000; i++) check(vcirqinterrupt(&f->irq) == 1);
	return nil;
}

static void*
work(void *aux)
{
	Fixture *f;
	int i;
	f = aux;
	for(i = 0; i < 50000; i++) check(vcirqraise(&f->irq, VcIrqWork) == 0);
	return nil;
}

static void*
worker(void *aux)
{
	Fixture *f;
	int bits;
	f = aux;
	for(;;){
		check(pthread_mutex_lock(&f->wait) == 0);
		while(!vcirqready(&f->irq)) check(pthread_cond_wait(&f->cond, &f->wait) == 0);
		check(pthread_mutex_unlock(&f->wait) == 0);
		check(vcirqtake(&f->irq, &bits) == 0 && !(bits&VcIrqFault));
		f->observed |= bits; /* only this worker writes, main reads after join */
		if(bits&VcIrqStop) return nil;
	}
}

int
main(void)
{
	Fixture f;
	pthread_t producer[3], consumer;
	int i, bits;

	init(&f); f.hold = 1;
	check(pthread_create(&producer[0], nil, oneirq, &f) == 0);
	check(pthread_mutex_lock(&f.wait) == 0);
	while(!f.entered) check(pthread_cond_wait(&f.cond, &f.wait) == 0);
	check(pthread_mutex_unlock(&f.wait) == 0);
	check(vcirqclose(&f.irq) == 0 && !vcirqdrained(&f.irq) && vcirqinterrupt(&f.irq) == 0);
	check(pthread_mutex_lock(&f.wait) == 0);
	f.hold = 0; check(pthread_cond_broadcast(&f.cond) == 0);
	check(pthread_mutex_unlock(&f.wait) == 0);
	check(pthread_join(producer[0], nil) == 0 && vcirqdrained(&f.irq) && f.reads == 1);
	check(vcirqtake(&f.irq, &bits) == 0 && bits == (VcIrqQueue|VcIrqConfig|VcIrqStop));
	destroy(&f);

	for(i = 0; i < 4; i++){
		init(&f);
		check(pthread_create(&consumer, nil, worker, &f) == 0);
		check(pthread_create(&producer[0], nil, irqs, &f) == 0 &&
			pthread_create(&producer[1], nil, work, &f) == 0 &&
			pthread_create(&producer[2], nil, work, &f) == 0);
		check(pthread_join(producer[0], nil) == 0 && pthread_join(producer[1], nil) == 0 &&
			pthread_join(producer[2], nil) == 0 && vcirqclose(&f.irq) == 0 &&
			pthread_join(consumer, nil) == 0);
		check(f.reads == 50000 && f.fences == 100000 &&
			f.observed == (VcIrqQueue|VcIrqConfig|VcIrqWork|VcIrqStop));
		destroy(&f);
	}
	puts("virtio IRQ threads: blocked-waker teardown and 600000 concurrent IRQ/work events passed");
	return 0;
}
