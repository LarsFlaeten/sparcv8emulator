#include "CPU.h"


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
        if (verbose)
            os << std::format("{:#08x} unimp : Program exited normally\n", d->PC);
        rs.reason = TerminateReason::NORMAL;
    }

}

// ------------------------------------------------

void CPU::CALL (pDecode_t d) 
{
    u32 temp;

    if (verbose) 
        os << std::format("{:#08x} call     {:#08x} to {:#08x}\n", d->PC, d->imm_disp_rs2, d->PC + (4 * d->imm_disp_rs2));

    d->wb_type = WriteBackType::WRITEBACKREG;
    d->value = d->PC;
    d->rd = OUTREG7;

    temp   = d->PC;
    d->PC  = d->nPC;
    d->nPC = temp + (4 * d->imm_disp_rs2);
}

// ------------------------------------------------

void CPU::BICC (pDecode_t d)
{
    int branch, annul, temp;

    if (verbose) 
        os << std::format("{:#08x} b{} {:#d} cc={:#1x}\n", d->PC, CondByte[d->rd & 0xf], (signed int)(sign_ext22(d->imm_disp_rs2*4)), (d->PSR >> PSR_CC_CARRY) & LOBITS4);

    branch = TestCC (d);
    annul = ((d->rd >> 4) & LOBITS1) && (((!branch) && ((d->rd & LOBITS3) != 0)) || ((d->rd & LOBITS3) == 0));

    temp = d->nPC;
    if (branch)
        d->nPC = d->PC + 4 * sign_ext22(d->imm_disp_rs2);
    else 
        d->nPC += 4;
    d->PC = temp;

    if (annul) {
        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::SETHI (pDecode_t d)
{
    u32 temp;

    if (d->opcode == NOP) {
        if (verbose) 
            os << std::format("{:#08x} nop\n", d->PC);
    } else {
        temp = d->imm_disp_rs2 << 10;
        if (verbose) 
            os << std::format("{:#08x} sethi    Opcode {:#08x} -> {} = {:#08x}\n", d->PC, d->opcode, DispRegStr(d->rd), temp);
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = temp;
    }
    d->PC = d->nPC;
    d->nPC += 4;
}

// ------------------------------------------------

void CPU::SLL (pDecode_t d)
{
    d->wb_type = WriteBackType::WRITEBACKREG;
    d->value = d->rs1_value << (d->ev & LOBITS5);

    if (verbose) 
        os << std::format("{:#08x} sll       {} {:#08x} << {:#02x} = {:#08x} {}\n", d->PC, DispRegStr(d->rs1),
                 d->rs1_value, d->ev, d->value, DispRegStr(d->rd));

    d->PC = d->nPC;
    d->nPC += 4;
}

// ------------------------------------------------

void CPU::SRL (pDecode_t d)
{
    d->wb_type = WriteBackType::WRITEBACKREG;
    d->value = d->rs1_value >> (d->ev & LOBITS5);

    if (verbose) 
        os << std::format("{:#08x} srl       {} {:#08x} << {:#02x} = {:#08x} {}\n", d->PC, DispRegStr(d->rs1),
                 d->rs1_value, d->ev, d->value, DispRegStr(d->rd));

    d->PC = d->nPC;
    d->nPC += 4;
}

// ------------------------------------------------

void CPU::SRA (pDecode_t d)
{
    d->wb_type = WriteBackType::WRITEBACKREG;
    d->value = (d->rs1_value >> (d->ev & LOBITS5)) | (((d->rs1_value & BIT31) && d->ev) ? (0xffffffff << (32 - (d->ev & LOBITS5))) : 0);

    if (verbose) 
        os << std::format("{:#08x} sra       {} {:#08x} << {:#02x} = {:#08x} {}\n", d->PC, DispRegStr(d->rs1),
                 d->rs1_value, d->ev, d->value, DispRegStr(d->rd));

    d->PC = d->nPC;
    d->nPC += 4;
}

// ------------------------------------------------

void CPU::RDY (pDecode_t d)
{
    if (d->rs1 == 15 && d->rd == 0) {
        if (verbose) 
            os << std::format("{:#08x} stbar\n", d->PC);
    } else {
        if (d->rs1 == 0 && (d->op_2_3 & 0x2f) == 0x28) {
            if (verbose) 
                os << std::format("{:#08x} rdy      {} = {:#08x}\n", d->PC, DispRegStr(d->rd), Y);
            d->wb_type = WriteBackType::WRITEBACKREG;
            d->value = Y;
        } else if(d->rs1 == 17 && (d->op_2_3 & 0x2f) == 0x28) {
            // Leon specific, read CPU id
            if (verbose) 
                os << std::format("{:#08x} rd %asr17      {} = {:#08x}\n", d->PC, DispRegStr(d->rd), this->cpu_id);
            d->wb_type = WriteBackType::WRITEBACKREG;
            d->value = (this->cpu_id << 28) 
                | (0x1 << 26)   // NOTAG / CASA
                | (0b11 << 10)  // GRFP liteU
                | (0x1 << 8)    // SparcV8 MUL/DIV
                | ((NWINDOWS-1) & 0x1f) ;
        }
        else 
           UNIMP(d);
    }
    d->PC = d->nPC;
    d->nPC += 4;
}

// ------------------------------------------------

void CPU::RDPSR (pDecode_t d)
{
    if (verbose) 
        os << std::format("{:#08x} rdpsr    {} = {:#08x}\n", d->PC, DispRegStr(d->rd), d->PSR);

    if (d->p->s == 0) {
        Trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    } else {
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = d->PSR;
        d->PC = d->nPC;
        d->nPC= d->nPC + 4;
    }
}

// ------------------------------------------------

void CPU::RDWIM (pDecode_t d)
{
    if (verbose) 
        os << std::format("{:#08x} rdwim    {} = {:#08x}\n", d->PC, DispRegStr(d->rd), WIM);

    if (d->p->s == 0) {
        Trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    } else {
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = WIM;
        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::RDTBR (pDecode_t d)
{

    if (verbose) 
       os << std::format("{:#08x} rdtbr    {} = {:#08x}\n", d->PC, DispRegStr(d->rd), TBR);

    if (d->p->s == 0) {
        Trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    } else {
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = TBR;
        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::WRY (pDecode_t d)
{
    if ((d->rd & LOBITS5) == 0) {
        Y = d->rs1_value ^ d->ev;
        if (verbose) 
            os << std::format("{:#08x} wry      = {:#08x}\n", d->PC, Y);
    } else 
        UNIMP(d);

    d->PC = d->nPC;
    d->nPC += 4;
}

// ------------------------------------------------

void CPU::WRPSR (pDecode_t d)
{
    if (verbose) 
        os << std::format("{:#08x} wrpsr    = {:#08x}\n", d->PC, d->rs1_value ^ d->ev);

    if (((d->rs1_value ^ d->ev) & LOBITS5) >= NWINDOWS) { 
       //os << std::format("Ooops, rs1:{:#08x}, ev:{:#08x}, rs1^ev[4:0]:{:#08x}, NWIN:{:#08x}\n", d->rs1_value, d->ev, (d->rs1_value^d->ev)&LOBITS5, NWINDOWS);

       Trap (d, SPARC_ILLEGAL_INSTRUCTION);
    }
    else if (d->p->s == 0) 
        Trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    else {
        // Hard wire LEON specific values..
        d->PSR = (0xf << 28) | (0x3 << 24) | (d->rs1_value ^ d->ev);
        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::WRWIM (pDecode_t d)
{
    if (verbose) 
       os << std::format("{:#08x} wrwim    = {:#08x}\n", d->PC, d->rs1_value ^ d->ev);

    if (d->p->s == 0)
        Trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    else {
        // Only allow WIM to be set up to number of windows
        // Reference page 30
        WIM = (d->rs1_value ^ d->ev) & ((0x1 << (NWINDOWS))-1);
        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::WRTBR (pDecode_t d)
{
    if (verbose) 
        os << std::format("{:#08x} wrtbr    = {:#08x}\n", d->PC, d->rs1_value ^ d->ev);

    if (d->p->s == 0) 
        Trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    else {
        TBR = d->rs1_value ^ d->ev;
        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::JMPL (pDecode_t d)
{
    u32 temp;


    if (verbose) 
        os << std::format("{:#08x} jmpl     rs1 {:#08x} + ev => nPC = {:#08x}\n", d->PC, d->rs1_value, d->nPC);

    if (d->ev & LOBITS2) {
        Trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = d->PC;
        temp = d->PC;
        d->PC = d->nPC;
        d->nPC = d->ev;
    }
}

// ------------------------------------------------

void CPU::RETT (pDecode_t d)
{
    u32 temp, new_cwp;

    new_cwp = (d->p->cwp + 1) % NWINDOWS;
    //os << std::format("RETT, new cwp {:#x}\n", new_cwp);
 
    if (verbose) 
        os << std::format("{:#08x} rett     rs1 {:#08x} + ev => nPC = {:#08x} (new CWP = {:#08x})\n", d->PC, d->rs1_value, d->ev & ~LOBITS2, new_cwp);

    if ( (d->p->et && !d->p->s) || (!d->p->et && !d->p->s))
        Trap (d, SPARC_PRIVILEGED_INSTRUCTION);
    else if (d->p->et && d->p->s)
        Trap (d, SPARC_ILLEGAL_INSTRUCTION);
    else if (d->ev & LOBITS2)
        Trap (d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    else if (((WIM >> new_cwp) & LOBITS1) != 0)
        Trap (d, SPARC_WINDOW_UNDERFLOW);
    else {
        temp = d->PC;
        d->PC = d->nPC;
        d->nPC = d->ev & ~LOBITS2;
        d->p->s = d->p->ps;
        d->p->et = 1;
        d->p->cwp = new_cwp & LOBITS5;
    }

}

// ------------------------------------------------

void CPU::TICC (pDecode_t d)
{
    int tn = TrapType;

    if (TestCC(d)) {
        if (d->i) 
            tn = 128 + ((d->rs1_value + sign_ext7((d->opcode & LOBITS7))) & LOBITS7);
        else {
            tn = d->value;
            tn = 128 + ((tn +  d->rs1_value) & LOBITS7);
        }
        if (verbose) 
            os << std::format("{:#08x} t{} {:#08x} op={:#01x}\n", d->PC, CondByte[d->rd & 0xf], tn, (d->rd & LOBITS4));
        Trap(d, tn);
    } else {
        if (verbose) 
            os << std::format("{:#08x} t{} {:#08x} op={:#01x}\n", d->PC, CondByte[d->rd & 0xf], tn, (d->rd & LOBITS4));
        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::SAVE (pDecode_t d)
{
    u32 new_cwp = ((d->PSR & LOBITS5) - 1) % NWINDOWS;
    //os << std::format("Save, new cwp {:#x}\n", new_cwp);
    
    if (verbose) 
        os << std::format("{:#08x} save     {:#08x}, {:#08x} (new CWP: {:#08x})\n", d->PC, d->rs1_value, d->ev, new_cwp);
    if (((WIM >> new_cwp) & LOBITS1) != 0)
        Trap (d, SPARC_WINDOW_OVERFLOW);
    else {
        d->PSR = (d->PSR & ~(LOBITS5)) | (new_cwp & LOBITS5);
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = d->ev;
        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::RESTORE (pDecode_t d)
{
    u32 new_cwp = ((d->PSR & LOBITS5) + 1) % NWINDOWS;
    //os << std::format("Restore, new cwp {:#x}\n", new_cwp);
 
    if (verbose) 
        os << std::format("{:#08x} restore (new CWP: {:#08x})\n", d->PC, new_cwp);

    if (((WIM >> new_cwp) & LOBITS1) != 0)
        Trap (d, SPARC_WINDOW_UNDERFLOW);
    else {
        d->PSR = (d->PSR & ~(LOBITS5)) | (new_cwp & LOBITS5);
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = d->ev;
        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::FLUSH (pDecode_t d)
{
    if (verbose) {
        os << std::format("{:#08x} flush    [{:#08x}], {}\n", d->PC, d->ev, DispRegStr(d->rd));
    }
    d->PC = d->nPC;
    d->nPC += 4;
}

// ------------------------------------------------

void CPU::MULSCC (pDecode_t d)
{
    u32 cc;
    u32 x, y, z;
    u32 xtop, ytop, ztop;

    cc = (d->PSR >> PSR_CC_CARRY) & LOBITS4;


    x = d->ev;
    y = (Y & 1) ? (d->rs1_value >> 1) | ((((cc >> CC_OVERFLOW) ^ (cc >> CC_NEGATIVE)) &1) << 31) : 0;

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

    d->PSR = (d->PSR & ~(LOBITS4 << PSR_CC_CARRY)) | (cc << PSR_CC_CARRY);

    Y = (Y >> 1) | ((d->rs1_value & 1) << 31);

    if (verbose) 
        os << std::format("{:#08x} mulscc   {} {:#08x}, {} {:#08x} -> {} = {:#08x} cc={:#1x}\n",
                 d->PC,  
                 DispRegStr(d->rs1), 
                 x,
                 !d->i ? DispRegStr(d->opcode & LOBITS5) : "", 
                 y, 
                 DispRegStr(d->rd), 
                 z,
                 (d->PSR >> PSR_CC_CARRY) & LOBITS4);

    d->PC = d->nPC;
    d->nPC += 4;
}

/////////////////////
// Format31 functions

// ------------------------------------------------

void CPU::LD (pDecode_t d)
{
    if (verbose) 
        os << std::format("{:#08x} ld       [{:#08x}], %{}\n", d->PC, d->ev, DispRegStr(d->rd));

    if (d->ev & LOBITS2) {
        Trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if(MemRead(d->ev, 4, d->rd, 0) < 0)
            handleMMUFault(d);
        else {
            d->PC = d->nPC;
            d->nPC += 4;
        }
    }
}

// ------------------------------------------------

void CPU::LDUB (pDecode_t d)
{
    if (verbose) 
       os << std::format("{:#08x} ldub       [{:#08x}], %{}\n", d->PC, d->ev, DispRegStr(d->rd));

    if(MemRead(d->ev, 1, d->rd, 0) < 0)
        handleMMUFault(d);
    else {
        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::LDUH (pDecode_t d)
{
    if (verbose) 
       os << std::format("{:#08x} lduh      [{:#08x}], %{}\n", d->PC, d->ev, DispRegStr(d->rd));

    if (d->ev & LOBITS1) {
        Trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if(MemRead(d->ev, 2, d->rd, 0) < 0)
            handleMMUFault(d);
        else {
            d->PC = d->nPC;
            d->nPC += 4;
        }
    }
}

// ------------------------------------------------

void CPU::LDD (pDecode_t d)
{
    if (verbose) 
       os << std::format("{:#08x} ldd      [{:#08x}], %{}\n", d->PC, d->ev, DispRegStr(d->rd));

    if (d->rd & LOBITS1) {
        Trap(d, SPARC_ILLEGAL_INSTRUCTION);
    } else if (d->ev & LOBITS3) {
        Trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if(MemRead(d->ev, 8, d->rd & ~LOBITS1, 0) < 0)
            handleMMUFault(d);
        else {
            d->PC = d->nPC;
            d->nPC += 4;
        }
    }
}

// ------------------------------------------------

void CPU::LDSB (pDecode_t d)
{
    if (verbose) 
       os << std::format("{:#08x} ldsb     [{:#08x}], %{}\n", d->PC, d->ev, DispRegStr(d->rd));

    if(MemRead(d->ev, 1, d->rd, 1) < 0)
        handleMMUFault(d);
    else {
        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::LDSH (pDecode_t d)
{
    if (verbose) 
        os << std::format("{:#08x} ldsh     [{:#08x}], %{}\n", d->PC, d->ev, DispRegStr(d->rd));

    if (d->ev & LOBITS1) {
        Trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if(MemRead(d->ev, 2, d->rd, 1) < 0)
            handleMMUFault(d);
        else {
            d->PC = d->nPC;
            d->nPC += 4;
        }
    }
}

// ------------------------------------------------

void CPU::ST (pDecode_t d)
{
    if (verbose) 
        os << std::format("{:#08x} st       {}, [{:#08x}] = {:#08x}\n", d->PC, DispRegStr(d->rd), d->ev, d->value & LOWORDMASK);

    if (d->ev & LOBITS2) {
        Trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if(MemWrite(d->ev, 4, d->rd) < 0)
            handleMMUFault(d);
        else {
            d->PC = d->nPC;
            d->nPC += 4;
        }
    }
}

// ------------------------------------------------

void CPU::STB (pDecode_t d)
{
    if (verbose) 
        os << std::format("{:#08x} stb      {}, [{:#08x}] = {:#08x}\n", d->PC, DispRegStr(d->rd), d->ev, d->value & LOBITS8);

    if(MemWrite(d->ev, 1, d->rd) < 0)
        handleMMUFault(d);
    else {
        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::STH (pDecode_t d)
{

    if (verbose) 
        os << std::format("{:#08x} sth      {}, [{:#08x}] = {:#08x}\n", d->PC, DispRegStr(d->rd), d->ev, d->value & LOHWORDMASK);

    if (d->ev & LOBITS1) {
        Trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if(MemWrite(d->ev, 2, d->rd) < 0)
            handleMMUFault(d);
        else {
            d->PC = d->nPC;
            d->nPC += 4;
        }
    }
}

// ------------------------------------------------

void CPU::STD (pDecode_t d)
{
    u64 hold0;

    if (verbose) {
        hold0 = ((u64)d->value << (u64)32);
        hold0 |= d->value1;
        os << std::format("{:#08x} std      {}, [{:#08x}] = {:#x}\n", d->PC, DispRegStr(d->rd), d->ev, hold0);
    }
    if (d->rd & LOBITS1) {
        Trap(d, SPARC_ILLEGAL_INSTRUCTION);
    } else if (d->ev & LOBITS3) {
        Trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        if( MemWrite(d->ev, 8, d->rd) < 0)
            handleMMUFault(d);
        else {
            d->rd &= ~LOBITS1;
            d->PC = d->nPC;
            d->nPC += 4;
        }
    }
}

// ------------------------------------------------

void CPU::SWAP (pDecode_t d)
{
    //u32 hold0;

    if (verbose)
        os << std::format("{:#08x} swap     [{:#08x}], {} = {:#08x}\n", d->PC, d->ev, DispRegStr(d->rd), d->value );

    if (d->ev & LOBITS2)
        Trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    else {
        // Copy data to swap register
        *pSwapReg = d->value;

        // Issue the read
        if( MemRead(d->ev, 4, d->rd, 0) < 0)
            handleMMUFault(d);
        else {   
            // Write the data back
            if(MemWrite(d->ev, 4, GLOBALREG8) < 0)
                handleMMUFault(d);
            else { 
                d->PC = d->nPC;
                d->nPC += 4;
            }
        }
    }
}

// ------------------------------------------------

void CPU::LDSTUB (pDecode_t d)
{
    //u32 value;

    if (verbose) 
        os << std::format("{:#08x} ldstub   [{:#08x}], {}\n", d->PC, d->ev, DispRegStr(d->rd));

    if( MemRead(d->ev, 1, d->rd, 0) < 0)
    {
        handleMMUFault(d);
        return;
    }

    // Set memory byte to all 1s
    *pSwapReg = 0xff;

    if(MemWrite(d->ev, 1, GLOBALREG8) < 0)
        handleMMUFault(d);
    else { 
        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::MUL (pDecode_t d)
{
    u64 x, y, z; 
    u32 cc;

    cc = (d->PSR >> PSR_CC_CARRY) & LOBITS4;

    x = d->rs1_value;
    y = d->ev;

    if (d->op_2_3 & LOBITS1) {
        // Sign extend x and y
        x |= (x & 0x80000000) ? ((u64)0xffffffff << (u64)32) : 0;
        y |= (y & 0x80000000) ? ((u64)0xffffffff << (u64)32) : 0;

        // Calculate multiplication
        z = (s64)x * (s64)y;

        // Y = hi 32 bits
        Y = (u32)(z >> 32);

        // z = lo 32 bits
        z &= 0xffffffff;
    } else {
        z = x * y;

        Y = (u32)(z >> 32);
        z &= 0xffffffff;
    }

    // Modify icc bit set
    if (d->op_2_3 & BIT4) {
        cc = (cc & ~(1 << CC_ZERO))     | (((z == 0) ? 1 : 0) << CC_ZERO);
        cc = (cc & ~(1 << CC_NEGATIVE)) | (((u32)(z >> 31) & 1) << CC_NEGATIVE);
        cc &= ~(1 << CC_OVERFLOW);
        cc &= ~(1 << CC_CARRY);

        d->PSR = (d->PSR & ~(0xf << PSR_CC_CARRY)) | (cc << PSR_CC_CARRY);
    }

    if (verbose)
        os << std::format("{:#08x} {} {} {:#08x}, {} {:#08x} -> {} = {:#08x} {:#08x} cc={:#1x}\n",
                 d->PC, 
                 OpByte[d->op_2_3], 
                 DispRegStr(d->rs1),
                 (u32)x,
                 !d->i ? DispRegStr(d->opcode & LOBITS5) : "",
                 (u32)y, 
                 DispRegStr(d->rd), 
                 Y, 
                 (u32)z,
                 (d->PSR >> PSR_CC_CARRY) & LOBITS4);

    d->wb_type = WriteBackType::WRITEBACKREG;
    d->value = (u32)(z & 0xffffffff);

    d->PC = d->nPC;
    d->nPC += 4;
}

// ------------------------------------------------

void CPU::DIV (pDecode_t d)
{
    u64 x, y, z;
    u32 cc;

    cc = (d->PSR >> PSR_CC_CARRY) & LOBITS4;

    x = ((u64)Y << 32) | (u64)d->rs1_value;
    y = d->ev;

    if (verbose)
        os << std::format("{:#08x} {} {} {:#08x} {:#08x}, {} {:#08x} -> {} = ",
                 d->PC, OpByte[d->op_2_3], DispRegStr(d->rs1), 
                 (u32)(x>>32) & 0xffffffff, (u32)x & 0xffffffff,
                 !d->i ? DispRegStr(d->opcode & LOBITS5) : "", (u32)y & 0xffffffff, DispRegStr(d->rd));

    if (y == 0) {
        if (verbose)
            os << std::format("??? cc=??\n");
        Trap (d, SPARC_DIVISION_BY_ZERO);
    } else {
        if (d->op_2_3 & LOBITS1) {
            y |= (y & 0x80000000) ? ((u64)0xffffffff << (u64)32) : 0;
            z = (s64)x / (s64)y;
            Y = (u32)((s64)x % (s64)y);
        } else {
            z = x / y;
            Y = (u32)(x % y);
        }

        // Modify icc bit set
        if (d->op_2_3 & BIT4) {
            cc = (cc & ~(1 << CC_ZERO))     | (((z == 0) ? 1 : 0) << CC_ZERO);
            cc = (cc & ~(1 << CC_NEGATIVE)) | (((u32)(z >> 31) & 1) << CC_NEGATIVE);
            cc &= ~(1 << CC_OVERFLOW);
            cc &= ~(1 << CC_CARRY);

            d->PSR = (d->PSR & ~(LOBITS4 << PSR_CC_CARRY)) | (cc << PSR_CC_CARRY);
        }

        if (verbose)
            os << std::format("{:#08x} cc={:#1x}\n", (u32)z & 0xffffffff,
                                 (d->PSR >> PSR_CC_CARRY) & LOBITS4);

        d->wb_type = WRITEBACKREG;
        d->value = (u32)z &0xffffffff; 

        d->PC = d->nPC;
        d->nPC += 4;
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
    cc = (d->PSR >> PSR_CC_CARRY) & LOBITS4;

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

    if (verbose) {
        std::string tbyte;

        tbyte = (d->op_2_3 & 1) ? ((d->op_2_3 & 2) ? "tsubcctv" : "tsubcc  ") :
                                  ((d->op_2_3 & 2) ? "taddcctv" : "taddcc  ");
   
        os << std::format("{:#08x} {} {} {:#08x}, {} {:#08x} -> {} = {:#08x} cc={:#1x}\n",
                 d->PC, 
                 tag_inst ? tbyte : OpByte[d->op_2_3],
                 DispRegStr(d->rs1), 
                 x,
                 !d->i ? DispRegStr(d->opcode & LOBITS5) : "", 
                 y, 
                 DispRegStr(d->rd), 
                 z,
                 ((d->op_2_3 & BIT4) || tag_inst) ? cc : (d->PSR >> PSR_CC_CARRY) & LOBITS4);
    }

    // TADDcTV or TSUBccTV trap on tagged overflow
    if (((d->op_2_3 & 0x22) == 0x22) && (cc & (1 << CC_OVERFLOW)))
        Trap(d, SPARC_TAG_OVERFLOW);
    else {
        // Modify icc bit set (or TADDcc/TSUBcc)
        if ((d->op_2_3 & BIT4) || tag_inst) 
            d->PSR = (d->PSR & ~(LOBITS4 << PSR_CC_CARRY)) | (cc << PSR_CC_CARRY);
        d->wb_type = WriteBackType::WRITEBACKREG;
        d->value = z;

        d->PC = d->nPC;
        d->nPC += 4;
    }
}

// ------------------------------------------------

void CPU::AND (pDecode_t d)
{
    u32 x, y, z, cc;
    u32 y_sign;

    cc = (d->PSR >> PSR_CC_CARRY) & LOBITS4;

    x = d->rs1_value;
    y = d->ev;

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
        CalcCC(cc, z, d->PSR);

    if (verbose)
        os << std::format("{:#08x} {} {} {:#08x}, {} {:#08x} -> {} = {:#08x} cc={:#1x}\n",
                 d->PC, OpByte[d->op_2_3], DispRegStr(d->rs1), x,
                 !d->i ? DispRegStr(d->opcode & LOBITS5) : "", y, DispRegStr(d->rd), z,
                 (d->PSR >> PSR_CC_CARRY) & LOBITS4);

    d->wb_type = WRITEBACKREG;
    d->value = z;

    d->PC = d->nPC;
    d->nPC += 4;
}



