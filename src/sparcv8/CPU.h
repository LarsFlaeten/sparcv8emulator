#ifndef CPU_H
#define CPU_H

#include <cstdint>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <vector>
#include <map>

#ifndef __cpp_lib_format
  // std::format polyfill using fmtlib
  #include <fmt/core.h>
  namespace std {
  using fmt::format;
  }
#else
  #include <format>
#endif

/* Declare integer datatypes for each number of bits */
typedef std::uint_least8_t  u8;  typedef std::int_least8_t  s8;
typedef std::uint_least16_t u16; typedef std::int_least16_t s16;
typedef std::uint_least32_t u32; typedef std::int_least32_t s32;
typedef std::uint_least64_t u64; typedef std::int_least64_t s64;


constexpr u32 NO_USER_BREAK =     0xffffffff;
constexpr u32 BREAK_SINGLE_STEP = 0xfffffffe;


// Define this to use LEON specific ASI Assignments
#define CONFIG_SPARC_LEON

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

// Defer structure definition
class CPU;
struct  DecodeStruct;

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

    u32 PC;                  // Current program counter value
    u32 nPC;                 // Next program counter value
    u32 PSR;                 // Program status register value
    pPSR_t p;                   // Pointer to structured version of PSR
    WriteBackType wb_type;             // Write back flag
    u32 value;               // Write back value
    u32 value1;              // Second (optional) write back value
};

enum TerminateReason {
    NORMAL = -1,
    INSTRUCTION = 0,
    BREAK = 1,
    STEP = 2,
    TRAP_CONDITIONAL = 3,
    UNIMPLEMENTED = 4,
    RECV_SIGINT = 5
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
        u32 PC;
        u32 nPC;

        u32 PSR;
        u32 IRL;

        u32 TrapType;
        u32 TBR;
        u32 WIM;

        u32 Y;

        u32 cpu_id;
        u32 fpu_fsr; // FPU state register
        ///////////////////////
        // Registers
        u32 Globals [9];              // Global[8] is a pseudo register used only for SWAP
        u32 Locals  [NWINDOWS * 8];
        u32 Outs    [NWINDOWS * 8];

        u32 *pSwapReg = &Globals[8];



        // Emulator control and debugging
       	std::ostream& os;
        bool running;
        bool verbose;       
        bool single_step;
        RunSummary rs;
        std::map<u32, bool>    breakpoints; 

        u32 breakpoint;
        std::string DispRegStr (u32 regnum);

        std::function<void()> bus_tick_func;
        std::function<void()> breakpoint_func;
        bool _interrupt;         
   public:
        CPU(std::ostream& out = std::cout) : cpu_id(0), os(out), running(false), verbose(false), single_step(false), breakpoint(NO_USER_BREAK), _interrupt(false)
        {  }

        // Main execution flow methods
 
        void Reset(u32 entry_va);

        u32  Run(u32 ExecCount = 0, RunSummary* _rs = nullptr);
        
        void IFetch(u32 virt_addr, u32& opcode);

        void Decode(pDecode_t d);
        
        void WriteBack (const pDecode_t d);

        void Trap (pDecode_t d, u32 trap_no); 


        // Read/write registers
        void ReadReg (const u32 reg_no, u32 * const value);
        //u32  ReadRegsAll (const int reg_base); 
        void WriteReg (const u32 value, const u32 reg_no);
        //void WriteRegAll (const int RegBase, const u32 WriteValue);
        int  MemRead(const u32 va, const int bytes, const u32 rd, const int signext);    
        int  MemWrite(const u32 va, const int bytes, const u32 rd);
        //u32  GetRegBase (const u32 reg_no); 

        // Get/Set multi-core id:
        void SetId(u32 value) { cpu_id = value; }
        u32 GetId() const { return cpu_id; }
        // State Accessors
        u32 GetPSR() const {return PSR;}
        u32 GetWIM() const {return WIM;}
        u32 GetTBR() const {return TBR;}
        u32 GetY() const {return Y;}
        u32 GetPC() const { return PC;  }
        u32 GetnPC() const { return nPC; }
        u32 GetIRL() const { return IRL; }
        u32 GetTrapType() const { return TrapType; }
        void SetIRL(u32 irl)  { IRL = (irl & 0xf); }
        u32 GetFSR() const { return fpu_fsr; }
        void SetFSR(u32 _fsr)  { fpu_fsr = _fsr; }
        bool IsRunning() const {return running;}
        // MMU Fault handling
        void handleMMUFault(pDecode_t d);

        // Helper CC test
        int TestCC (pDecode_t d);


        // Emulator control and debugging
        void interrupt() { _interrupt = true; }
        void    SetSingleStep(bool v) { single_step = v; }
        void    SetVerbose(bool v) { verbose = v; }
        bool    GetVerbose() const {return verbose;}
        std::ostream& GetOStream() const { return os; }
        void    AddUserBreakpoint(u32 bp) { breakpoints[bp] = true; }
        bool    RemoveUserBreakpoint(u32 bp) { 
            auto i = breakpoints.erase(bp); 
            if(i==0) 
                return false; 
            else 
                return true;}
        const std::map<u32, bool>& GetUserBreakpoints() const {return breakpoints;}
        void    RegisterDump (bool transpose = false); 
        void    DispReadReg (const u32 reg_no, u32 *value); 
        void    RegisterBusTickFunction(std::function<void()> f) {
            bus_tick_func = f;
        }
        void    RegisterBreakpointFunction(std::function<void()> f) {
            breakpoint_func = f;
        }
    public:
        

	private:
	// Common functions
    // Implemented in CPU_functions.cpp
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
    // Extended functions
    // Implemented in CPU_functionsExt.cpp
    void STA_impl   (pDecode_t d);
    void LDA_impl   (pDecode_t d);

    //////////////////////////////////////
    // LEON3 Extensions
    //
    void CASA    (pDecode_t d);

    //////////////////////////////////////
    // FPU Extensions
    //
    void fpu_STFSR    (pDecode_t d);
    void fpu_LDFSR    (pDecode_t d);




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
    const std::string  OpByte[32] = {
       "add     ", "and     ", "or      ", "xor     ",
       "sub     ", "andn    ", "orn     ", "xnor    ",
       "addx    ", "unip    ", "umul    ", "smul    ",
       "subx    ", "unip    ", "udiv    ", "sdiv    ",
       "addcc   ", "andcc   ", "orcc    ", "xorcc   ",
       "subcc   ", "andncc  ", "orncc   ", "xnorcc  ",
       "addxcc  ", "unipcc  ", "umulcc  ", "smulcc  ",
       "subxcc  ", "unipcc  ", "udivcc  ", "sdivcc  "};

    const std::string CondByte [16] = {
       "n      ", "e      ", "le     ", "l       ",
       "leu    ", "cs     ", "neg    ", "vs      ",
       "a      ", "ne     ", "g      ", "ge      ",
       "gu     ", "cc     ", "pos    ", "vc      "
    };


    const std::string TrapStr [64] = {
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
