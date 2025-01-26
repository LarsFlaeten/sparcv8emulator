#include "CPU.h"
#include <sstream>
#include <iomanip>
#include "MMU.h"
//------------------------------------------------------------------------
// Reset()
//
// Reset major state. Called when run from time 0.
//
void CPU::Reset(u32 entry_va = 0x0)
{
    rs.reason = TerminateReason::NORMAL;
    rs.instr_count = 0;
    rs.last_opcode = 0;

    if(verbose)
        os << "Resetting device, entry PC = 0x" << std::hex << entry_va << "\n";
    
    PC  = entry_va;
    nPC = PC + 4;;

    //PSR = (1 << PSR_ENABLE_TRAPS) | (1 << PSR_SUPER_MODE);
    PSR = 0;
    ((pPSR_t)&PSR)->et = 1;
    ((pPSR_t)&PSR)->s  = 1;
    ((pPSR_t)&PSR)->ps  = 1;
    ((pPSR_t)&PSR)->ef = 1;

    // Leon3 specific values:
    ((pPSR_t)&PSR)->imp = 0xf; // Gaisler
    ((pPSR_t)&PSR)->ver = 0x3; // Leon3


 

    IRL = 0;

    TrapType = 0;
    TBR = 0;
    WIM = 2;
}

void CPU::IFetch(u32 virt_addr, u32& opcode)
{
    MMU::MemAccess<intent_execute, 4>(virt_addr, opcode, CROSS_ENDIAN);
}

u32  CPU::Run(u32 ExecCount, RunSummary* _rs) {
    rs.reason = TerminateReason::INSTRUCTION;
    
    u64 count = 0;
    //u32 word_count;
    struct DecodeStruct Dec, *d=&Dec;
    pPSR_t p = (pPSR_t)&PSR;

    // Map the PSR structure to the decode PSR variable just once
    d->p = (pPSR_t) &(d->PSR);

    // Start executing program
    running = true;
    while ((!(rs.reason) /*!= TerminateReason::INSTRUCTION*/) && (ExecCount == 0) ? 1 : (count < (u64)ExecCount)) {

        count++;
        
        // Process interrupts
        if (TrapType == 0 && (p->et && IRL > p->pil)) {
            if (verbose)
                os << std::format("INT  {:#x} PC={:#08x} NPC={:#08x}\n", IRL, PC, nPC);

            TrapType = IRL + SPARC_INTERRUPT;

            // TODO: Do we clear IRL here, or handle that in interrupt handler?
        }

        // Process Traps
        if (TrapType) {
            p->et = 0;
            p->ps = p->s;
            p->s = 1;
            p->cwp -= 1;
            WriteReg (PC,  LOCALREG1);
            WriteReg (nPC, LOCALREG2);
            
            TBR = (TBR & ~(0xff0)) | ((TrapType & LOBITS8) << 4);
            PC  = TBR;
            nPC = TBR + 4;
            TrapType &= ~(LOBITS8);
        }

        u32 virt_addr = (u32) PC;

        // Service breakpoints if breakpoint_func is added and virt_addr is in breakpoints list,
        // or we are in single step mode
        //if(breakpoint_func && (breakpoints.contains(virt_addr) || single_step)) {
            // Check if breakpoint enabled
        //    if(breakpoints[virt_addr] || single_step) {
        //        breakpoint_func();
        //    }
        //}
        if(single_step) {
            rs.reason = TerminateReason::STEP;
            break;
        }

        if(breakpoints.contains(virt_addr)) {
            // Check if breakpoint enabled
            rs.reason = TerminateReason::BREAK;
            break;
        }
        

       	// Process instruction ...

        // ---- IFetch ----
        IFetch(virt_addr, d->opcode);
        
        // ---- Decode ----
        Decode (d);

        // ---- Execute ----
        d->function(this, d);

        // ---- Writeback ----
        WriteBack(d);

        // Tick the bus, handling input, interrupts etc
        // ..and gdb server if present
		if(bus_tick_func)
            bus_tick_func();
    }

    rs.instr_count = count;
    rs.last_opcode = d->opcode; 

    // Return summary
    if(_rs != nullptr)
        *_rs = rs;

    // return ocunt
    return count;
}

//------------------------------------------------------------------------
// Decode()
//
// Main instruction decode. Selects instc function and expands input data
//
void CPU::Decode(pDecode_t d)
{
    u32 fmt_bits = (d->opcode >> FMTSTARTBIT) & LOBITS2;
    u32 op2      = (d->opcode >> OP2STARTBIT) & LOBITS3;
    u32 op3      = (d->opcode >> OP3STARTBIT) & LOBITS6;
    u32 I_idx, regvalue;

    if (verbose) 
        os << std::format("{:#08x}: ", d->opcode);

    // By default, no change to program counter and PSR, and
    // no writeback
    d->PC      = PC;
    d->nPC     = nPC;
    d->PSR     = PSR;
    d->wb_type = WriteBackType::NO_WRITEBACK;

    switch (fmt_bits) {
    // CALL
    case 1:  
        d->function     = format1;                         
        d->imm_disp_rs2 = d->opcode & LOBITS30;
        break;

    // SETHI, Branches
    case 0:  
        d->function     = format2[op2];                     
        d->rd           = (d->opcode >> RDSTARTBIT) & LOBITS5;
        d->op_2_3       = (d->opcode >> OP2STARTBIT) & LOBITS3;
        d->imm_disp_rs2 = (d->opcode & LOBITS22);
        break;

    // Memory accesses, ALU etc.
    case 3:  
    case 2:  
        d->function     = format3[op3 + ((fmt_bits & 1) << 6)];
        d->rd           = (d->opcode >> RDSTARTBIT)  & LOBITS5;
        d->op_2_3       = (d->opcode >> OP3STARTBIT) & LOBITS6;
        d->rs1          = (d->opcode >> RS1STARTBIT) & LOBITS5;
        d->i            = (d->opcode >> ISTARTBIT)   & LOBITS1;
        d->imm_disp_rs2 = (d->opcode >> RS2STARTBIT) & LOBITS13;

        // All instructions need r[rs1] value
        // op = 11 and op3 = 100xxx means load or store from fregs,
        // handle that in function
        if((fmt_bits == 3) && ( (d->op_2_3 >> 3 ) == 4) ) {
            break;
        } else if ((fmt_bits == 2) && ( (d->op_2_3 == 52) || (d->op_2_3 == 53)) ) {
            break; // Skip read regs for FOP1 and FOP2
        } else
        {        
            ReadReg (d->rs1, &d->rs1_value);
        }

        // ev = ((RD/WR/ALU/Logic instr) ? r[rs1] : 0) + (i ? sign_ext(imm) : r[rs2])
        I_idx = op3 + ((fmt_bits & 1) << 6);
        regvalue = (I_idx < FIRST_RS1_EVAL_IDX) ? 0 : d->rs1_value;

        if (d->i)
           d->ev = regvalue + sign_ext13(d->imm_disp_rs2);
        else {
            
            ReadReg(d->imm_disp_rs2 & LOBITS5, &d->ev);
            d->ev += regvalue;
        }

        // All format 3 instructions require at least one register read (rs1),
        // but a few require more...

        // A store double access requires two register reads
        // from, r[rd], r[rd+1].
        if (I_idx == STORE_DBL_IDX) {
            ReadReg (d->rd & ~LOBITS1, &d->value);
            ReadReg ((d->rd & ~LOBITS1)+1, &d->value1);

        // Ticc requires an r[rs2] read if i bit clear
        } else if (I_idx == TICC_IDX && !d->i)
            ReadReg (d->imm_disp_rs2 & LOBITS5, &d->value); 

        // Memory instructions need to read r[rd]
        else if (fmt_bits & 1)
            ReadReg (d->rd, &d->value);

        break;
   }
}

//------------------------------------------------------------------------
// WriteBack()
//
// Write back register values
//
void CPU::WriteBack (const pDecode_t d)
{
    PC  = d->PC;
    nPC = d->nPC;
    PSR = d->PSR;

    if (d->wb_type == WriteBackType::WRITEBACKREG) 
        WriteReg(d->value, d->rd);
}


// ------------------------------------------------

void CPU::Trap (pDecode_t d, u32 trap_no) 
{
    int tn = trap_no & LOBITS8;
    u32 npc = (TBR & ~(0xff0)) | ((tn & LOBITS8) << 4);;

    if (d->p->et == 0) {
         fprintf (stderr, "ERROR TRAP %x WHILE TRAPS DISABLED\n", trap_no);
         RegisterDump();
         //terminate = d->opcode;
         rs.last_opcode = d->opcode;
         rs.reason = TerminateReason::TRAP_CONDITIONAL;
         return;
    }

    if (verbose) {
        if (tn < 0x40)
            os << std::format("                   TRAP {} ({:#x}) PC={:#08x} NPC={:#08x}\n", TrapStr[tn], tn, d->PC, npc);
        else if (tn >= 0x40 && tn < 0x60)
            os << std::format("                   TRAP NO TRAP ({:#x}) PC={:#08x} NPC={:#08x}\n", tn, d->PC, npc);
        else if (tn >= 0x60 && tn < 0x80)
            os << std::format("                   TRAP Impl. Dep. ({:#x}) PC={:#08x} NPC={:#08x}\n", tn, d->PC, npc);
        else 
            os << std::format("                   TRAP Ticc ({:#x}) PC={:#08x} NPC={:#08x}\n", tn, d->PC, npc);
    }

    TrapType = (TrapType & ~(LOBITS8)) | tn;
}

// ------------------------------------------------
// DispRegStr()
//
// Returns a formatted string representing the register indexed by regnum.
// USed in verbose output only.
//
std::string CPU::DispRegStr (const u32 regnum) 
{
    std::ostringstream oss;

    switch ((regnum >> 3) & LOBITS2) {
    case 0: 
        oss << std::format("%g{:#0d}", regnum & LOBITS3);
        break;
    case 1: 
        oss << std::format("%o{:#0d}", regnum & LOBITS3);
        break;
    case 2: 
        oss << std::format("%l{:#0d}", regnum & LOBITS3);
        break;
    case 3: 
        oss << std::format("%i{:#0d}", regnum & LOBITS3);
    }

    return oss.str();
}


int CPU::TestCC (pDecode_t d) {

    int cond; 

    cond = d->rd & LOBITS4;

    switch (cond & LOBITS3) {
    case 0 :
        return (cond >> 3) & LOBITS1;
        break;
    case 1 :
        return ((cond >> 3) ^ d->p->z);
        break;
    case 2 :
        return ((cond >> 3) ^ (d->p->z | (d->p->n ^ d->p->v)));
        break;
    case 3 :
        return ((cond >> 3) ^ (d->p->n ^ d->p->v));
        break;
    case 4 :
        return ((cond >> 3) ^ (d->p->c | d->p->z));
        break;
    case 5 :
        return ((cond >> 3) ^ d->p->c);
        break;
    case 6 :
        return ((cond >> 3) ^ d->p->n);
        break;
    case 7 :
        return ((cond >> 3) ^ d->p->v);
        break;
    default:
        std::cerr << "*** TestCC(): fatal error\n";
        throw std::runtime_error("*** TestCC(): fatal error");
    }

    return 0;
}


//------------------------------------------------------------------------
//
void CPU::ReadReg (const u32 reg_no, u32 * const value) 
{
    int win;

    Globals[0] = 0;
    switch (reg_no >> 3) {
    case 0 : // Globals
        *value = Globals[reg_no & LOBITS3];
        break;
    case 1 : // Outs
        *value = Outs [((GetPSR() & LOBITS4) << 3) | (reg_no & LOBITS3)];
        break;
    case 2 : // locals
        *value = Locals [((GetPSR() & LOBITS4) << 3) | (reg_no & LOBITS3)];
        break;
    case 3 : // Ins
        win = ((GetPSR() & LOBITS4) + 1) & LOBITS4;
        *value = Outs [(win << 3) | (reg_no & LOBITS3)];
        break;
    }
}


//------------------------------------------------------------------------
//
void CPU::WriteReg (const u32 value, const u32 reg_no) 
{
   int win = 0;

   switch ((reg_no >>3) & LOBITS2) {
   case 0 : // Globals
      Globals[reg_no & LOBITS3] = value;
      Globals[0] = 0;
      break;
   case 1 : // Outs
      Outs [((GetPSR() & LOBITS4) << 3) | (reg_no & LOBITS3)] = value;
      break;
   case 2 : // locals
      Locals [((GetPSR() & LOBITS4) << 3) | (reg_no & LOBITS3)] = value;
      break;
   case 3 : // Ins
      win = ((GetPSR() & LOBITS4) + 1) & LOBITS4;
      Outs [(win << 3) | (reg_no & LOBITS3)] = value;
      break;
   }
}

//------------------------------------------------------------------------
//
void CPU::WriteRegAll (const int RegBase, const u32 WriteValue) {
    switch ((RegBase >> 7) & 3) {
    case 0:
        Outs[RegBase & LOBITS7] = WriteValue;
        //if (Debug)
        //    fprintf(ofp, "          Read Outs[%0d]=%x\n", RegBase & LOBITS7, WriteValue); 
        break;
    case 1:
        Locals[RegBase & LOBITS7] = WriteValue;
        //if (Debug)
        //    fprintf(ofp, "          Read Locals[%0d]=%x\n", RegBase & LOBITS7, WriteValue); 
        break;
    case 2:
        Globals[RegBase & LOBITS4] = WriteValue;
        //if (Debug)
        //    fprintf(ofp, "          Read Globals[%0d]=%x\n", RegBase & LOBITS4, WriteValue); 
        break;
    case 3:
        throw std::runtime_error("*** WriteRegAll(): bad register address\n");
        break;
   }
}

//------------------------------------------------------------------------
//
u32 CPU::ReadRegsAll (const int reg_base) 
{
    switch ((reg_base >> 7) & LOBITS2) {
    case 0: return Outs[reg_base & LOBITS7]; break;
    case 1: return Locals[reg_base & LOBITS7]; break;
    case 2: return Globals[reg_base & LOBITS4]; break;
   }

   return 0;
}

//------------------------------------------------------------------------
//
u32 CPU::GetRegBase (const u32 reg_no) 
{
    switch ((reg_no >> 3) & LOBITS2) {
    case 0 : // Globals
        return  (2 << 7) | (reg_no & LOBITS3);
        break;
    case 1 : // Outs
        return  ((GetPSR() & 0xf) << 3) | (reg_no & LOBITS3);
        break;
    case 2 : // locals
        return  (1 << 7) | ((GetPSR() & LOBITS4) << 3) | (reg_no & LOBITS3);
        break;
    case 3 : // Ins
        return   (((GetPSR()+1) & LOBITS4) << 3) | (reg_no & LOBITS3);
        break;
    }
    return 0;
}


//------------------------------------------------------------------------
//
int CPU::MemRead(const u32 va, const int bytes, const u32 rd, const int signext) 
{
    u32 value, value_ext, reg_no;
    int ret1, ret2;
    reg_no = GetRegBase(rd);

    ret1 = ret2 = 0;

    switch (bytes) {
    case 1 :
        ret1 = MMU::MemAccess<intent_load,1>(va, value, CROSS_ENDIAN);
        value |= ((signext && (value & BIT7)) ? 0xffffff00 : 0);
        WriteRegAll(reg_no, value);
        break;
    case 2 : 
        ret1 = MMU::MemAccess<intent_load,2>(va, value, CROSS_ENDIAN);
        value |= ((signext && (value & BIT15)) ? 0xffff0000 : 0);
        WriteRegAll(reg_no, value);
        break;
    case 4 :
        ret1 = MMU::MemAccess<intent_load,4>(va, value, CROSS_ENDIAN);
        WriteRegAll(reg_no, value);
        break;
    case 8 :
        ret1 = MMU::MemAccess<intent_load,4>(va, value, CROSS_ENDIAN);
        ret2 = MMU::MemAccess<intent_load,4>(va+4, value_ext, CROSS_ENDIAN);
        WriteRegAll(reg_no, value);
        WriteRegAll(reg_no+1, value_ext);
        break;
    }

    return ret1 | ret2;
}

//------------------------------------------------------------------------
//
int CPU::MemWrite(const u32 va, const int bytes, const u32 rd) 
{
    u32 value, reg_no;
    int ret1, ret2;

    if (rd == GLOBALREG8)
        reg_no = rd;
    else
        reg_no = GetRegBase(rd);
    value = ReadRegsAll(reg_no);

    ret1 = ret2 = 0;
    switch (bytes) {
    case 1 :
        ret1 = MMU::MemAccess<intent_store,1>(va, value, CROSS_ENDIAN);
        break;
    case 2 : 
        ret1 = MMU::MemAccess<intent_store,2>(va, value, CROSS_ENDIAN);
        break;
    case 4 :
        ret1 = MMU::MemAccess<intent_store,4>(va, value, CROSS_ENDIAN);
        break;
    case 8 :
        ret1 = MMU::MemAccess<intent_store,4>(va, value, CROSS_ENDIAN);
        value = ReadRegsAll(reg_no+1);
        ret2 = MMU::MemAccess<intent_store,4>(va+4, value, CROSS_ENDIAN);
        break;
    }

    return ret1 | ret2; 

}


//------------------------------------------------------------------------
//
void CPU::DispReadReg (const u32 reg_no, u32 *value) 
{
    u32  win;
   
    win = (GetPSR() & LOBITS4);
    Globals[0] = 0;

    switch ((reg_no >> 3) & LOBITS2) {
    case 0 : // Globals
        *value = Globals[reg_no & LOBITS3];
        break;
    case 1 : // Outs
        *value = Outs [(win << 3) | (reg_no & LOBITS3)];
        break;
    case 2 : // locals
        *value = Locals [(win << 3) | (reg_no & LOBITS3)];
        break;
    case 3 : // Ins
        win = (GetPSR() & LOBITS4) + 1;
        *value = Outs [(win << 3) | (reg_no & LOBITS3)];
        break;
    }
}


//------------------------------------------------------------------------
//
void CPU::RegisterDump (bool transpose) {

    u32 a, b, c, d;
    if(!transpose) {
        ReadReg (OUTREG6, &a);
        os << std::format("Ma PC={:#08x} nPC={:#08x} PSR()={:#08x} (RegWin={:#d} N={:#d} Z={:#d} V={:#d} C={:#d} PIL={:#x} IRL={:#x})\n",
                 GetPC(), GetnPC(), GetPSR(), GetPSR() & LOBITS4,
                 (GetPSR() >> PSR_CC_NEGATIVE) & LOBITS1,
                 (GetPSR() >> PSR_CC_ZERO) & LOBITS1,
                 (GetPSR() >> PSR_CC_OVERFLOW) & LOBITS1,
                 (GetPSR() >> PSR_CC_CARRY) & LOBITS1, (GetPSR() >> PSR_INTERRUPT_LEVEL_0) & LOBITS4, GetIRL());
        os << std::format("Ma g0=({:#08x}) g1=({:#08x}) g2=({:#08x}) g3=({:#08x})\n",
                 Globals[0], Globals[1], Globals[2], Globals[3]);
        os << std::format("Ma g4=({:#08x}) g5=({:#08x}) g6=({:#08x}) g7=({:#08x})\n",
                 Globals[4], Globals[5], Globals[6], Globals[7]);
        DispReadReg (OUTREG0, &a);
        DispReadReg (OUTREG1, &b);
        DispReadReg (OUTREG2, &c);
        DispReadReg (OUTREG3, &d);
        os << std::format("Ma o0=({:#08x}) o1=({:#08x}) o2=({:#08x}) o3=({:#08x})\n", a, b, c, d);
        DispReadReg (OUTREG4, &a);
        DispReadReg (OUTREG5, &b);
        DispReadReg (OUTREG6, &c);
        DispReadReg (OUTREG7, &d);
        os << std::format("Ma o4=({:#08x}) o5=({:#08x}) o6=({:#08x}) o7=({:#08x})\n", a, b, c, d);
        DispReadReg (LOCALREG0, &a);
        DispReadReg (LOCALREG1, &b);
        DispReadReg (LOCALREG2, &c);
        DispReadReg (LOCALREG3, &d);
        os << std::format("Ma l0=({:#08x}) l1=({:#08x}) l2=({:#08x}) l3=({:#08x})\n", a, b, c, d);
        DispReadReg (LOCALREG4, &a);
        DispReadReg (LOCALREG5, &b);
        DispReadReg (LOCALREG6, &c);
        DispReadReg (LOCALREG7, &d);
        os << std::format("Ma l4=({:#08x}) l5=({:#08x}) l6=({:#08x}) l7=({:#08x})\n", a, b, c, d);
        DispReadReg (INREG0, &a);
        DispReadReg (INREG1, &b);
        DispReadReg (INREG2, &c);
        DispReadReg (INREG3, &d);
        os << std::format("Ma i0=({:#08x}) i1=({:#08x}) i2=({:#08x}) i3=({:#08x})\n", a, b, c, d);
        DispReadReg (INREG4, &a);
        DispReadReg (INREG5, &b);
        DispReadReg (INREG6, &c);
        DispReadReg (INREG7, &d);
        os << std::format("Ma i4=({:#08x}) i5=({:#08x}) i6=({:#08x}) i7=({:#08x})\n", a, b, c, d);
    } else {
        os << std::format("         INS         LOCALS      OUTS        GOBALS\n");
        DispReadReg (INREG0, &a);
        DispReadReg (LOCALREG0, &b);
        DispReadReg (OUTREG0, &c);
        d = Globals[0];
        os << std::setfill('0') << std::hex << std::setw(8) << "     0:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        DispReadReg (INREG1, &a);
        DispReadReg (LOCALREG1, &b);
        DispReadReg (OUTREG1, &c);
        d = Globals[1];
        os << std::setfill('0') << std::hex << std::setw(8) << "     1:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        DispReadReg (INREG2, &a);
        DispReadReg (LOCALREG2, &b);
        DispReadReg (OUTREG2, &c);
        d = Globals[2];
        os << std::setfill('0') << std::hex << std::setw(8) << "     2:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        DispReadReg (INREG3, &a);
        DispReadReg (LOCALREG3, &b);
        DispReadReg (OUTREG3, &c);
        d = Globals[3];
        os << std::setfill('0') << std::hex << std::setw(8) << "     3:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        DispReadReg (INREG4, &a);
        DispReadReg (LOCALREG4, &b);
        DispReadReg (OUTREG4, &c);
        d = Globals[4];
        os << std::setfill('0') << std::hex << std::setw(8) << "     4:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        DispReadReg (INREG5, &a);
        DispReadReg (LOCALREG5, &b);
        DispReadReg (OUTREG5, &c);
        d = Globals[5];
        os << std::setfill('0') << std::hex << std::setw(8) << "     5:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        DispReadReg (INREG6, &a);
        DispReadReg (LOCALREG6, &b);
        DispReadReg (OUTREG6, &c);
        d = Globals[6];
        os << std::setfill('0') << std::hex << std::setw(8) << "     6:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        DispReadReg (INREG7, &a);
        DispReadReg (LOCALREG7, &b);
        DispReadReg (OUTREG7, &c);
        d = Globals[7];
        os << std::setfill('0') << std::hex << std::setw(8) << "     7:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
 


    }
}

void CPU::handleMMUFault(pDecode_t d) {
    // Get the fault from MMU:
    u32 f = MMU::GetFaultStatus();
    u32 FT = (f >> 2) & 0x3;
    if(FT == 0)
        return; // No fault....
                
    if(FT > 0)
        Trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
}
