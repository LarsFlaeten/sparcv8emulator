#ifndef __SRCROOT_DEBUG_H__
#define __SRCROOT_DEBUG_H__
#include "sparcv8/CPU.h"

void debug_registerdump(CPU& cpu);
void debug_dumpmem(u32 pa, int n = 16);
void debug_dumpmemv(u32 va, int n = 16);

#endif
