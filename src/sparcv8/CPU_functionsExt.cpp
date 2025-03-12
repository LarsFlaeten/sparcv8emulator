#include "CPU.h"
#include "MMU.h"

#define ASI_START_BIT 5


// Load alternate space
void CPU::LDA_impl (pDecode_t d) {
   
    if (d->p->s == 0) {
        Trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    } else if( d->i == 1) {
        Trap (d, SPARC_ILLEGAL_INSTRUCTION);
    }
    // First we need an extra decode here, to pull our ASI and rs2, both stored in simm13
    u32 rs2 = d->imm_disp_rs2 & LOBITS5; 
    u32 ASI = (d->imm_disp_rs2 >> ASI_START_BIT ) & LOBITS8;

    std::string op = "";
    // Decode op3
    switch (d->op_2_3 & 15) {
        case(0):
            op = "lda";
            break;
        default:
            throw std::runtime_error("Load Altternate op3 != 0x0 Not implemented yet");
   }

    u32 rd_value;
    ReadReg(d->rd, &rd_value);

    u32 r1, r2;
    ReadReg(d->rs1, &r1);
    ReadReg(rs2, &r2);
    u32 address = r1 + r2;

    if(verbose)
        os << std::format("{:#08x} {}      [{:#08x}] asi: {:#08x}, {}\n", d->PC, op, address, ASI, DispRegStr(d->rd));

    u32 offset = 0x0;
 
    switch(ASI) {
        case(ASI_M_MMUREGS):
            switch(address) {
                case(0x0):
                    d->value = MMU::GetControlReg();
                    break;
                case(0x100):
                    d->value = MMU::GetCtxTblPtr();
                    break;
                case(0x200):
                    d->value = MMU::GetCtxNumber();
                    break;
                case(0x300):
                    d->value = MMU::GetFaultStatus();
                    MMU::ClearFaultStatus(); // Clear on read, ref turboSparc
                    break;
                case(0x400):
                    d->value = MMU::GetFaultAddress();
                    break;
                default:
                    throw not_implemented_leon_exception("Address not implemented for MMU access"); 

            }
            d->wb_type = WriteBackType::WRITEBACKREG;
            break;
        case(ASI_M_MXCC):
            // Read cache control registers:
            // Offset   | Register
            // 0x00     | Cache control register
            // 0x04     | Reserved
            // 0x08     | Instruction cache configuration register
            // 0x0C     | Data cache configuration register
            offset = address & 0xf;
            switch(offset) {
                case(0x0):
                    d->value = MMU::GetCCR();
                    break;
                case(0x8):
                    d->value = MMU::GetICCR();
                    break;
                case(0xc):
                    d->value = MMU::GetDCCR();
                    break;
                default:
                    throw std::runtime_error("ASI=0x2 with offset != 0,8,c not implemented");
            }
            d->wb_type = WriteBackType::WRITEBACKREG;
            break;
        case(ASI_LEON_BYPASS):
            d->value = MMU::MemAccessBypassRead4(address, CROSS_ENDIAN);
            d->wb_type = WriteBackType::WRITEBACKREG;

            break;
        default:
            throw std::runtime_error("ASI assignment not implemented");
    }


    d->PC = d->nPC;
    d->nPC= d->nPC + 4;



}

// Store alternate space
void CPU::STA_impl (pDecode_t d) {
   
    if (d->p->s == 0) {
        Trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    } else if( d->i == 1) {
        Trap (d, SPARC_ILLEGAL_INSTRUCTION);
    }

    // First we need an extra decode here, to pull our ASI and rs2, both stored in simm13
    u32 rs2 = d->imm_disp_rs2 & LOBITS5; 
    u32 ASI = (d->imm_disp_rs2 >> ASI_START_BIT ) & LOBITS8;

    std::string op = "";
    // Decode op3
    switch (d->op_2_3 & 3) {
        case(0):
            op = "sta";
            break;
        case(1):
            op = "stba";
            break;
        case(2):
            op = "stha";
            break;
        case(3):
            op = "stda";
            break;
    }

    u32 rd_value;
    ReadReg(d->rd, &rd_value);

    u32 r1, r2;
    ReadReg(d->rs1, &r1);
    ReadReg(rs2, &r2);
    u32 address = r1 + r2;
    
    if (address & LOBITS2)
        Trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
 


    if(verbose)
        os << std::format("{:#08x} {}      {} {:#08x} , {:#08x} asi: {:#08x}\n", d->PC, op, DispRegStr(d->rd), rd_value, address, ASI);
            
    u32 offset = 0x0;
 
    switch(ASI) {
        case(ASI_M_MMUREGS):
            switch(address & ~0x11) {
                case(0x000):
                    MMU::SetControlReg(rd_value);
                    break;
                case(0x100):
                    MMU::SetCtxTblPtr(rd_value);
                    break;
                case(0x200):
                    MMU::SetCtxNumber(rd_value);
                    break;
                default:
                    Trap (d, SPARC_ILLEGAL_INSTRUCTION);
            }
            break;
        case(ASI_M_MXCC):
            // Write cache control registers:
            // Offset   | Register
            // 0x00     | Cache control register
            // 0x04     | Reserved
            // 0x08     | Instruction cache configuration register
            // 0x0C     | Data cache configuration register
            offset = address & 0xf;
            switch(offset) {
                case(0x0):
                    MMU::SetCCR(rd_value);
                    break;
                case(0x8):
                    MMU::SetICCR(rd_value);
                    break;
                case(0xc):
                    MMU::SetDCCR(rd_value);
                    break;
                default:
                    throw std::runtime_error("ASI=0x2 with offset != 0,8,c not implemented");
            }
            break;
        case(ASI_LEON_DFLUSH):
            MMU::nop();
            break;
        case(ASI_LEON_MMUFLUSH):
            MMU::flush();
            break;
        case(ASI_LEON_BYPASS):
            MMU::MemAccessBypassWrite4(address, rd_value, CROSS_ENDIAN);
            break;
        default:
            throw std::runtime_error("ASI assignment not implemented");
    }


    d->PC = d->nPC;
    d->nPC= d->nPC + 4;

}
