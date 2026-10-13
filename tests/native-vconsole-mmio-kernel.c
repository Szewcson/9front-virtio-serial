/* SPDX-License-Identifier: MIT */
/* Compile only against actual PC64 kernel declarations; not a loaded driver. */
#include <u.h>
#include "/sys/src/9/port/lib.h"
#include "/sys/src/9/pc64/mem.h"
#include "/sys/src/9/pc64/dat.h"
#include "/sys/src/9/pc64/fns.h"
#define QGA_VCONSOLE_KERNEL
#include "../kernel/vconsole-mmio.c"
