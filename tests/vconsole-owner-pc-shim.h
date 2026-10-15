/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_OWNER_PC_SHIM_H
#define QGA9_VCONSOLE_OWNER_PC_SHIM_H
#include "vconsole-pc-shim.h"
typedef struct Lock Lock;
typedef struct Rendez Rendez;
typedef struct Ureg Ureg;
struct Lock { int unused; };
struct Rendez { int unused; };
struct Ureg { int unused; };
void ilock(Lock*);
void iunlock(Lock*);
int pcicfgr16(Pcidev*, int);
void pcicfgw16(Pcidev*, int, int);
void intrenable(int, void(*)(Ureg*, void*), void*, int, char*);
int intrenablechecked(int, void(*)(Ureg*, void*), void*, int, char*);
void intrdisable(int, void(*)(Ureg*, void*), void*, int, char*);
#endif
