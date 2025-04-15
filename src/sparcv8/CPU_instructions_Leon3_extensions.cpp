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

    if (verbose)
        os << std::format("{:#08x} casa    {} [{:#08x}] cmp {}, rd {} asi={:#08x}\n", d->pc, DispRegStr(d->rs1), d->rs1_value, DispRegStr(rs2), DispRegStr(d->rd), imm_asi);
 
    // The instruction is privileged but setting
    // ASI = 0xA (user data) will allow it to be used in user mode.
    if ((d->p->s == 0) && (imm_asi != 0xA)) 
        trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    else if (rs1_addr & LOBITS2) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    }

    u32 rs2_value;
    read_reg(rs2 & LOBITS5, &rs2_value);
   
    // Get value pointed to by rs1
    u32 rs1_value, mret;
    bool super = false;
    if( imm_asi == 0xB) {
        super = true;
    } else if (imm_asi == 0xA) {
        super = false;
    } else {
        throw not_implemented_leon_exception("ASI != 0xb|0xa not implemented for CASA");
        mret = -1;
    }
        
    mret = MMU::MemAccess<intent_load>(rs1_addr, rs1_value, CROSS_ENDIAN, super);
 
    if(mret < 0) {
        if(!MMU::GetNoFault())
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        return;
    }

    //if (verbose)
    //    os << std::format("  --> casa cmp   {:#08x} and {:#08x}\n", rs1_value, rs2_value);
    //    os << std::format("      Value from reg:  rs2={}:                {:#08x}\n", DispRegStr(rs2), rs2_value);
    //    os << std::format("      Value from mem:  rs1={}: [{:#08x}] = {:#08x}\n", DispRegStr(d->rs1), rs1_addr, rs1_value);


     
    // CMP. If equal, SWAP r_rd and rs1
    if(rs1_value == rs2_value) {

        
        u32 rd_value;
        read_reg(d->rd & LOBITS5, &rd_value);
        //os << std::format("      Value from reg:   rd={}:                {:#08x}\n", DispRegStr(d->rd), rd_value);

       // Write back swapped
        write_reg(rs1_value, d->rd & LOBITS5);
        mret = MMU::MemAccess<intent_store>(rs1_addr, rd_value, CROSS_ENDIAN, super); 
        if(mret < 0) {
            if(!MMU::GetNoFault())
                trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
            return;
        }

       //os << std::format("      ..swapped rd and rs1 \n", DispRegStr(d->rd),  rs1_value);
    } else {
        // Only RD changed by content of rs1
        write_reg(rs1_value, d->rd & LOBITS5);
        //os << std::format("      writing {:#08x} to rd:{}\n", rs1_value, DispRegStr(d->rd));
 
    }
    
    d->pc = d->npc;
    d->npc += 4;



}
