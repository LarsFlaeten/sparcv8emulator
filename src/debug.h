// SPDX-License-Identifier: MIT
#ifndef __SRCROOT_DEBUG_H__
#define __SRCROOT_DEBUG_H__
#include "sparcv8/CPU.h"

void debug_registerdump(CPU& cpu);
void debug_set_active_mmu(MMU* mmu);
void debug_dumpmem(u32 pa, int n = 16);
void debug_dumpmemv(u32 va, int n = 16);
void debug_mmu_tables();
void debug_mmu_tlbs();
void debug_print_amba_pnp();
void debug_print_memory_banks();

std::string rs_reason_str(TerminateReason trs);

#endif
