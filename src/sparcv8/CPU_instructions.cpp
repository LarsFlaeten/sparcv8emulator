#include <time.h>

#include "CPU.h"
#include "MMU.h"

#define CalcCC(_cc, _val, _PSR) {                                            \
    _cc &= ~(1 << CC_OVERFLOW);                                              \
    _cc &= ~(1 << CC_CARRY);                                                 \
    _cc = (_cc & ~(1 << CC_ZERO))     | (((_val == 0) ? 1 : 0) << CC_ZERO);  \
    _cc = (_cc & ~(1 << CC_NEGATIVE)) | (((_val >> 31) & 1) << CC_NEGATIVE); \
    _PSR = (_PSR & ~(LOBITS4 << PSR_CC_CARRY)) | (_cc << PSR_CC_CARRY);      \
}


// ------------------------------------------------

void CPU::UNIMP (pDecode_t d)
{
    if (d->opcode != TERMINATE_INST) {
        std::cerr << "*** Error: Unimplemented instruction.\n";
            
        rs.reason = TerminateReason::UNIMPLEMENTED; 
        rs.last_opcode = d->opcode;
    } else {
        os << std::format("{:#08x} unimp : Program exited normally\n", d->pc);
        rs.reason = TerminateReason::NORMAL;
        rs.last_opcode = d->opcode;
    }

}

// ------------------------------------------------

void CPU::CALL (pDecode_t d) 
{
    u32 temp;

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} call     {:#08x} to {:#08x}\n", d->pc, d->imm_disp_rs2, d->pc + (4 * d->imm_disp_rs2));
#endif

    d->wb_type = WriteBackType::WRITEBACKREG;
    d->value = d->pc;
    d->rd = OUTREG7;

    temp   = d->pc;
    d->pc  = d->npc;
    d->npc = temp + (4 * d->imm_disp_rs2);
}

// ------------------------------------------------

void CPU::BICC (pDecode_t d)
{
    int branch, annul, temp;

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} b{} {:#d} cc={:#1x}\n", d->pc, cond_byte[d->rd & 0xf], (signed int)(sign_ext22(d->imm_disp_rs2*4)), (d->psr >> PSR_CC_CARRY) & LOBITS4);
#endif

    branch = test_cc (d);
    annul = ((d->rd >> 4) & LOBITS1) && (((!branch) && ((d->rd & LOBITS3) != 0)) || ((d->rd & LOBITS3) == 0));

    temp = d->npc;
    if (branch)
        d->npc = d->pc + 4 * sign_ext22(d->imm_disp_rs2);
    else 
        d->npc += 4;
    d->pc = temp;

    if (annul) {
        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::SETHI (pDecode_t d)
{
    u32 temp;

    if (d->opcode == NOP) {
#ifdef CPU_VERBOSE
            os << std::format("{:#08x} nop\n", d->pc);
#endif
    } else {
        temp = d->imm_disp_rs2 << 10;
#ifdef CPU_VERBOSE
            os << std::format("{:#08x} sethi    Opcode {:#08x} -> {} = {:#08x}\n", d->pc, d->opcode, DispRegStr(d->rd), temp);
#endif
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = temp;
    }
    d->pc = d->npc;
    d->npc += 4;
}

// ------------------------------------------------

void CPU::SLL (pDecode_t d)
{
    d->wb_type = WriteBackType::WRITEBACKREG;
    d->value = d->rs1_value << (d->ev & LOBITS5);

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} sll       {} {:#08x} << {:#02x} = {:#08x} {}\n", d->pc, DispRegStr(d->rs1),
                 d->rs1_value, d->ev, d->value, DispRegStr(d->rd));
#endif

    d->pc = d->npc;
    d->npc += 4;
}

// ------------------------------------------------

void CPU::SRL (pDecode_t d)
{
    d->wb_type = WriteBackType::WRITEBACKREG;
    d->value = d->rs1_value >> (d->ev & LOBITS5);

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} srl       {} {:#08x} << {:#02x} = {:#08x} {}\n", d->pc, DispRegStr(d->rs1),
                 d->rs1_value, d->ev, d->value, DispRegStr(d->rd));
#endif

    d->pc = d->npc;
    d->npc += 4;
}

// ------------------------------------------------

void CPU::SRA (pDecode_t d)
{
    d->wb_type = WriteBackType::WRITEBACKREG;
    d->value = (d->rs1_value >> (d->ev & LOBITS5)) | (((d->rs1_value & BIT31) && d->ev) ? (0xffffffff << (32 - (d->ev & LOBITS5))) : 0);

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} sra       {} {:#08x} << {:#02x} = {:#08x} {}\n", d->pc, DispRegStr(d->rs1),
                 d->rs1_value, d->ev, d->value, DispRegStr(d->rd));
#endif

    d->pc = d->npc;
    d->npc += 4;
}

// ------------------------------------------------

void CPU::RDY (pDecode_t d)
{
    if (d->rs1 == 15 && d->rd == 0) {
#ifdef CPU_VERBOSE
            os << std::format("{:#08x} stbar\n", d->pc);
#endif
        nop();
    } else {
        if (d->rs1 == 0 && (d->op_2_3 & 0x2f) == 0x28) {
#ifdef CPU_VERBOSE
                os << std::format("{:#08x} rdy      {} = {:#08x}\n", d->pc, DispRegStr(d->rd), y_reg);
#endif
            d->wb_type = WriteBackType::WRITEBACKREG;
            d->value = y_reg;
        } else if(d->rs1 == 17 && (d->op_2_3 & 0x2f) == 0x28) {
            // Leon specific, read CPU id, NWINDOWS etc from ASR17
#ifdef CPU_VERBOSE
                os << std::format("{:#08x} rd %asr17      {} = {:#08x}\n", d->pc, DispRegStr(d->rd), this->cpu_id_);
#endif
            d->wb_type = WriteBackType::WRITEBACKREG;
            d->value = (this->cpu_id_ << 28) 
                | (0x1 << 26)   // NOTAG / CASA
                | (0b11 << 10)  // GRFP liteU
                | (0x1 << 8)    // SparcV8 MUL/DIV
                | ((NWINDOWS-1) & 0x1f) ;
        }
        else 
           UNIMP(d);
    }
    d->pc = d->npc;
    d->npc += 4;
}

// ------------------------------------------------

void CPU::RDPSR (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} rdpsr    {} = {:#08x}\n", d->pc, DispRegStr(d->rd), d->psr);
#endif

    if (d->p->s == 0) {
        trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    } else {
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = d->psr;
        d->pc = d->npc;
        d->npc= d->npc + 4;
    }
}

// ------------------------------------------------

void CPU::RDWIM (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} rdwim    {} = {:#08x}\n", d->pc, DispRegStr(d->rd), wim);
#endif

    if (d->p->s == 0) {
        trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    } else {
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = wim;
        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::RDTBR (pDecode_t d)
{

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} rdtbr    {} = {:#08x}\n", d->pc, DispRegStr(d->rd), tbr);
#endif

    if (d->p->s == 0) {
        trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    } else {
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = tbr;
        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::WRY (pDecode_t d)
{
    if ((d->rd & LOBITS5) == 0) {
        y_reg = d->rs1_value ^ d->ev;
#ifdef CPU_VERBOSE
            os << std::format("{:#08x} wry      = {:#08x}\n", d->pc, y_reg);
#endif
    } else {
        // This is wr %asr!
        if(d->rd == 19) {
            if(power_down_enabled)
                enter_powerdown(); // Will hang here until wakeup
            else
                nop();
        } else {
            UNIMP(d);
        }
    } 

    d->pc = d->npc;
    d->npc += 4;
}

// ------------------------------------------------

void CPU::WRPSR (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} wrpsr    = {:#08x}\n", d->pc, d->rs1_value ^ d->ev);
#endif

    if (((d->rs1_value ^ d->ev) & LOBITS5) >= NWINDOWS) { 
       //os << std::format("Ooops, rs1:{:#08x}, ev:{:#08x}, rs1^ev[4:0]:{:#08x}, NWIN:{:#08x}\n", d->rs1_value, d->ev, (d->rs1_value^d->ev)&LOBITS5, NWINDOWS);

       trap (d, SPARC_ILLEGAL_INSTRUCTION);
    }
    else if (d->p->s == 0) 
        trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    else {
        // Hard wire LEON specific values..
        d->psr = (0xf << 28) | (0x3 << 24) | (d->rs1_value ^ d->ev);

        // disable EC (and EF if not implemented):
#ifdef FPU_IMPLEMENTED
        d->psr = d->psr & ~(0x1 << 13);
#else
        d->psr = d->psr & ~(0x1 << 13) & ~(0x1 << 12);
#endif        
        // disable resevred field:
        d->psr = d->psr & ~(0x3f << 14);

        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::WRWIM (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} wrwim    = {:#08x}\n", d->pc, d->rs1_value ^ d->ev);
#endif

    if (d->p->s == 0)
        trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    else {
        // Only allow WIM to be set up to number of windows
        // Reference page 30
        wim = (d->rs1_value ^ d->ev) & ((0x1 << (NWINDOWS))-1);
        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::WRTBR (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} wrtbr    = {:#08x}\n", d->pc, d->rs1_value ^ d->ev);
#endif

    if (d->p->s == 0) 
        trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    else {
        tbr = d->rs1_value ^ d->ev;
        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::JMPL (pDecode_t d)
{
    //u32 temp;


#ifdef CPU_VERBOSE
        os << std::format("{:#08x} jmpl     rs1 {:#08x} + ev => nPC = {:#08x}\n", d->pc, d->rs1_value, d->npc);
#endif

    if (d->ev & LOBITS2) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = d->pc;
        //temp = d->pc;
        d->pc = d->npc;
        d->npc = d->ev;
    }
}

// ------------------------------------------------

void CPU::RETT (pDecode_t d)
{
    //u32 temp;
    u32 new_cwp;

    new_cwp = (d->p->cwp + 1) % NWINDOWS;
    //os << std::format("RETT, new cwp {:#x}\n", new_cwp);
 
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} rett     rs1 {:#08x} + ev => nPC = {:#08x} (new CWP = {:#08x})\n", d->pc, d->rs1_value, d->ev & ~LOBITS2, new_cwp);
#endif

    if ( (d->p->et && !d->p->s) || (!d->p->et && !d->p->s))
        trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    else if (d->p->et && d->p->s)
        trap (d, SPARC_ILLEGAL_INSTRUCTION);
    else if (d->ev & LOBITS2)
        trap (d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    else if (((wim >> new_cwp) & LOBITS1) != 0)
        trap (d, SPARC_WINDOW_UNDERFLOW);
    else {
        //temp = d->pc;
        d->pc = d->npc;
        d->npc = d->ev & ~LOBITS2;
        d->p->s = d->p->ps;
        d->p->et = 1;
        d->p->cwp = new_cwp & LOBITS5;
    }

}

// ------------------------------------------------

void CPU::TICC (pDecode_t d)
{
    // Check for ta 1 software trap
    if(gdb_stub != nullptr) {
        if(d->opcode == 0x91d02001) {
            if(gdb_stub->has_breakpoint(this->pc)) {
                // First, get the original instruction
                // Since it seems gdb may remove it during handling notify_bp
                auto bp_i = gdb_stub->get_breakpoint_instruction(this->pc);
                
                gdb_stub->notify_breakpoint(this->cpu_id_, this->pc);
                
                // Get the original instruction and execute it:
                d->opcode = bp_i;
                this->excute_one(d);

                return;
            } else {
                std::cerr << "[CPU] Software breakpoint encountered, but gdb stub has no matching breakpoint.\n";
                // (was: verbose = true — compile with -DCPU_VERBOSE to trace trap handling)
            }
        }
    }

    int tn = trap_type;

    if (test_cc(d)) {
                if (d->i) 
            tn = 128 + ((d->rs1_value + sign_ext7((d->opcode & LOBITS7))) & LOBITS7);
        else {
            tn = d->value;
            tn = 128 + ((tn +  d->rs1_value) & LOBITS7);
        }
#ifdef CPU_VERBOSE
            os << std::format("{:#08x} t{} {:#08x} op={:#01x}\n", d->pc, cond_byte[d->rd & 0xf], tn, (d->rd & LOBITS4));
#endif
        trap(d, tn);
    } else {
#ifdef CPU_VERBOSE
            os << std::format("{:#08x} t{} {:#08x} op={:#01x}\n", d->pc, cond_byte[d->rd & 0xf], tn, (d->rd & LOBITS4));
#endif
        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::SAVE (pDecode_t d)
{
    u32 new_cwp = ((d->psr & LOBITS5) - 1) % NWINDOWS;
    //os << std::format("Save, new cwp {:#x}\n", new_cwp);
    
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} save     {:#08x}, {:#08x} (new CWP: {:#08x})\n", d->pc, d->rs1_value, d->ev, new_cwp);
#endif
    if (((wim >> new_cwp) & LOBITS1) != 0)
        trap (d, SPARC_WINDOW_OVERFLOW);
    else {
        d->psr = (d->psr & ~(LOBITS5)) | (new_cwp & LOBITS5);
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = d->ev;
        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::RESTORE (pDecode_t d)
{
    u32 new_cwp = ((d->psr & LOBITS5) + 1) % NWINDOWS;
    //os << std::format("Restore, new cwp {:#x}\n", new_cwp);
 
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} restore (new CWP: {:#08x})\n", d->pc, new_cwp);
#endif

    if (((wim >> new_cwp) & LOBITS1) != 0)
        trap (d, SPARC_WINDOW_UNDERFLOW);
    else {
        d->psr = (d->psr & ~(LOBITS5)) | (new_cwp & LOBITS5);
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = d->ev;
        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::FLUSH (pDecode_t d)
{
#ifdef CPU_VERBOSE
    {
        os << std::format("{:#08x} flush    [{:#08x}], {}\n", d->pc, d->ev, DispRegStr(d->rd));
    }
#endif
    d->pc = d->npc;
    d->npc += 4;
}

// ------------------------------------------------

void CPU::MULSCC (pDecode_t d)
{
    u32 cc;
    u32 x, y, z;
    u32 xtop, ytop, ztop;

    cc = (d->psr >> PSR_CC_CARRY) & LOBITS4;

    // FIX: https://github.com/wyvernSemi/sparc/pull/2/commits/edd362553bc2834114e8c216d7e6d1cbc3472393
    x = (d->rs1_value >> 1) | ((((cc >> CC_OVERFLOW) ^ (cc >> CC_NEGATIVE)) &1) << 31);
    y = (y_reg & 1) ? d->ev : 0;

    z = x + y;

    xtop = x >> 31;
    ytop = y >> 31;
    ztop = z >> 31;

    d->wb_type = WriteBackType::WRITEBACKREG;
    d->value = z;

    cc = (cc & ~(1 << CC_OVERFLOW)) | (((((xtop & ytop & ~(ztop)) | (~(xtop) & ~(ytop) & ztop)) & LOBITS1)) << CC_OVERFLOW);
    cc = (cc & ~(1 << CC_CARRY)) | (((( (xtop & ytop) | (~ztop & (xtop | ytop)))) & LOBITS1) << CC_CARRY);
    cc = (cc & ~(1 << CC_ZERO))     | (((z == 0) ? 1 : 0) << CC_ZERO);
    cc = (cc & ~(1 << CC_NEGATIVE)) | (((z >> 31) & LOBITS1) << CC_NEGATIVE);

    d->psr = (d->psr & ~(LOBITS4 << PSR_CC_CARRY)) | (cc << PSR_CC_CARRY);

    y_reg = (y_reg >> 1) | ((d->rs1_value & 1) << 31);

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} mulscc   {} {:#08x}, {} {:#08x} -> {} = {:#08x} cc={:#1x}\n",
                 d->pc,  
                 DispRegStr(d->rs1), 
                 x,
                 !d->i ? DispRegStr(d->opcode & LOBITS5) : "", 
                 y, 
                 DispRegStr(d->rd), 
                 z,
                 (d->psr >> PSR_CC_CARRY) & LOBITS4);
#endif

    d->pc = d->npc;
    d->npc += 4;
}

/////////////////////
// Format31 functions

// ------------------------------------------------

void CPU::LD (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} ld       [{:#08x}], %{}\n", d->pc, d->ev, DispRegStr(d->rd));
#endif

    if (d->ev & LOBITS2) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if((load32(d->ev, d->rd, 0, false) < 0) && !mmu.GetNoFault())
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        else {
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}

// ------------------------------------------------

void CPU::LDUB (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} ldub       [{:#08x}], %{}\n", d->pc, d->ev, DispRegStr(d->rd));
#endif

    if((load8(d->ev, d->rd, 0, false) < 0) && !mmu.GetNoFault())
        trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
    else {
        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::LDUH (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} lduh      [{:#08x}], %{}\n", d->pc, d->ev, DispRegStr(d->rd));
#endif

    if (d->ev & LOBITS1) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if((load16(d->ev, d->rd, 0, false) < 0) && !mmu.GetNoFault())
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        else {
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}

// ------------------------------------------------

void CPU::LDD (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} ldd      [{:#08x}], %{}\n", d->pc, d->ev, DispRegStr(d->rd));
#endif

    if (d->rd & LOBITS1) {
        trap(d, SPARC_ILLEGAL_INSTRUCTION);
    } else if (d->ev & LOBITS3) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if((load64(d->ev, d->rd & ~LOBITS1, 0, false) < 0) && !mmu.GetNoFault())
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        else {
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}

// ------------------------------------------------

void CPU::LDSB (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} ldsb     [{:#08x}], %{}\n", d->pc, d->ev, DispRegStr(d->rd));
#endif

    if((load8(d->ev, d->rd, 1, false) < 0) && !mmu.GetNoFault())
        trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
    else {
        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::LDSH (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} ldsh     [{:#08x}], %{}\n", d->pc, d->ev, DispRegStr(d->rd));
#endif

    if (d->ev & LOBITS1) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if((load16(d->ev, d->rd, 1, false) < 0) && !mmu.GetNoFault())
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        else {
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}

// ------------------------------------------------

void CPU::ST (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} st       {}, [{:#08x}] = {:#08x}\n", d->pc, DispRegStr(d->rd), d->ev, d->value & LOWORDMASK);
#endif

    if (d->ev & LOBITS2) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if((store32(d->ev, d->rd) < 0) && !mmu.GetNoFault()) {
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        } else {
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}

// ------------------------------------------------

void CPU::STB (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} stb      {}, [{:#08x}] = {:#08x}\n", d->pc, DispRegStr(d->rd), d->ev, d->value & LOBITS8);
#endif

    if((store8(d->ev, d->rd) < 0) && !mmu.GetNoFault())
        trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
    else {
        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::STH (pDecode_t d)
{

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} sth      {}, [{:#08x}] = {:#08x}\n", d->pc, DispRegStr(d->rd), d->ev, d->value & LOHWORDMASK);
#endif

    if (d->ev & LOBITS1) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if((store16(d->ev, d->rd) < 0) && !mmu.GetNoFault())
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        else {
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}

// ------------------------------------------------

void CPU::STD (pDecode_t d)
{
#ifdef CPU_VERBOSE
    {
        u64 hold0 = ((u64)d->value << (u64)32);
        hold0 |= d->value1;
        os << std::format("{:#08x} std      {}, [{:#08x}] = {:#x}\n", d->pc, DispRegStr(d->rd), d->ev, hold0);
    }
#endif
    if (d->rd & LOBITS1) {
        trap(d, SPARC_ILLEGAL_INSTRUCTION);
    } else if (d->ev & LOBITS3) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if(( store64(d->ev, d->rd) < 0) && !mmu.GetNoFault())
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        else {
            d->rd &= ~LOBITS1;
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}

// ------------------------------------------------

void CPU::SWAP (pDecode_t d)
{
    //u32 hold0;

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} swap     [{:#08x}], {} = {:#08x}\n", d->pc, d->ev, DispRegStr(d->rd), d->value );
#endif

    if (d->ev & LOBITS2) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
        return;
    }
    
    bool super = (d->p->s == 0x1U);
        
    // Get the physical address:
    u32 paddr = d->ev;

    // Copy data to swap register for the sake of good order
    // Allthough we dont use it
    *p_swap_reg = d->value;

    auto r = mmu.atomic_swap32(paddr, super, d->value);
    
    if(!r.ok && !mmu.GetNoFault()) {
        trap(d,  SPARC_DATA_ACCESS_EXCEPTION);
        return;
    }
    
    write_reg(r.old, d->rd);

    d->pc = d->npc;
    d->npc += 4;
    
}

// ------------------------------------------------

void CPU::SWAPA (pDecode_t d)
{
    u32 asi = (d->opcode >> 5) & 0xFFU; 
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} swapa    [{:#08x}], {} = {:#08x}, asi={}\n", d->pc, d->ev, DispRegStr(d->rd), d->value, asi );
#endif
    
    
    
    bool forced_cache_miss = false;
    if(asi == 1)
        forced_cache_miss = true;
    else {
        std::cout << "SWAPA error, unimplemeted ASI\n";
        os << std::format("{:#08x} swapa    [{:#08x}], {} = {:#08x}, asi={}\n", d->pc, d->ev, DispRegStr(d->rd), d->value, asi );
        UNIMP(d);
        return;
    }

    if (!d->p->s) {
        trap (d, SPARC_PRIVILEGED_INSTRUCTION);
        return;
    } else if (d->ev & LOBITS2) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
        return;
    }

    // TODO:
    // Force cache miss:
    if(forced_cache_miss) {
        // Flush/invalidate cache line for the address
    }

    u32 addr = d->ev;
    bool super = (d->p->s == 0x1U);
    u32 value = d->value;

    // Copy data to swap register for the sake of good order
    // Allthough we dont use it
    *p_swap_reg = value;
        
    auto r = mmu.atomic_swap32(addr, super, value);

    if(!r.ok && !mmu.GetNoFault()) {
        trap(d,  SPARC_DATA_ACCESS_EXCEPTION);
        return;
    }

    write_reg(r.old, d->rd);
    
    // Proceed
    d->pc = d->npc;
    d->npc += 4;
    
}

// ------------------------------------------------

void CPU::LDSTUB (pDecode_t d)
{
    //u32 value;

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} ldstub   [{:#08x}], {}\n", d->pc, d->ev, DispRegStr(d->rd));
#endif

    // Here we do manual translate, and we do not use MMU generic
    // access funcs for read and write. This is to retain atomicity
    
    bool super = (d->p->s == 0x1U);
    u32 paddr = 0x0U;

    if(mmu.GetEnabled()) {
        auto translate_res = mmu.translate_va(d->ev, super, intent_load, !mmu.GetNoFault());
        if(!translate_res.ok) {
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
            return;    
        }

        paddr = translate_res.pa;
    } else
        paddr = d->ev;


    // Do the ldstub
    auto ares = mmu.GetMCTRL().atomic_ldstub8(paddr);

    if(!ares.ok) {
        trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        return;
    }

    write_reg((ares.old & 0xff), d->rd);

    // For the sake of order, we set the swap reg (G8), even though we do
    // not use it in our transaction.
    *p_swap_reg = 0xff;
    
    d->pc = d->npc;
    d->npc += 4;

}

// ------------------------------------------------

void CPU::MUL (pDecode_t d)
{
    u64 x, y, z; 
    u32 cc;

    cc = (d->psr >> PSR_CC_CARRY) & LOBITS4;

    x = d->rs1_value;
    y = d->ev;

    if (d->op_2_3 & LOBITS1) {
        // Sign extend x and y
        x |= (x & 0x80000000) ? ((u64)0xffffffff << (u64)32) : 0;
        y |= (y & 0x80000000) ? ((u64)0xffffffff << (u64)32) : 0;

        // Calculate multiplication
        z = (s64)x * (s64)y;

        // Y = hi 32 bits
        y_reg = (u32)(z >> 32);

        // z = lo 32 bits
        z &= 0xffffffff;
    } else {
        z = x * y;

        y_reg = (u32)(z >> 32);
        z &= 0xffffffff;
    }

    // Modify icc bit set
    if (d->op_2_3 & BIT4) {
        cc = (cc & ~(1 << CC_ZERO))     | (((z == 0) ? 1 : 0) << CC_ZERO);
        cc = (cc & ~(1 << CC_NEGATIVE)) | (((u32)(z >> 31) & 1) << CC_NEGATIVE);
        cc &= ~(1 << CC_OVERFLOW);
        cc &= ~(1 << CC_CARRY);

        d->psr = (d->psr & ~(0xf << PSR_CC_CARRY)) | (cc << PSR_CC_CARRY);
    }

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} {} {} {:#08x}, {} {:#08x} -> {} = {:#08x} {:#08x} cc={:#1x}\n",
                 d->pc, 
                 op_byte[d->op_2_3], 
                 DispRegStr(d->rs1),
                 (u32)x,
                 !d->i ? DispRegStr(d->opcode & LOBITS5) : "",
                 (u32)y, 
                 DispRegStr(d->rd), 
                 y_reg, 
                 (u32)z,
                 (d->psr >> PSR_CC_CARRY) & LOBITS4);
#endif

    d->wb_type = WriteBackType::WRITEBACKREG;
    d->value = (u32)(z & 0xffffffff);

    d->pc = d->npc;
    d->npc += 4;
}

// ------------------------------------------------

void CPU::DIV (pDecode_t d)
{
    u64 x, y, z;
    u32 cc;

    cc = (d->psr >> PSR_CC_CARRY) & LOBITS4;

    x = ((u64)y_reg << 32) | (u64)d->rs1_value;
    y = d->ev;

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} {} {} {:#08x} {:#08x}, {} {:#08x} -> {} = ",
                 d->pc, op_byte[d->op_2_3], DispRegStr(d->rs1), 
                 (u32)(x>>32) & 0xffffffff, (u32)x & 0xffffffff,
                 !d->i ? DispRegStr(d->opcode & LOBITS5) : "", (u32)y & 0xffffffff, DispRegStr(d->rd));
#endif

    if (y == 0) {
#ifdef CPU_VERBOSE
            os << std::format("??? cc=??\n");
#endif
        trap (d, SPARC_DIVISION_BY_ZERO);
    } else {
        if (d->op_2_3 & LOBITS1) {
            y |= (y & 0x80000000) ? ((u64)0xffffffff << (u64)32) : 0;
            z = (s64)x / (s64)y;
            y_reg = (u32)((s64)x % (s64)y);
        } else {
            z = x / y;
            y_reg = (u32)(x % y);
        }

        // Modify icc bit set
        if (d->op_2_3 & BIT4) {
            cc = (cc & ~(1 << CC_ZERO))     | (((z == 0) ? 1 : 0) << CC_ZERO);
            cc = (cc & ~(1 << CC_NEGATIVE)) | (((u32)(z >> 31) & 1) << CC_NEGATIVE);
            cc &= ~(1 << CC_OVERFLOW);
            cc &= ~(1 << CC_CARRY);

            d->psr = (d->psr & ~(LOBITS4 << PSR_CC_CARRY)) | (cc << PSR_CC_CARRY);
        }

#ifdef CPU_VERBOSE
            os << std::format("{:#08x} cc={:#1x}\n", (u32)z & 0xffffffff,
                                 (d->psr >> PSR_CC_CARRY) & LOBITS4);
#endif

        d->wb_type = WRITEBACKREG;
        d->value = (u32)z &0xffffffff; 

        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::ADD (pDecode_t d)
{
    u32 x, y, z, cc;
    u32 xtop, ytop, ztop, carry;
    u32 y_sign; 
    u32 tag_inst, tag_overflow, tag_sub;
    // Opcode is a tagged add/sub
    tag_inst = d->op_2_3 & BIT5;
    tag_sub = (tag_inst && (d->op_2_3 & LOBITS1)) ? 1 : 0;
    cc = (d->psr >> PSR_CC_CARRY) & LOBITS4;

    x = d->rs1_value;
    y = d->ev;

    y_sign = ((d->op_2_3 & BIT2) || tag_sub) ? ~y : y;
    carry  = ((((d->op_2_3 >> 2) | tag_sub) ^ ((cc >> CC_CARRY) & (~tag_sub & (d->op_2_3 >> 3)))) & LOBITS1);
    xtop   = (x >> 31) & LOBITS1 ;
    ytop   = (y_sign >> 31) & LOBITS1;
 
    z = x + y_sign + carry ;

    ztop = (z >> 31) & LOBITS1 ;

    if (tag_inst) 
       tag_overflow = ((x & 0x3) || (y & 0x3)) ? 1 : 0;
    else 
       tag_overflow = 0;

    cc = (cc & ~(1 << CC_OVERFLOW)) | (((((xtop & ytop & ~(ztop)) | (~(xtop) & ~(ytop) & ztop)) & LOBITS1) | tag_overflow) << CC_OVERFLOW);
    cc = (cc & ~(1 << CC_CARRY))    | ((((d->op_2_3 >> 2) ^ ( (xtop & ytop) | (~ztop & (xtop | ytop)))) & LOBITS1) << CC_CARRY);
    cc = (cc & ~(1 << CC_ZERO))     | (((z == 0) ? 1 : 0) << CC_ZERO);
    cc = (cc & ~(1 << CC_NEGATIVE)) | (((z >> 31) & LOBITS1) << CC_NEGATIVE);

#ifdef CPU_VERBOSE
    {
        std::string tbyte;

        tbyte = (d->op_2_3 & 1) ? ((d->op_2_3 & 2) ? "tsubcctv" : "tsubcc  ") :
                                  ((d->op_2_3 & 2) ? "taddcctv" : "taddcc  ");
   
        os << std::format("{:#08x} {} {} {:#08x}, {} {:#08x} -> {} = {:#08x} cc={:#1x}\n",
                 d->pc, 
                 tag_inst ? tbyte : op_byte[d->op_2_3],
                 DispRegStr(d->rs1), 
                 x,
                 !d->i ? DispRegStr(d->opcode & LOBITS5) : "", 
                 y, 
                 DispRegStr(d->rd), 
                 z,
                 ((d->op_2_3 & BIT4) || tag_inst) ? cc : (d->psr >> PSR_CC_CARRY) & LOBITS4);
    }
#endif

    // TADDcTV or TSUBccTV trap on tagged overflow
    if (((d->op_2_3 & 0x22) == 0x22) && (cc & (1 << CC_OVERFLOW)))
        trap(d, SPARC_TAG_OVERFLOW);
    else {
        // Modify icc bit set (or TADDcc/TSUBcc)
        if ((d->op_2_3 & BIT4) || tag_inst) 
            d->psr = (d->psr & ~(LOBITS4 << PSR_CC_CARRY)) | (cc << PSR_CC_CARRY);
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = z;

        d->pc = d->npc;
        d->npc += 4;
    }
}

// ------------------------------------------------

void CPU::AND (pDecode_t d)
{
    u32 x, y, z, cc;
    u32 y_sign;

    cc = (d->psr >> PSR_CC_CARRY) & LOBITS4;

    x = d->rs1_value;
    y = d->ev;
    z = 0; // To avoid warning..
    y_sign = (d->op_2_3 & BIT2) ? ~y : y;

    switch (d->op_2_3 & LOBITS2) {
    case 1:
        z = x & y_sign ;
        break;
    case 2:
        z = x | y_sign;
        break;
    case 3:
        z = x ^ y_sign;
        break;
    }

    // Modify icc bit set
    if (d->op_2_3 & BIT4) 
        CalcCC(cc, z, d->psr);

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} {} {} {:#08x}, {} {:#08x} -> {} = {:#08x} cc={:#1x}\n",
                 d->pc, op_byte[d->op_2_3], DispRegStr(d->rs1), x,
                 !d->i ? DispRegStr(d->opcode & LOBITS5) : "", y, DispRegStr(d->rd), z,
                 (d->psr >> PSR_CC_CARRY) & LOBITS4);
#endif

    d->wb_type = WRITEBACKREG;
    d->value = z;

    d->pc = d->npc;
    d->npc += 4;
}



