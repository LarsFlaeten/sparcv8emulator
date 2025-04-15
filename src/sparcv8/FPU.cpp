#include "CPU.h"
#include "MMU.h"
// ------------------------------------------------
// Reads the fsr value and stores it in memory address pointed by rs1 and rs2
void CPU::fpu_STFSR (pDecode_t d)
{
    if (verbose) 
        os << std::format("{:#08x} stfsr    {} = {:#08x}\n", d->pc, DispRegStr(d->ev), fpu_fsr);

    if (d->p->ef == 0) {
        trap (d,  SPARC_FP_DISABLED   );
    } else if (d->ev & LOBITS2) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        u32 va = d->ev;
        u32 value = fpu_fsr;
        u32 ret1 = MMU::MemAccess<intent_store,4>(va, value, CROSS_ENDIAN);
        if(ret1 < 0)
            throw("fix this"); //handleMMUFault(d);
        else {
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}

// ------------------------------------------------
// Reads a new fsr value from memory address pointed by rs1 and rs2 and stores it into fsr
void CPU::fpu_LDFSR (pDecode_t d)
{
    if (verbose) 
        os << std::format("{:#08x} ldfsr    [{:#08x}] => $fsr\n", d->pc, d->ev);

    if (d->p->ef == 0) {
        trap (d,  SPARC_FP_DISABLED   );
    } else if (d->ev & LOBITS2) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        u32 va = d->ev;
        u32 value = 0; // To avoid warning
        u32 ret1 = MMU::MemAccess<intent_load,4>(va, value, CROSS_ENDIAN);
        if(ret1 < 0)
            throw("fix this"); //handleMMUFault(d);
        else {
            fpu_fsr = value;
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}


