// SPDX-License-Identifier: MIT
#include "CPU.h"
#include "MMU.h"

// Leon3 extension
// CASA Copmare and Swap from SparcV9

// The CASA instruction compares the low-order 32 bits of register r[rs2] with a word in
// memory pointed to by the word address in r[rs1]. If the values are equal, the low-order 32
// bits of register r[rd] are swapped with the contents of the memory word pointed to by the
// address in r[rs1] and the high-order 32 bits of register r[rd] are set to zero. If the values are
// not equal, the memory location remains unchanged, but the zero-extended contents of the
// memory word pointed to by r[rs1] replace the low-order 32 bits of r[rd] and the high-order
// 32 bits of register r[rd] are set to zero.

void CPU::CASA (pDecode_t d)
{
    // Compare value of rs2 with memory location pointed by rs1
    u32 rs1_addr;
    read_reg(d->rs1 & LOBITS5, &rs1_addr);
    auto rs2 = d->imm_disp_rs2 & 0x1f; // Rs2 in 5 least sig bits
    auto imm_asi = (d->imm_disp_rs2 >> 5 ) & 0xff ; // asi in next 8 bits

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} casa    {} [{:#08x}] cmp {}, rd {} asi={:#08x}\n", d->pc, DispRegStr(d->rs1), d->rs1_value, DispRegStr(rs2), DispRegStr(d->rd), imm_asi);
#endif
 
    // The instruction is privileged but setting
    // ASI = 0xA (user data) will allow it to be used in user mode.
    if ((d->p->s == 0) && (imm_asi != 0xA)) {
        trap (d, SPARC_PRIVILEGED_INSTRUCTION);
        return;
    } else if (rs1_addr & LOBITS2) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
        return;
    }

    bool super = false;
    if( imm_asi == 0xB) {
        super = true;
    } else if (imm_asi == 0xA) {
        super = false;
    } else {
        throw not_implemented_leon_exception("ASI != 0xb|0xa not implemented for CASA");
    }

    // Set up for atomic CASA
    u32 expected;
    read_reg(rs2 & LOBITS5, &expected);

    u32 desired = 0x0;
    read_reg(d->rd & LOBITS5, &desired);

    bool swapped = false;

    auto r = mmu.atomic_casa32(rs1_addr, super, expected, desired, swapped);

    if(!r.ok && !mmu.get_no_fault()) {
        trap(d,  SPARC_DATA_ACCESS_EXCEPTION);
        return;
    }
    
    // We write to [rd], swapped or not
    write_reg(r.old, d->rd & LOBITS5);
    
    // Proceed
    d->pc = d->npc;
    d->npc += 4;
}
