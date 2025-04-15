#ifndef _CPU_DEFINES_H_
#define _CPU_DEFINES_H_

#include <exception>

class not_implemented_exception : public std::exception
{
public:
virtual char const * what() const noexcept { return "Sparc function not implemented"; }
};

class not_implemented_leon_exception : public std::exception
{
    std::string w;
public:
    not_implemented_leon_exception(std::string msg) : w(msg) {}

    virtual char const * what() const noexcept { return w.c_str(); }
};



#define NWINDOWS                           8

#define SPARC_SOFTWARE_RESET               0x00
#define SPARC_INSTRUCTION_ACCESS_EXCEPTION 0x01
#define SPARC_ILLEGAL_INSTRUCTION          0x02
#define SPARC_PRIVILEGED_INSTRUCTION       0x03
#define SPARC_FP_DISABLED                  0x04
#define SPARC_WINDOW_OVERFLOW              0x05
#define SPARC_WINDOW_UNDERFLOW             0x06
#define SPARC_MEMORY_ADDR_NOT_ALIGNED      0x07
#define SPARC_FP_EXCEPTION                 0x08
#define SPARC_DATA_ACCESS_EXCEPTION        0x09
#define SPARC_TAG_OVERFLOW                 0x0a
#define SPARC_WATCHPOINT_DETECT            0x0b
#define SPARC_R_REGISTER_ACCESS_ERROR      0x20
#define SPARC_INSTRUCTION_ACCESS_ERROR     0x21
#define SPARC_CP_DISABLED                  0x24
#define SPARC_UNIMPLEMENTED_FLUSH          0x25
#define SPARC_CP_EXCEPTION                 0x28
#define SPARC_DATA_ACCESS_ERROR            0x29
#define SPARC_DIVISION_BY_ZERO             0x2A
#define SPARC_DATA_STORE_ERROR             0x2B
#define SPARC_DATA_ACCESS_MMU_MISS         0x2C
#define SPARC_INSTRUCTION_ACCESS_MMU_MISS  0x3C

#define SPARC_INTERRUPT                    0x10 
#define SPARC_TRAP_INSTRUCTION             0x80

#define CC_NEGATIVE                        3
#define CC_ZERO                            2
#define CC_OVERFLOW                        1
#define CC_CARRY                           0

#define PSR_ENABLE_TRAPS                   5
#define PSR_PREV_SUPER_MODE                6
#define PSR_SUPER_MODE                     7
#define PSR_INTERRUPT_LEVEL_0              8
#define PSR_INTERRUPT_LEVEL_3              11
#define PSR_ENABLE_FLOATING_POINT          12
#define PSR_ENABLE_COPROCESSOR             13
#define PSR_VER                            24
#define PSR_IMPL                           28


#define PSR_CC_NEGATIVE                    23
#define PSR_CC_ZERO                        22
#define PSR_CC_OVERFLOW                    21
#define PSR_CC_CARRY                       20

#define INREG7             0x1f
#define INREG6             0x1e
#define INREG5             0x1d
#define INREG4             0x1c
#define INREG3             0x1b
#define INREG2             0x1a
#define INREG1             0x19
#define INREG0             0x18
#define LOCALREG7          0x17
#define LOCALREG6          0x16
#define LOCALREG5          0x15
#define LOCALREG4          0x14
#define LOCALREG3          0x13
#define LOCALREG2          0x12
#define LOCALREG1          0x11
#define LOCALREG0          0x10
#define OUTREG7            0x0f
#define OUTREG6            0x0e
#define OUTREG5            0x0d
#define OUTREG4            0x0c
#define OUTREG3            0x0b
#define OUTREG2            0x0a
#define OUTREG1            0x09
#define OUTREG0            0x08
#define GLOBALREG7         0x07
#define GLOBALREG6         0x06
#define GLOBALREG5         0x05
#define GLOBALREG4         0x04
#define GLOBALREG3         0x03
#define GLOBALREG2         0x02
#define GLOBALREG1         0x01
#define GLOBALREG0         0x00

#define MEM_SIZE_BITS      (u64)30
#define ADDR_MASK          (((u64)1 << MEM_SIZE_BITS)-(u64)1)
#define LOBITS1            0x01
#define LOBITS2            0x03
#define LOBITS3            0x07
#define LOBITS4            0x0f
#define LOBITS5            0x1f
#define LOBITS6            0x3f
#define LOBITS7            0x7f
#define LOBITS8            0xff
#define LOBITS9            0x1ff
#define LOBITS13           0x1fff
#define LOBITS16           0xffff
#define LOBITS22           0x3fffff
#define LOBITS30           0x3fffffff

#define LOWORDMASK         0xffffffff
#define LOHWORDMASK        0x0000ffff

#define BIT0               0x00000001
#define BIT1               0x00000002
#define BIT2               0x00000004
#define BIT3               0x00000008
#define BIT4               0x00000010
#define BIT5               0x00000020
#define BIT6               0x00000040
#define BIT7               0x00000080
#define BIT8               0x00000100
#define BIT9               0x00000200
#define BIT10              0x00000400
#define BIT11              0x00000800
#define BIT12              0x00001000
#define BIT13              0x00002000
#define BIT14              0x00004000
#define BIT15              0x00008000
#define BIT16              0x00010000
#define BIT17              0x00020000
#define BIT18              0x00040000
#define BIT19              0x00080000
#define BIT20              0x00100000
#define BIT21              0x00200000
#define BIT22              0x00400000
#define BIT23              0x00800000
#define BIT24              0x01000000
#define BIT25              0x02000000
#define BIT26              0x04000000
#define BIT27              0x08000000
#define BIT28              0x10000000
#define BIT29              0x20000000
#define BIT30              0x40000000
#define BIT31              0x80000000

#define FMTSTARTBIT        30
#define RDSTARTBIT         25
#define OP2STARTBIT        22
#define OP3STARTBIT        19
#define RS1STARTBIT        14
#define ISTARTBIT          13
#define ASISTARTBIT        5
#define RS2STARTBIT        0

#define TERMINATE_INST     0x00000000
#define NOP                0x01000000

#define GLOBALREG8         0x108

#define sign_ext22(_ARG) (_ARG | ((_ARG & (1 << 21)) ? 0xffc00000 : 0))
#define sign_ext13(_ARG) (_ARG | ((_ARG & (1 << 12)) ? 0xffffe000 : 0))
#define sign_ext7(_ARG)  (_ARG | ((_ARG & (1 << 6))  ? 0xffffff80 : 0))

#define FIRST_RS1_EVAL_IDX 56
#define TICC_IDX           58
#define STORE_DBL_IDX      71

// ASI Assignments
// https://patchwork.ozlabs.org/project/sparclinux/patch/1245664973-11520-2-git-send-email-konrad@gaisler.com/
#define ASI_M_UNA01         0x01   /* Same here... */
#define ASI_M_MXCC          0x02   /* Access to TI VIKING MXCC registers */
#define ASI_M_FLUSH_PROBE   0x03   /* Reference MMU Flush/Probe; rw, ss */
#ifndef CONFIG_SPARC_LEON
    #define ASI_M_MMUREGS       0x04   /* MMU Registers; rw, ss */
#else
    #define ASI_M_MMUREGS       0x19
#endif /* CONFIG_SPARC_LEON */
#define ASI_M_TLBDIAG       0x05   /* MMU TLB only Diagnostics */
#define ASI_M_DIAGS         0x06   /* Reference MMU Diagnostics */
#define ASI_M_IODIAG        0x07   /* MMU I/O TLB only Diagnostics */

#define ASI_LEON_DFLUSH 0x11
#define ASI_LEON_MMUFLUSH 0x18
#define ASI_LEON_BYPASS 0x1c


#endif
