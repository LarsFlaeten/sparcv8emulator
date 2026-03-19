// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <map>

#if 0 // Use this for older compilers
#ifndef __cpp_lib_format
  // std::format polyfill using fmtlib
  #include <fmt/core.h>
  namespace std {
  using fmt::format;
  }
#else
#endif
#else
#include <format>
#endif

#include "../common.h"




// Define this to use LEON specific ASI Assignments
#define CONFIG_SPARC_LEON
#define FPU_IMPLEMENTED

#include "CPU_defines.h"
#include "CPU_instr_map.h"

typedef struct {
    unsigned int cwp : 5;       // Current window pointer
    unsigned int et  : 1;       // Enable traps
    unsigned int ps  : 1;       // Previous supervisor mode
    unsigned int s   : 1;       // Supervisor mode
    unsigned int pil : 4;       // Proc interrupt level
    unsigned int ef  : 1;       // FPU enable
    unsigned int ec  : 1;       // Co-processor enable
    unsigned int rsv2: 2;       // Reserved
    unsigned int rsv1: 4;       // Reserved
    unsigned int c   : 1;       // Carry
    unsigned int v   : 1;       // Overflow
    unsigned int z   : 1;       // Zero
    unsigned int n   : 1;       // Negative 
    unsigned int ver : 4;       // Version
    unsigned int imp : 4;       // Implementation


} PSR_t, *pPSR_t;

enum TerminateReason {
    NORMAL = -1,
    INSTRUCTION = 0,
    BREAK = 1,
    STEP = 2,
    TRAP_CONDITIONAL = 3,
    UNIMPLEMENTED = 4,
    RECV_SIGINT = 5,
    TIMER_INTERRUPT = 6,
    POWER_DOWN = 7
};

static inline const char* to_string(TerminateReason r) {
    switch (r) {
        case NORMAL:            return "NORMAL";
        case INSTRUCTION:       return "INSTRUCTION";
        case BREAK:             return "BREAK";
        case STEP:              return "STEP";
        case TRAP_CONDITIONAL:  return "TRAP_CONDITIONAL";
        case UNIMPLEMENTED:     return "UNIMPLEMENTED";
        case RECV_SIGINT:       return "RECV_SIGINT";
        case TIMER_INTERRUPT:   return "TIMER_INTERRUPT";
        case POWER_DOWN:        return "POWER_DOWN";
        default:                return "UNKNOWN_TERMINATE_REASON";
    }
}

// Defer structure definition
class CPU;
class IRQMP;
class MMU;
#include "MMU.h"
#include "../peripherals/IRQMP.h"

struct  DecodeStruct;
#include "../gdb/gdb_stub.hpp"
#include "../gdb/DebugStopController.hpp"


typedef struct DecodeStruct *pDecode_t;
typedef void (CPU::*pc_func)(pDecode_t);

enum WriteBackType {
    NO_WRITEBACK =      0,
    WRITEBACKREG =      1
};


struct DecodeStruct {
    u32 opcode;              // Instruction opcode
    pc_func function;                                          // Pointer to inst.c function
    u32 rd;                  // rd register number
    u32 rs1;                 // rs1 register number
    u32 rs1_value;           // rs1 register value
    u32 imm_disp_rs2;        // Immediate, displacement or rs2 
    u32 ev;                  // Extended value (ev) = r[rs1] + (i ? sign_ext(imm) : r[rs2])
    u32 op_2_3;              // op2 or op3 value
    u32 i;                   // i, imm/rs2 indicator

    u32 pc;                  // Current program counter value
    u32 npc;                 // Next program counter value
    u32 psr;                 // Program status register value
    pPSR_t p;                   // Pointer to structured version of PSR
    WriteBackType wb_type;             // Write back flag
    u32 value;               // Write back value
    u32 value1;              // Second (optional) write back value
};





struct RunSummary {
    TerminateReason reason;
    u64 instr_count;
    u32 last_opcode;
};

static inline void print_run_summary(
    const RunSummary& rs,
    int cpu_id = -1
) {
    if (cpu_id >= 0) {
        printf(
            "[CPU%d] RunSummary: reason=%s (%d), instr=%llu, last_opcode=0x%08x\n",
            cpu_id,
            to_string(rs.reason),
            static_cast<int>(rs.reason),
            static_cast<unsigned long long>(rs.instr_count),
            rs.last_opcode
        );
    } else {
        printf(
            "RunSummary: reason=%s (%d), instr=%llu, last_opcode=0x%08x\n",
            to_string(rs.reason),
            static_cast<int>(rs.reason),
            static_cast<unsigned long long>(rs.instr_count),
            rs.last_opcode
        );
    }
}

class CPU 
{    
    private:
        ///////////////////////
        // Major state of model
        u32 pc;
        u32 npc;

        u32 psr;
        u32 irl;

        u32 trap_type;
        u32 tbr;
        u32 wim;

        u32 y_reg;

        u32 cpu_id_;
        u32 fsr; // FPU state register
        u32 cwp_base_ = 0; // Cached: (psr & LOBITS5) << 3, updated whenever CWP changes
        __attribute__((always_inline)) void update_cwp_bases() {
            cwp_base_ = (psr & LOBITS5) << 3;
        }
        ///////////////////////
        // Registers
        u32 globals [9];              // Global[8] is a pseudo register used only for SWAP
        u32 locals  [NWINDOWS * 8];
        u32 outs    [NWINDOWS * 8];

        u32 *p_swap_reg = &globals[8];

#ifdef FPU_IMPLEMENTED
        u32 freg[32];
#endif


        // Emulator control and debugging
       	std::ostream& os;
        bool running = false;
        bool power_down_enabled = false;
        std::atomic<bool> powerdown_flag{false};
        std::atomic<bool> wakeup_flag{false};

        std::mutex power_mtx;
        std::condition_variable power_cv;

        
        RunSummary rs;
        GdbStub*    gdb_stub = nullptr;
        DebugStopController::WorkerToken wtoken;
        
        std::string DispRegStr (u32 regnum);

        bool _interrupt;
        MMU mmu;  
        IRQMP& intc;      
   public:
        CPU(MCtrl& mctrl, IRQMP& intc, u32 cpu_id, std::ostream& out = std::cout) : cpu_id_(cpu_id), os(out), _interrupt(false), mmu(mctrl), intc(intc)
        { 
            // Set Cache control regs as TSIM does
            mmu.set_ccr(0x00020000);
            mmu.set_iccr(0x10220008);
            mmu.set_dccr(0x18220008);

            // Register worker since we now have the unique name
            if (auto* dbg = DebugStopController::Global())
                wtoken = dbg->register_worker("CPU" + std::to_string(cpu_id_));
            
        }

        CPU() = delete;
        CPU(const CPU&) = delete;
        CPU& operator=(const CPU&) = delete;
        
        // Main execution flow methods
 
        void reset(u32 entry_va);

        u32  run(u32 ExecCount = 0, RunSummary* _rs = nullptr);
        
        __attribute__((always_inline)) bool instr_fetch(u32 virt_addr, pDecode_t d) {
            u32& opcode = d->opcode;
            bool super = (psr >> 7) & 0x1;
            if (mmu.MemAccess<intent_execute, 4>(virt_addr, opcode, CROSS_ENDIAN, super) < 0) {
                u32 f  = mmu.get_fault_status();
                u32 AT = (f >> 5) & 0x7;
                if (AT < 2 || AT > 3)
                    throw std::runtime_error("AT != 2 | 3 is not possible in an instruction fetch!");
                u32 nf = (mmu.get_control_reg() & 0x2) >> 1;
                if (nf == 0) { trap(d, SPARC_INSTRUCTION_ACCESS_EXCEPTION); return false; }
                else throw std::runtime_error("Unhandled error in instruction fetch (MMU nofault == 1)");
            }
            return true;
        }

        __attribute__((always_inline)) void excute_one(pDecode_t d) {
            // ---- Decode ----
            decode (d);

            // ---- Execute ----
            (this->*d->function)(d);

            // ---- Writeback ----
            write_back(d);
        }

        __attribute__((always_inline)) void decode(pDecode_t d) {
            const u32 fmt_bits = (d->opcode >> FMTSTARTBIT) & LOBITS2;
            const u32 op2      = (d->opcode >> OP2STARTBIT) & LOBITS3;
            const u32 op3      = (d->opcode >> OP3STARTBIT) & LOBITS6;
            u32 I_idx, regvalue;

            // By default, no change to program counter and PSR, and no writeback
            d->pc      = pc;
            d->npc     = npc;
            d->psr     = psr;
            d->wb_type = WriteBackType::NO_WRITEBACK;

            switch (fmt_bits) {
            case 1:
                d->function     = format1;
                d->imm_disp_rs2 = d->opcode & LOBITS30;
                break;
            case 0:
                d->function     = format2[op2];
                d->rd           = (d->opcode >> RDSTARTBIT) & LOBITS5;
                d->op_2_3       = (d->opcode >> OP2STARTBIT) & LOBITS3;
                d->imm_disp_rs2 = (d->opcode & LOBITS22);
                break;
            case 3:
            case 2:
                d->function     = format3[op3 + ((fmt_bits & 1) << 6)];
                d->rd           = (d->opcode >> RDSTARTBIT)  & LOBITS5;
                d->op_2_3       = (d->opcode >> OP3STARTBIT) & LOBITS6;
                d->rs1          = (d->opcode >> RS1STARTBIT) & LOBITS5;
                d->i            = (d->opcode >> ISTARTBIT)   & LOBITS1;
                d->imm_disp_rs2 = (d->opcode >> RS2STARTBIT) & LOBITS13;

                read_reg(d->rs1, &d->rs1_value);

                I_idx    = op3 + ((fmt_bits & 1) << 6);
                regvalue = (I_idx < FIRST_RS1_EVAL_IDX) ? 0 : d->rs1_value;

                if (d->i)
                    d->ev = regvalue + sign_ext13(d->imm_disp_rs2);
                else {
                    read_reg(d->imm_disp_rs2 & LOBITS5, &d->ev);
                    d->ev += regvalue;
                }

                if (I_idx == STORE_DBL_IDX) {
                    read_reg(d->rd & ~LOBITS1, &d->value);
                    read_reg((d->rd & ~LOBITS1)+1, &d->value1);
                } else if (I_idx == TICC_IDX && !d->i)
                    read_reg(d->imm_disp_rs2 & LOBITS5, &d->value);
                else if (fmt_bits & 1)
                    read_reg(d->rd, &d->value);
                break;
            }
        }
        
        inline void write_back(const pDecode_t d) {
            pc  = d->pc;
            npc = d->npc;
            psr = d->psr;
            update_cwp_bases();
            if (d->wb_type == WriteBackType::WRITEBACKREG)
                write_reg(d->value, d->rd);
        }

        void trap (pDecode_t d, u32 trap_no); 


        void nop() { return; } // Just for the sake of it...


        // Read/write registers (inlined for hot-path performance)
        inline void read_reg(const u32 reg_no, u32* const value) {
            if (__builtin_expect(reg_no == GLOBALREG8, 0)) { *value = *p_swap_reg; return; }
            switch (reg_no >> 3) {
            case 0: *value = globals[reg_no & LOBITS3]; break;
            case 1: *value = outs  [cwp_base_ | (reg_no & LOBITS3)]; break;
            case 2: *value = locals[cwp_base_ | (reg_no & LOBITS3)]; break;
            case 3: *value = outs  [((cwp_base_ + 8) & (NWINDOWS*8-1)) | (reg_no & LOBITS3)]; break;
            }
        }

        inline void write_reg(const u32 value, const u32 reg_no) {
            if (__builtin_expect(reg_no == GLOBALREG8, 0)) { *p_swap_reg = value; return; }
            switch ((reg_no >> 3) & LOBITS2) {
            case 0: globals[reg_no & LOBITS3] = value; globals[0] = 0; break;
            case 1: outs  [cwp_base_ | (reg_no & LOBITS3)] = value; break;
            case 2: locals[cwp_base_ | (reg_no & LOBITS3)] = value; break;
            case 3: outs  [((cwp_base_ + 8) & (NWINDOWS*8-1)) | (reg_no & LOBITS3)] = value; break;
            }
        }
        
        // Handy methods for reading/writing between memory and a reg
        int load8(const u32 va, const u32 rd, const int signext, bool forced_cache_miss);    
        int load16(const u32 va, const u32 rd, const int signext, bool forced_cache_miss);    
        int load32(const u32 va, const u32 rd, const int signext, bool forced_cache_miss);    
        int load64(const u32 va, const u32 rd, const int signext, bool forced_cache_miss);    
        int store8(const u32 va, const u32 rd);
        int store16(const u32 va, const u32 rd);
        int store32(const u32 va, const u32 rd);
        int store64(const u32 va, const u32 rd);
        
        // Get/Set multi-core id:
        //void set_cpu_id(u32 value) { 
        //    cpu_id = value; 
        //}
        u32 get_cpu_id() const { return cpu_id_; }
        // State Accessors
        u32 get_psr() const {return psr;}
        void set_psr(u32 value) { psr = value; update_cwp_bases(); }
        u32 get_wim() const {return wim;}
        u32 get_tbr() const {return tbr;}
        u32 get_y_reg() const {return y_reg;}
        u32 get_pc() const { return pc;  }
        u32 get_npc() const { return npc; }
        u32 get_irl() const { return irl; }
        u32 get_trap_type() const { return trap_type; }
        void set_irl(u32 _irl)  { irl = (_irl & 0xf); }
        u32 get_fsr() const { return fsr; }
        void set_fsr(u32 _fsr)  { fsr = _fsr; }
        bool is_running() const {return running;}
        

        // Helper CC test
        __attribute__((always_inline)) int test_cc(pDecode_t d) {
            const int cond = d->rd & LOBITS4;
            switch (cond & LOBITS3) {
            case 0: return (cond >> 3) & LOBITS1;
            case 1: return ((cond >> 3) ^ d->p->z);
            case 2: return ((cond >> 3) ^ (d->p->z | (d->p->n ^ d->p->v)));
            case 3: return ((cond >> 3) ^ (d->p->n ^ d->p->v));
            case 4: return ((cond >> 3) ^ (d->p->c | d->p->z));
            case 5: return ((cond >> 3) ^ d->p->c);
            case 6: return ((cond >> 3) ^ d->p->n);
            case 7: return ((cond >> 3) ^ d->p->v);
            default: throw std::runtime_error("*** test_cc(): fatal error");
            }
        }


        // Emulator control and debugging
        void interrupt() { _interrupt = true; }
        MMU&    get_mmu() {return mmu;}
        std::ostream& get_ostream() const { return os; }
        void    set_gdb_stub(GdbStub* stub) {gdb_stub = stub;}
        void    enable_power_down(bool pd) {power_down_enabled = pd;}
        void    dump_regs (bool transpose = false); 
        //void    disp_read_reg (const u32 reg_no, u32 *value); 
        IRQMP&  get_intc_ref() {return intc;}

        void enter_powerdown();    // handles sleeping
        
        void wakeup();             // called by IRQMP


	private:
	// Common instructions
    // Implemented in CPU_instructions.cpp
	void UNIMP   (pDecode_t d);
	void CALL    (pDecode_t d);
	void BICC    (pDecode_t d);
	void SETHI   (pDecode_t d);
	void SLL     (pDecode_t d);
	void SRL     (pDecode_t d);
	void SRA     (pDecode_t d);
	void RDY     (pDecode_t d);
	void RDPSR   (pDecode_t d);
	void RDWIM   (pDecode_t d);
	void RDTBR   (pDecode_t d);
	void WRY     (pDecode_t d);
	void WRPSR   (pDecode_t d);
	void WRWIM   (pDecode_t d);
	void WRTBR   (pDecode_t d);
	void JMPL    (pDecode_t d);
	void RETT    (pDecode_t d);
	void TICC    (pDecode_t d);
	void SAVE    (pDecode_t d);
	void RESTORE (pDecode_t d);
	void FLUSH   (pDecode_t d);
	void MULSCC  (pDecode_t d);
	void LD      (pDecode_t d);
	void LDUB    (pDecode_t d);
	void LDUH    (pDecode_t d);
	void LDD     (pDecode_t d);
	void LDSB    (pDecode_t d);
	void LDSH    (pDecode_t d);
	void ST      (pDecode_t d);
	void STB     (pDecode_t d);
	void STH     (pDecode_t d);
	void STD     (pDecode_t d);
	void SWAP    (pDecode_t d);
	void SWAPA    (pDecode_t d);
	void LDSTUB  (pDecode_t d);
	void MUL     (pDecode_t d);
	void DIV     (pDecode_t d);
	void ADD     (pDecode_t d);
	void AND     (pDecode_t d);

    //////////////////////////////////////
    // Extended instructions
    // Implemented in CPU_instructions_extensions.cpp
    void STA_impl   (pDecode_t d);
    void LDA_impl   (pDecode_t d);

    //////////////////////////////////////
    // LEON3 Extensions
    //
    void CASA    (pDecode_t d);

#ifdef FPU_IMPLEMENTED
    //////////////////////////////////////
    // FPU Extensions
    //
    void LDF_impl     (pDecode_t d);
    void LDDF_impl    (pDecode_t d);
    void LDFSR_impl   (pDecode_t d);
    void STF_impl     (pDecode_t d);
    void STDF_impl    (pDecode_t d);
    void STFSR_impl   (pDecode_t d);
    void STDFQ_impl   (pDecode_t d);
    void FBFCC_impl   (pDecode_t d);
    void FOP1_impl    (pDecode_t d);
    void FOP2_impl    (pDecode_t d);

#endif
	//////////////////////////////////////
	// Instruction function pointer tables

	// Format 1 instruction
    const pc_func  format1 = &CPU::CALL;

	// Format 2 instructions
	const pc_func format2[8] = {
		&CPU::UNIMP    , &CPU::UNIMP    , &CPU::BICC     , &CPU::UNIMP,
		&CPU::SETHI    , &CPU::UNIMP    , &CPU::FBFCC    , &CPU::CBCCC};

	// Format 3 instructions 
	const pc_func format3[128] = {
		&CPU::ADD      , &CPU::AND      , &CPU::OR       , &CPU::XOR      ,  // opcode[31:30] = 2
		&CPU::SUB      , &CPU::ANDN     , &CPU::ORN      , &CPU::XNOR     ,
		&CPU::ADDX     , &CPU::UNIMP    , &CPU::UMUL     , &CPU::SMUL     ,
		&CPU::SUBX     , &CPU::UNIMP    , &CPU::UDIV     , &CPU::SDIV     ,
		&CPU::ADDCC    , &CPU::ANDCC    , &CPU::ORCC     , &CPU::XORCC    ,
		&CPU::SUBCC    , &CPU::ANDNCC   , &CPU::ORNCC    , &CPU::XNORCC   ,
		&CPU::ADDXCC   , &CPU::UNIMP    , &CPU::UMULCC   , &CPU::SMULCC   ,
		&CPU::SUBXCC   , &CPU::UNIMP    , &CPU::UDIVCC   , &CPU::SDIVCC   ,
		&CPU::TADDCC   , &CPU::TSUBCC   , &CPU::TADDCCTV , &CPU::TSUBCCTV ,
		&CPU::MULSCC   , &CPU::SLL      , &CPU::SRL      , &CPU::SRA      ,
		&CPU::RDY      , &CPU::RDPSR    , &CPU::RDWIM    , &CPU::RDTBR    ,
		&CPU::UNIMP    , &CPU::UNIMP    , &CPU::UNIMP    , &CPU::UNIMP    ,
		&CPU::WRY      , &CPU::WRPSR    , &CPU::WRWIM    , &CPU::WRTBR    ,
		&CPU::FPOP1    , &CPU::FPOP2    , &CPU::CPOP1    , &CPU::CPOP2    ,
		&CPU::JMPL     , &CPU::RETT     , &CPU::TICC     , &CPU::FLUSH    ,
		&CPU::SAVE     , &CPU::RESTORE  , &CPU::UNIMP    , &CPU::UNIMP    ,
		&CPU::LD       , &CPU::LDUB     , &CPU::LDUH     , &CPU::LDD      ,  // opcode[31:30] = 3
		&CPU::ST       , &CPU::STB      , &CPU::STH      , &CPU::STD      ,
		&CPU::UNIMP    , &CPU::LDSB     , &CPU::LDSH     , &CPU::UNIMP    ,
		&CPU::UNIMP    , &CPU::LDSTUB   , &CPU::UNIMP    , &CPU::SWAP     ,
		&CPU::LDA      , &CPU::LDUBA    , &CPU::LDUHA    , &CPU::LDDA     ,
		&CPU::STA      , &CPU::STBA     , &CPU::STHA     , &CPU::STDA     ,
		&CPU::UNIMP    , &CPU::LDSBA    , &CPU::LDSHA    , &CPU::UNIMP    ,
		&CPU::UNIMP    , &CPU::LDSTUBA  , &CPU::UNIMP    , &CPU::SWAPA    ,
		&CPU::LDF      , &CPU::LDFSR    , &CPU::UNIMP    , &CPU::LDDF     ,
		&CPU::STF      , &CPU::STFSR    , &CPU::STDFQ    , &CPU::STDF     ,
		&CPU::UNIMP    , &CPU::UNIMP    , &CPU::UNIMP    , &CPU::UNIMP    ,
		&CPU::UNIMP    , &CPU::UNIMP    , &CPU::UNIMP    , &CPU::UNIMP    ,
		&CPU::LDC      , &CPU::LDCSR    , &CPU::UNIMP    , &CPU::LDDC     ,
		&CPU::STC      , &CPU::STCSR    , &CPU::STDCQ    , &CPU::STDC     ,
		&CPU::UNIMP    , &CPU::UNIMP    , &CPU::UNIMP    , &CPU::UNIMP    ,
		&CPU::CASA    , &CPU::UNIMP    , &CPU::UNIMP    , &CPU::UNIMP 
		};

#ifdef CPU_VERBOSE
    /////////////////////////
    // ALU instruction labels (only needed for verbose trace output)
    static constexpr const char* op_byte[32] = {
       "add     ", "and     ", "or      ", "xor     ",
       "sub     ", "andn    ", "orn     ", "xnor    ",
       "addx    ", "unip    ", "umul    ", "smul    ",
       "subx    ", "unip    ", "udiv    ", "sdiv    ",
       "addcc   ", "andcc   ", "orcc    ", "xorcc   ",
       "subcc   ", "andncc  ", "orncc   ", "xnorcc  ",
       "addxcc  ", "unipcc  ", "umulcc  ", "smulcc  ",
       "subxcc  ", "unipcc  ", "udivcc  ", "sdivcc  "};

    static constexpr const char* cond_byte[16] = {
       "n      ", "e      ", "le     ", "l       ",
       "leu    ", "cs     ", "neg    ", "vs      ",
       "a      ", "ne     ", "g      ", "ge      ",
       "gu     ", "cc     ", "pos    ", "vc      "
    };

    static constexpr const char* trap_str[64] = {
        "Software Reset",
        "Instruction Access Exception",
        "Illegal Instruction",
        "Privileged Instruction",
        "FP Disabled",
        "Window Overflow",
        "Window Underflow",
        "Memory Address not Aligned",
        "FP Exception",
        "Data Access Exception",
        "Tag Overflow",
        "Watchpoint Detect",
        "NO TRAP", "NO TRAP", "NO TRAP", "NO TRAP",
        "NO TRAP", "NO TRAP", "NO TRAP", "NO TRAP",
        "NO TRAP", "NO TRAP", "NO TRAP", "NO TRAP",
        "NO TRAP", "NO TRAP", "NO TRAP", "NO TRAP",
        "NO TRAP", "NO TRAP", "NO TRAP", "NO TRAP",
        "R Register Access Error",
        "Instruction Access Error",
        "NO TRAP",
        "NO TRAP",
        "CP Disabled",
        "Unimplemented Flush",
        "NO TRAP",
        "NO TRAP",
        "CP Exception",
        "Data Access Error",
        "Divide by Zero",
        "Data Store Error",
        "Data Access MMU Miss",
        "NO TRAP",
        "NO TRAP",
        "NO TRAP",
        "NO TRAP", "NO TRAP", "NO TRAP", "NO TRAP",
        "NO TRAP", "NO TRAP", "NO TRAP", "NO TRAP",
        "NO TRAP", "NO TRAP", "NO TRAP", "NO TRAP",
        "Instruction Access MMU Miss",
        "NO TRAP", "NO TRAP", "NO TRAP"
    };
#endif // CPU_VERBOSE



};


