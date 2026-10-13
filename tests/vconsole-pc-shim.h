/* SPDX-License-Identifier: MIT */
/* Test-only native-primitive stand-ins; never compiled into a guest. */
#ifndef QGA9_VCONSOLE_PC_SHIM_H
#define QGA9_VCONSOLE_PC_SHIM_H
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
typedef uintptr_t uintptr;
#define USED(x) ((void)(x))
#define BY2PG 4096
#define VMAPSIZE ((uint64_t)1<<40)
#define KZERO ((uintptr_t)4096)
#define PADDR(p) testpaddr(p)
uintptr_t testpaddr(void*);
void *mallocalign(unsigned long, unsigned long, long, unsigned long);
typedef struct Pcidev Pcidev;
struct Pcidev {
	uint16_t vid, did;
	int tbdf, msi;
	uint16_t pcr;
	uint8_t intl;
	void *testaux;
	struct { uint64_t bar; int64_t size; } mem[6];
};
int pcicfgr8(Pcidev*, int);
void *vmap(uint64_t, int64_t);
void vunmap(void*, int64_t);
extern void (*coherence)(void);
void delay(int);
#endif
