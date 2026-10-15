/* SPDX-License-Identifier: MIT */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#define QGA_VCONSOLE_KERNEL
#include "vconsole-controller-pc.h"

static void
ctllock(void *aux)
{
	VcPcCtlLock *gate;
	gate = aux; ilock(&gate->lock);
}

static void
ctlunlock(void *aux)
{
	VcPcCtlLock *gate;
	gate = aux; iunlock(&gate->lock);
}

static void*
ctlalloc(void *aux, int bytes)
{
	USED(aux);
	if(bytes < 1 || bytes > VcPumpBudgetMax) return nil;
	/* malloc may return nil. No smalloc sleep/error while acquiring ownership. */
	return malloc(bytes);
}

static void
ctlfree(void *aux, void *p, int bytes)
{
	USED(aux); USED(bytes);
	free(p);
}

int
vcpcctlops(VcCtlOps *out)
{
	static const VcCtlOps ops = {ctllock, ctlunlock, ctlalloc, ctlfree};
	if(out == nil) return VcInvalid;
	*out = ops;
	return 0;
}
