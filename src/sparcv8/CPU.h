#ifndef CPU_H
#define CPU_H

#include <cstdint>
#include <iostream>
#include <functional>
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

// Defer structure definition
class CPU;
class IRQMP;
class MMU;
#include "MMU.h"
#include "../peripherals/IRQMP.h"

struct  DecodeStruct;
#include "../gdb/gdb_stub.hpp"


typedef struct DecodeStruct *pDecode_t;
typedef void (*p_func) (pDecode_t);
typedef std::function<void(CPU*,pDecode_t)> pc_func;

typedef std::function<void()> bus_tick_func;


enum WriteBackType {
    NO_WRITEBACK =      0,
    WRITEBACKREG =      1
};


struct DecodeStruct {
    u32 opcode;              // Instruction opcode
    std::function<void(CPU*, pDecode_t)> function;            // Pointer to inst.c function
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

        u32 cpu_id;
        u32 fsr; // FPU state register
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
        bool verbose = false;
        bool break_on_timer_interrupt = false; 
        bool power_down_enabled = false; 
        std::atomic<bool> powerdown_flag{false};
        std::atomic<bool> wakeup_flag{false};

        std::mutex power_mtx;
        std::condition_variable power_cv;

        
        RunSummary rs;
        GdbStub*    gdb_stub = nullptr;
        
        std::string DispRegStr (u32 regnum);

        std::function<void()> bus_tick_func{};
        
        bool _interrupt; 
        MMU mmu;  
        IRQMP& intc;      
   public:
        CPU(MCtrl& mctrl, IRQMP& intc, std::ostream& out = std::cout) : cpu_id(0), os(out), _interrupt(false), mmu(mctrl), intc(intc)
        { 
            // Set Cache control regs as TSIM does
            mmu.SetCCR(0x00020000);
            mmu.SetICCR(0x10220008);
            mmu.SetDCCR(0x18220008);
            
        }

        CPU() = delete;
        CPU(const CPU&) = delete;
        CPU& operator=(const CPU&) = delete;
        
        // Main execution flow methods
 
        void reset(u32 entry_va);

        u32  run(u32 ExecCount = 0, RunSummary* _rs = nullptr);
        
        bool instr_fetch(u32 virt_addr, pDecode_t d);

        inline void excute_one(pDecode_t d) {
            // ---- Decode ----
            decode (d);

            // ---- Execute ----
            d->function(this, d);

            // ---- Writeback ----
            write_back(d);
        }

        void decode(pDecode_t d);
        
        void write_back (const pDecode_t d);

        void trap (pDecode_t d, u32 trap_no); 


        void nop() { return; } // Just for the sake of it...


        // Read/write registers
        void read_reg (const u32 reg_no, u32 * const value);
        //u32  ReadRegsAll (const int reg_base); 
        void write_reg (const u32 value, const u32 reg_no);
        //void WriteRegAll (const int RegBase, const u32 WriteValue);
        int  mem_read(const u32 va, const int bytes, const u32 rd, const int signext);    
        int  mem_write(const u32 va, const int bytes, const u32 rd);
        //u32  GetRegBase (const u32 reg_no); 

        // Get/Set multi-core id:
        void set_cpu_id(u32 value) { cpu_id = value; }
        u32 get_cpu_id() const { return cpu_id; }
        // State Accessors
        u32 get_psr() const {return psr;}
        void set_psr(u32 value) { psr = value; }
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
        int test_cc (pDecode_t d);


        // Emulator control and debugging
        void interrupt() { _interrupt = true; }
        void    set_verbose(bool v) { verbose = v; }
        bool    get_verbose() const {return verbose;}
        MMU&    get_mmu() {return mmu;}
        std::ostream& get_ostream() const { return os; }
        void    set_gdb_stub(GdbStub* stub) {gdb_stub = stub;}
        void    enable_power_down(bool pd) {power_down_enabled = pd;}
        void    dump_regs (bool transpose = false); 
        //void    disp_read_reg (const u32 reg_no, u32 *value); 
        void    register_bus_tick_function(std::function<void()> f) {
            bus_tick_func = std::move(f);
        }

        void enter_powerdown();    // handles sleeping
        
        void wakeup();             // called by IRQMP


        void    set_break_on_timer_interrupt(bool val) {
            break_on_timer_interrupt = val;
        }
        
    public:
        

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

    /////////////////////////
    // ALU instruction labels
    //static constexpr char *
    const std::string  op_byte[32] = {
       "add     ", "and     ", "or      ", "xor     ",
       "sub     ", "andn    ", "orn     ", "xnor    ",
       "addx    ", "unip    ", "umul    ", "smul    ",
       "subx    ", "unip    ", "udiv    ", "sdiv    ",
       "addcc   ", "andcc   ", "orcc    ", "xorcc   ",
       "subcc   ", "andncc  ", "orncc   ", "xnorcc  ",
       "addxcc  ", "unipcc  ", "umulcc  ", "smulcc  ",
       "subxcc  ", "unipcc  ", "udivcc  ", "sdivcc  "};

    const std::string cond_byte [16] = {
       "n      ", "e      ", "le     ", "l       ",
       "leu    ", "cs     ", "neg    ", "vs      ",
       "a      ", "ne     ", "g      ", "ge      ",
       "gu     ", "cc     ", "pos    ", "vc      "
    };


    const std::string trap_str [64] = {
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



};


#endif
