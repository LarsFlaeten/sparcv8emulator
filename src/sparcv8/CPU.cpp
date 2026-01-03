#include "CPU.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include "MMU.h"

#include "../debug.h"
#include "../cv_log.hpp"

#include "../LoopTimer.h"

#include "../gdb/DebugStopController.hpp"


#if 0
#define PERFORMANCE_MONITOR
#endif

#if 0
#define PROFILE_INSTRUCTIONS
#endif


#ifdef PROFILE_INSTRUCTIONS
#include <array>
#include "../dis.h"
#include <cstring>
// Counters for different format instructions
u32 format1_counter = 0;
std::array<u32, 8> format2_counter{};
std::array<u32, 128> format3_counter{};


#endif




//------------------------------------------------------------------------
// Reset()
//
// Reset major state. Called when run from time 0.
//
void CPU::reset(u32 entry_va = 0x0)
{
    rs.reason = TerminateReason::NORMAL;
    rs.instr_count = 0;
    rs.last_opcode = 0;

    if(verbose)
        os << "Resetting CPU, entry PC = 0x" << std::hex << entry_va << "\n";
    
    pc  = entry_va;
    npc = pc + 4;;

    psr = (1 << PSR_ENABLE_TRAPS) | (0 << PSR_PREV_SUPER_MODE) 
        | (1 << PSR_SUPER_MODE) | (1 << PSR_ENABLE_FLOATING_POINT)
        | (0x3 << PSR_VER) | (0xf << PSR_IMPL) ;
    //((pPSR_t)&psr)->et = 1;
    //((pPSR_t)&psr)->s  = 1;
    //((pPSR_t)&psr)->ps  = 0; // Previus trap supervisor bit
    //((pPSR_t)&psr)->ef = 1; // Enable floats by default

    // Leon3 specific values:
    //((pPSR_t)&psr)->imp = 0xf; // Gaisler
    //((pPSR_t)&psr)->ver = 0x3; // Leon3

    // Version 3 in bitfield 17:19
    fsr = 3 << 17; 

    irl = 0;

    trap_type = 0;
    tbr = 0;
    wim = 2;

    y_reg = 0;

    // Clear regs:
    // Clear register windows
    memset(globals, 0, sizeof(globals));
    memset(locals,  0, sizeof(locals));
    memset(outs,    0, sizeof(outs));
#ifdef FPU_IMPLEMENTED
    memset(freg,    0, sizeof(freg));
#endif

    mmu.reset();

    // Emulator control:
    _interrupt = false;
    powerdown_flag = false;
    wakeup_flag = false;
    running = false;
    gdb_stub = nullptr;
    
}

bool CPU::instr_fetch(u32 virt_addr, pDecode_t d){
 
    u32& opcode = d->opcode;
    
	bool super = (psr >> 7) & 0x1;
    
    if(mmu.MemAccess<intent_execute, 4>(virt_addr, opcode, CROSS_ENDIAN, super) < 0)
    {
        // Get the fault from MMU:
        u32 f = mmu.GetFaultStatus();
            
        // We have a fault..
        u32 AT = (f >> 5) & 0x7;
        
        if( AT<2 || AT>3)
            throw std::runtime_error("AT != 2 | 3 is not possible in an instruction fetch!");
   
        // The NF field in the MMU control regs governs wether we should TRAP    
        u32 nf = (mmu.GetControlReg() & 0x2) >> 1;

        // Only throw trap if nf == 0
        if(nf == 0) { 
            trap(d,  SPARC_INSTRUCTION_ACCESS_EXCEPTION); 
        	return false;
		} else {
			throw std::runtime_error("Unhandled error in instruction fetch (MMU nofault == 1)");
		}
        return false;
    } 

    return true;
}

u32  CPU::run(u32 ExecCount, RunSummary* _rs) {
    rs.reason = TerminateReason::INSTRUCTION;
 
#ifdef PERFORMANCE_MONITOR
	LoopTimer lt;
#endif
	   
    DecodeStruct Dec{};
    pDecode_t d;
    pPSR_t p;

    d = &Dec;
    p = reinterpret_cast<pPSR_t>(&psr);
    
    // Map the PSR structure to the decode PSR variable just once
    d->p = (pPSR_t) &(d->psr);


    u64 count = 0;
    //u32 word_count;
    
    // Start executing program
    running = true;
    while ((ExecCount == 0) ? 1 : (count < (u64)ExecCount)) {

        // Halt here if stopped by debug context:
        if (auto* dbg = DebugStopController::Global()) dbg->checkpoint(wtoken);

#ifdef PERFORMANCE_MONITOR
		lt.start();
#endif		
        ++count;
        
        // Process interrupts if ther are no traps being handled
        if (trap_type == 0 && (p->et && (irl > p->pil))) {
            if (verbose)
                os << std::format("INT  {:#x} PC={:#08x} NPC={:#08x}\n", irl, pc, npc);

            trap_type = irl + SPARC_INTERRUPT;
            // Do we clear IRL here, or handle that in interrupt handler?
            // Update - yes we absolutely have to clear it here. The interrupt handlers cannot reach IRL,
            // as it is not exposed in the machine... Hence, it was nevre cleared..
            irl = 0;
        }
        
        // Process Traps
        if (trap_type) {
            p->et = 0;
            p->ps = p->s;
            p->s = 1;
            u32 n_cwp = p->cwp;
            n_cwp = ((n_cwp - 1) & LOBITS5) % NWINDOWS;
            p->cwp = n_cwp;

            write_reg (pc,  LOCALREG1);
            write_reg (npc, LOCALREG2);
            
            tbr = (tbr & ~(0xff0)) | ((trap_type & LOBITS8) << 4);
            pc  = tbr;
            npc = tbr + 4;
            trap_type &= ~(LOBITS8);
        }

       
        u32 virt_addr = (u32) pc;

       	// Process instruction ...

        // ---- IFetch ----
        if(instr_fetch(virt_addr, d)) {
        
            excute_one(d);
        
        } else {
            // we could not fetch instruction, and no Traps occured.. Not much more to do.
            if(!trap_type) {
                throw std::runtime_error("Could not fetch instruction.");
            }
        }
        
        // Tick the bus, handling IO, interrupts etc
        if(bus_tick_func)
            bus_tick_func();
        
        // Check interrupt controller for pending interrupts and take any,
        // as long as there are not ongoing trap handling
        u32 _incoming_irl = intc.get_next_pending_irq(this->cpu_id_);
        if(_incoming_irl>0 && trap_type == 0) {
            set_irl(_incoming_irl); // We take this interrupt
            intc.clear_irq(_incoming_irl, this->cpu_id_);

            // In multithreaded, we break out in timer interrupts, and wait until we are started again
            if((irl == 8)) {
                if(break_on_timer_interrupt) {
                    rs.reason = TerminateReason::TIMER_INTERRUPT;
                    break;
                }
            }
        }

        

        // External request to interrupt this run tick
        if(_interrupt) {
            _interrupt = false;
            rs.reason = TerminateReason::RECV_SIGINT; // received SIGINT
            break;
        }

        /*
        // Request to power down (for this tick)
        if(power_down) {
            power_down = false;
            rs.reason = TerminateReason::POWER_DOWN;
            //CVLOG_UNMUTE();
            break;
        }*/

        // Check if any instructions tell us to exit
        if(rs.reason == TerminateReason::NORMAL || rs.reason == TerminateReason::UNIMPLEMENTED) {
            break;
        }

#ifdef PERFORMANCE_MONITOR
		lt.stop(d->opcode);
#endif
    }

#ifdef PERFORMANCE_MONITOR
	std::cout << "Stats for CPU # " << get_cpu_id() << "\n";	
	lt.printStats();
#endif

#ifdef PROFILE_INSTRUCTIONS
    //Assemble all instrucions in a vector of instr, count
    std::vector<std::pair<std::string, int>> indexedCounters;
    for (size_t i = 0; i < format3_counter.size(); ++i) {
        indexedCounters.emplace_back(format3_str[i], format3_counter[i]);
    }
    for (size_t i = 0; i < format2_counter.size(); ++i) {
        indexedCounters.emplace_back(format2_str[i], format2_counter[i]);
    }
    indexedCounters.emplace_back(format1_str, format1_counter);

    // Sort by value (second in the pair)
    std::sort(indexedCounters.begin(), indexedCounters.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;  // descending order
        }); 

    for(auto& m : indexedCounters) {
        if(m.second > 0)
            std::cout << m.first << "\t:" << m.second << " (" << (double)m.second*100.0/(double)count << "%)\n";
    }

#endif

    rs.instr_count = count;
    rs.last_opcode = d->opcode; 

    // Return summary
    if(_rs != nullptr)
        *_rs = rs;
    
    return count;
}

//------------------------------------------------------------------------
// Decode()
//
// Main instruction decode. Selects instc function and expands input data
//
void CPU::decode(pDecode_t d)
{
    // Map the PSR structure to the decode PSR
    d->p = (pPSR_t)&(d->psr);

    u32 fmt_bits = (d->opcode >> FMTSTARTBIT) & LOBITS2;
    u32 op2      = (d->opcode >> OP2STARTBIT) & LOBITS3;
    u32 op3      = (d->opcode >> OP3STARTBIT) & LOBITS6;
    u32 I_idx, regvalue;

    if (verbose) 
        os << std::format("{:#08x}: ", d->opcode);

    // By default, no change to program counter and PSR, and
    // no writeback
    d->pc      = pc;
    d->npc     = npc;
    d->psr     = psr;
    d->wb_type = WriteBackType::NO_WRITEBACK;

    switch (fmt_bits) {
    // CALL
    case 1:  
#ifdef PROFILE_INSTRUCTIONS
        ++format1_counter;    
#endif
        d->function     = format1;                         
        d->imm_disp_rs2 = d->opcode & LOBITS30;
        break;

    // SETHI, Branches
    case 0:  
#ifdef PROFILE_INSTRUCTIONS
        ++format2_counter[op2];    
#endif
        d->function     = format2[op2];                     
        d->rd           = (d->opcode >> RDSTARTBIT) & LOBITS5;
        d->op_2_3       = (d->opcode >> OP2STARTBIT) & LOBITS3;
        d->imm_disp_rs2 = (d->opcode & LOBITS22);
        break;

    // Memory accesses, ALU etc.
    case 3:  
    case 2:  
 #ifdef PROFILE_INSTRUCTIONS
        ++format3_counter[op3 + ((fmt_bits & 1) << 6)];   
#endif
        d->function     = format3[op3 + ((fmt_bits & 1) << 6)];
        d->rd           = (d->opcode >> RDSTARTBIT)  & LOBITS5;
        d->op_2_3       = (d->opcode >> OP3STARTBIT) & LOBITS6;
        d->rs1          = (d->opcode >> RS1STARTBIT) & LOBITS5;
        d->i            = (d->opcode >> ISTARTBIT)   & LOBITS1;
        d->imm_disp_rs2 = (d->opcode >> RS2STARTBIT) & LOBITS13;

        // All instructions need r[rs1] value
        // op = 11 and op3 = 100xxx means load or store from fregs,
        // handle that in function
        /*if((fmt_bits == 3) && ( (d->op_2_3 >> 3 ) == 4) ) {
            break;
        } else if ((fmt_bits == 2) && ( (d->op_2_3 == 52) || (d->op_2_3 == 53)) ) {
            break; // Skip read regs for FOP1 and FOP2
        } else
        { */       
            read_reg (d->rs1, &d->rs1_value);
        /*}*/

        // ev = ((RD/WR/ALU/Logic instr) ? r[rs1] : 0) + (i ? sign_ext(imm) : r[rs2])
        I_idx = op3 + ((fmt_bits & 1) << 6);
        regvalue = (I_idx < FIRST_RS1_EVAL_IDX) ? 0 : d->rs1_value;

        if (d->i)
           d->ev = regvalue + sign_ext13(d->imm_disp_rs2);
        else {
            
            read_reg(d->imm_disp_rs2 & LOBITS5, &d->ev);
            d->ev += regvalue;
        }

        // All format 3 instructions require at least one register read (rs1),
        // but a few require more...

        // A store double access requires two register reads
        // from, r[rd], r[rd+1].
        if (I_idx == STORE_DBL_IDX) {
            read_reg (d->rd & ~LOBITS1, &d->value);
            read_reg ((d->rd & ~LOBITS1)+1, &d->value1);

        // Ticc requires an r[rs2] read if i bit clear
        } else if (I_idx == TICC_IDX && !d->i)
            read_reg (d->imm_disp_rs2 & LOBITS5, &d->value); 

        // Memory instructions need to read r[rd]
        else if (fmt_bits & 1)
            read_reg (d->rd, &d->value);

        break;
   }
}

//------------------------------------------------------------------------
// WriteBack()
//
// Write back register values
//
void CPU::write_back (const pDecode_t d)
{
    pc  = d->pc;
    npc = d->npc;
    psr = d->psr;

    if (d->wb_type == WriteBackType::WRITEBACKREG) 
        write_reg(d->value, d->rd);
}


// ------------------------------------------------

void CPU::trap(pDecode_t d, u32 trap_no) 
{
    int tn = trap_no & LOBITS8;
    u32 npc = (tbr & ~(0xff0)) | ((tn & LOBITS8) << 4);;

    if ( ((psr >> PSR_ENABLE_TRAPS) & 0x1) == 0) {
         fprintf (stderr, "ERROR TRAP %x WHILE TRAPS DISABLED\n", trap_no);
         dump_regs();
         //terminate = d->opcode;
         rs.last_opcode = d->opcode;
         rs.reason = TerminateReason::TRAP_CONDITIONAL;
         throw std::runtime_error("Trap in trap");
         return;
    }

    if (verbose) {
        if (tn < 0x40)
            os << std::format("                   TRAP {} ({:#x}) PC={:#08x} NPC={:#08x}\n", trap_str[tn], tn, pc, npc);
        else if (tn >= 0x40 && tn < 0x60)
            os << std::format("                   TRAP NO TRAP ({:#x}) PC={:#08x} NPC={:#08x}\n", tn, pc, npc);
        else if (tn >= 0x60 && tn < 0x80)
            os << std::format("                   TRAP Impl. Dep. ({:#x}) PC={:#08x} NPC={:#08x}\n", tn, pc, npc);
        else 
            os << std::format("                   TRAP Ticc ({:#x}) PC={:#08x} NPC={:#08x}\n", tn, pc, npc);
    }
/*
    // Set PS bit on PSR from S bit
    // (PS is supervisor bit on last (this) Trao)
    // NO, this is allready implemented in CPU::Run handling of traps
    u32 s = (PSR >> 7) & 0x1; // Fetch S-bit
    PSR = PSR & ~(1 << 6); // Clear PS;
    PSR = PSR | (s << 6); // Set SP from S
*/
    trap_type = (trap_type & ~(LOBITS8)) | tn;
}

void CPU::enter_powerdown()
{
    std::unique_lock<std::mutex> lock(power_mtx);

    powerdown_flag.store(true, std::memory_order_release);

    power_cv.wait(lock, [&]{
        return wakeup_flag.load();
    });

    powerdown_flag.store(false);
    wakeup_flag.store(false);
}

void CPU::wakeup()
{
    wakeup_flag.store(true, std::memory_order_release);
    power_cv.notify_one();
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


int CPU::test_cc (pDecode_t d) {

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
        std::cerr << "*** test_cc(): fatal error\n";
        throw std::runtime_error("*** test_cc(): fatal error");
    }

    return 0;
}


//------------------------------------------------------------------------
//
void CPU::read_reg (const u32 reg_no, u32 * const value) 
{
    int win;

    globals[0] = 0;
    if(reg_no == GLOBALREG8) {
        *value = *p_swap_reg;
        return;
    }
    
    switch (reg_no >> 3) {
    case 0 : // Globals
        *value = globals[reg_no & LOBITS3];
        break;
    case 1 : // Outs
        *value = outs [((get_psr() & LOBITS5) << 3) | (reg_no & LOBITS3)];
        break;
    case 2 : // locals
        *value = locals [((get_psr() & LOBITS5) << 3) | (reg_no & LOBITS3)];
        break;
    case 3 : // Ins
        win = ((get_psr() & LOBITS5) + 1) % NWINDOWS;
        *value = outs [(win << 3) | (reg_no & LOBITS3)];
        break;
    }
}


//------------------------------------------------------------------------
//
void CPU::write_reg (const u32 value, const u32 reg_no) 
{
   int win = 0;

   if(reg_no == GLOBALREG8) {
        *p_swap_reg = value;
        return;
    }
 
   switch ((reg_no >>3) & LOBITS2) {
   case 0 : // Globals
      globals[reg_no & LOBITS3] = value;
      globals[0] = 0;
      break;
   case 1 : // Outs
      outs [((get_psr() & LOBITS5) << 3) | (reg_no & LOBITS3)] = value;
      break;
   case 2 : // locals
      locals [((get_psr() & LOBITS5) << 3) | (reg_no & LOBITS3)] = value;
      break;
   case 3 : // Ins
      win = ((get_psr() & LOBITS5) + 1) % NWINDOWS;
      outs [(win << 3) | (reg_no & LOBITS3)] = value;
      break;
   }
}
/*
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
*/

//------------------------------------------------------------------------
//
int CPU::mem_read(const u32 va, const int bytes, const u32 rd, const int signext, bool forced_cache_miss) 
{
    // TODO:
    // Implement cache and possible forced cache miss (ASI=1)


    bool super = ((psr >> 7) & 0x1) == 0x1;


    u32 value = 0;
    u32 value_ext = 0;
    int ret1, ret2;
    //reg_no = GetRegBase(rd);

    ret1 = ret2 = 0;

    switch (bytes) {
    case 1 :
        ret1 = mmu.MemAccess<intent_load,1>(va, value, CROSS_ENDIAN, super);
        if(ret1 == 0) {
            value |= ((signext && (value & BIT7)) ? 0xffffff00 : 0);
            write_reg(value, rd);
        }
        break;
    case 2 : 
        ret1 = mmu.MemAccess<intent_load,2>(va, value, CROSS_ENDIAN, super);
        if(ret1 == 0) {
            value |= ((signext && (value & BIT15)) ? 0xffff0000 : 0);
            write_reg(value, rd);
        }
        break;
    case 4 :
        ret1 = mmu.MemAccess<intent_load,4>(va, value, CROSS_ENDIAN, super);
        if(ret1 == 0)
            write_reg(value, rd);
        break;
    case 8 :
        ret1 = mmu.MemAccess<intent_load,4>(va, value, CROSS_ENDIAN, super);
        ret2 = mmu.MemAccess<intent_load,4>(va+4, value_ext, CROSS_ENDIAN, super);
        if( (ret1 == 0) && (ret2 == 0) ) {
            write_reg(value, rd);
            write_reg(value_ext, rd+1);
        }
        break;
    }

    return ret1 | ret2;
}

//------------------------------------------------------------------------
//
int CPU::mem_write(const u32 va, const int bytes, const u32 rd) 
{
    u32 value;
    int ret1, ret2;

    bool super = ((psr >> 7) & 0x1) == 0x1;


    
    read_reg(rd, &value);
    
    ret1 = ret2 = 0;
    switch (bytes) {
    case 1 :
        ret1 = mmu.MemAccess<intent_store,1>(va, value, CROSS_ENDIAN, super);
        break;
    case 2 : 
        ret1 = mmu.MemAccess<intent_store,2>(va, value, CROSS_ENDIAN, super);
        break;
    case 4 :
        ret1 = mmu.MemAccess<intent_store,4>(va, value, CROSS_ENDIAN, super);
        break;
    case 8 :
        ret1 = mmu.MemAccess<intent_store,4>(va, value, CROSS_ENDIAN, super);
        read_reg(rd+1, &value);
        ret2 = mmu.MemAccess<intent_store,4>(va+4, value, CROSS_ENDIAN, super);
        break;
    }

    return ret1 | ret2; 

}


//------------------------------------------------------------------------
//
/*
void CPU::disp_read_reg (const u32 reg_no, u32 *value) 
{
    u32  win;
   
    win = (get_psr() & LOBITS4);
    globals[0] = 0;

    switch ((reg_no >> 3) & LOBITS2) {
    case 0 : // Globals
        *value = globals[reg_no & LOBITS3];
        break;
    case 1 : // Outs
        *value = outs [(win << 3) | (reg_no & LOBITS3)];
        break;
    case 2 : // locals
        *value = locals [(win << 3) | (reg_no & LOBITS3)];
        break;
    case 3 : // Ins
        win = (get_psr() & LOBITS4) + 1;
        *value = outs [(win << 3) | (reg_no & LOBITS3)];
        break;
    }
}
*/

//------------------------------------------------------------------------
//
void CPU::dump_regs (bool transpose) {

    u32 a = 0;
    u32 b = 0;
    u32 c = 0;
    u32 d = 0;
    if(!transpose) {
        read_reg (OUTREG6, &a);
        os << std::format("Ma PC={:#08x} nPC={:#08x} PSR()={:#08x} (RegWin={:#d} N={:#d} Z={:#d} V={:#d} C={:#d} PIL={:#x} IRL={:#x})\n",
                 get_pc(), get_npc(), get_psr(), get_psr() & LOBITS4,
                 (get_psr() >> PSR_CC_NEGATIVE) & LOBITS1,
                 (get_psr() >> PSR_CC_ZERO) & LOBITS1,
                 (get_psr() >> PSR_CC_OVERFLOW) & LOBITS1,
                 (get_psr() >> PSR_CC_CARRY) & LOBITS1, (get_psr() >> PSR_INTERRUPT_LEVEL_0) & LOBITS4, get_irl());
        os << std::format("Ma g0=({:#08x}) g1=({:#08x}) g2=({:#08x}) g3=({:#08x})\n",
                 globals[0], globals[1], globals[2], globals[3]);
        os << std::format("Ma g4=({:#08x}) g5=({:#08x}) g6=({:#08x}) g7=({:#08x})\n",
                 globals[4], globals[5], globals[6], globals[7]);
        read_reg (OUTREG0, &a);
        read_reg (OUTREG1, &b);
        read_reg (OUTREG2, &c);
        read_reg (OUTREG3, &d);
        os << std::format("Ma o0=({:#08x}) o1=({:#08x}) o2=({:#08x}) o3=({:#08x})\n", a, b, c, d);
        read_reg (OUTREG4, &a);
        read_reg (OUTREG5, &b);
        read_reg (OUTREG6, &c);
        read_reg (OUTREG7, &d);
        os << std::format("Ma o4=({:#08x}) o5=({:#08x}) o6=({:#08x}) o7=({:#08x})\n", a, b, c, d);
        read_reg (LOCALREG0, &a);
        read_reg (LOCALREG1, &b);
        read_reg (LOCALREG2, &c);
        read_reg (LOCALREG3, &d);
        os << std::format("Ma l0=({:#08x}) l1=({:#08x}) l2=({:#08x}) l3=({:#08x})\n", a, b, c, d);
        read_reg (LOCALREG4, &a);
        read_reg (LOCALREG5, &b);
        read_reg (LOCALREG6, &c);
        read_reg (LOCALREG7, &d);
        os << std::format("Ma l4=({:#08x}) l5=({:#08x}) l6=({:#08x}) l7=({:#08x})\n", a, b, c, d);
        read_reg (INREG0, &a);
        read_reg (INREG1, &b);
        read_reg (INREG2, &c);
        read_reg (INREG3, &d);
        os << std::format("Ma i0=({:#08x}) i1=({:#08x}) i2=({:#08x}) i3=({:#08x})\n", a, b, c, d);
        read_reg (INREG4, &a);
        read_reg (INREG5, &b);
        read_reg (INREG6, &c);
        read_reg (INREG7, &d);
        os << std::format("Ma i4=({:#08x}) i5=({:#08x}) i6=({:#08x}) i7=({:#08x})\n", a, b, c, d);
    } else {
        os << std::format("         INS         LOCALS      OUTS        GOBALS\n");
        read_reg (INREG0, &a);
        read_reg (LOCALREG0, &b);
        read_reg (OUTREG0, &c);
        d = globals[0];
        os << std::setfill('0') << std::hex << std::setw(8) << "     0:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        read_reg (INREG1, &a);
        read_reg (LOCALREG1, &b);
        read_reg (OUTREG1, &c);
        d = globals[1];
        os << std::setfill('0') << std::hex << std::setw(8) << "     1:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        read_reg (INREG2, &a);
        read_reg (LOCALREG2, &b);
        read_reg (OUTREG2, &c);
        d = globals[2];
        os << std::setfill('0') << std::hex << std::setw(8) << "     2:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        read_reg (INREG3, &a);
        read_reg (LOCALREG3, &b);
        read_reg (OUTREG3, &c);
        d = globals[3];
        os << std::setfill('0') << std::hex << std::setw(8) << "     3:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        read_reg (INREG4, &a);
        read_reg (LOCALREG4, &b);
        read_reg (OUTREG4, &c);
        d = globals[4];
        os << std::setfill('0') << std::hex << std::setw(8) << "     4:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        read_reg (INREG5, &a);
        read_reg (LOCALREG5, &b);
        read_reg (OUTREG5, &c);
        d = globals[5];
        os << std::setfill('0') << std::hex << std::setw(8) << "     5:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        read_reg (INREG6, &a);
        read_reg (LOCALREG6, &b);
        read_reg (OUTREG6, &c);
        d = globals[6];
        os << std::setfill('0') << std::hex << std::setw(8) << "     6:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
        read_reg (INREG7, &a);
        read_reg (LOCALREG7, &b);
        read_reg (OUTREG7, &c);
        d = globals[7];
        os << std::setfill('0') << std::hex << std::setw(8) << "     7:  0x" << std::setw(8) <<  a << "  0x" << std::setw(8) << b << "  0x" << std::setw(8) << c << "  0x" << std::setw(8) << d << "\n" << std::dec;
 


    }
}
