/* SPDX-License-Identifier: MIT */
/* Compile-only check against the actual stock PC64 kernel headers, not libc.h.
 * This object is not linked into the daemon or installed as a kernel driver. */
#include <u.h>
#include "/sys/src/9/port/lib.h"
#include "/sys/src/9/pc64/mem.h"
#include "/sys/src/9/pc64/dat.h"
#include "/sys/src/9/pc64/fns.h"
#define QGA_VCONSOLE_KERNEL
#include "../kernel/vconsole.c"
