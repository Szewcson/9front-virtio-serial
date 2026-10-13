/* SPDX-License-Identifier: MIT */
#ifndef QGA9_VCONSOLE_DEV_SHIM_H
#define QGA9_VCONSOLE_DEV_SHIM_H
/* Test-only native types/primitives. NOT a kernel, 9P server or guest runtime. */
#include "vconsole-owner-pc-shim.h"
#include <setjmp.h>
#include <stdarg.h>
typedef unsigned char uchar;
typedef unsigned long ulong;
typedef unsigned long long uvlong;
typedef long long vlong;
typedef struct Qid Qid;
typedef struct Chan Chan;
typedef struct Walkqid Walkqid;
typedef struct Dirtab Dirtab;
typedef struct Dir Dir;
typedef struct Block Block;
typedef struct Dev Dev;
typedef struct Proc Proc;
struct Qid { uvlong path; ulong vers; int type; };
struct Chan { Qid qid; int dev, flag, mode, iounit, refs; vlong offset; void *aux; };
struct Walkqid { Chan *clone; int nqid; Qid qid[16]; };
struct Dirtab { int unused; };
struct Dir { Qid qid; char *name, *uid; long mode; vlong length; };
struct Block { uchar *base, *rp, *wp; int capacity; };
struct Proc { char *user; };
struct Dev {
	int dc; char *name;
	void (*reset)(void), (*init)(void), (*shutdown)(void);
	Chan *(*attach)(char*);
	Walkqid *(*walk)(Chan*, Chan*, char**, int);
	int (*stat)(Chan*, uchar*, int);
	Chan *(*open)(Chan*, int), *(*create)(Chan*, char*, int, ulong);
	void (*close)(Chan*);
	long (*read)(Chan*, void*, long, vlong);
	Block *(*bread)(Chan*, long, ulong);
	long (*write)(Chan*, void*, long, vlong), (*bwrite)(Chan*, Block*, ulong);
	void (*remove)(Chan*);
	int (*wstat)(Chan*, uchar*, int);
};
enum { OREAD = 0, OWRITE, ORDWR, OEXEC, OTRUNC = 16, OCEXEC = 32, ORCLOSE = 64,
	COPEN = 1, QTDIR = 0x80, QTEXCL = 0x20, QTFILE = 0, DEVDOTDOT = -1,
	BIT16SZ = 2 };
#define DMDIR 0x80000000UL
#define DMEXCL 0x20000000UL
#define Eperm "permission denied"
#define Ebadarg "bad argument"
#define Enonexist "does not exist"
#define Eshortstat "short stat"
extern __thread Proc *up;
extern char *eve;
extern Dev vconsoledevtab;
typedef int Devgen(Chan*, char*, Dirtab*, int, int, Dir*);
int iseve(void);
void mkqid(Qid*, vlong, ulong, int);
void devdir(Chan*, Qid, char*, vlong, char*, long, Dir*);
void devreset(void);
void devinit(void);
void devshutdown(void);
Chan *devattach(int, char*);
Walkqid *devwalk(Chan*, Chan*, char**, int, Dirtab*, int, Devgen*);
int convD2M(Dir*, uchar*, int);
long devdirread(Chan*, char*, long, Dirtab*, int, Devgen*);
Chan *devcreate(Chan*, char*, int, ulong);
Block *devbread(Chan*, long, ulong);
long devbwrite(Chan*, Block*, ulong);
void devremove(Chan*);
int devwstat(Chan*, uchar*, int);
int snprint(char*, int, const char*, ...);
void tsleep(Rendez*, int(*)(void*), void*, ulong);
void wakeup(Rendez*);
void *testmemmove(void*, const void*, size_t);
void *vcdevtestregistry(void);
#define memmove testmemmove
/* Real native error() POPS the active frame before the nonlocal return. */
jmp_buf *testerrorpush(void);
void testerrorpop(void);
_Noreturn void error(char*);
_Noreturn void nexterror(void);
#define waserror() setjmp(*testerrorpush())
#define poperror() testerrorpop()
#endif
