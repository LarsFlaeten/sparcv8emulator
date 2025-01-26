#ifndef _CPU_INSTR_MAP_H
#define _CPU_INSTR_MAP_H

// Map instructions to common functions

#define ANDCC              AND
#define ANDN               AND
#define ANDNCC             AND
#define OR                 AND
#define ORCC               AND
#define ORN                AND
#define ORNCC              AND
#define XOR                AND
#define XORCC              AND
#define XNOR               AND
#define XNORCC             AND
#define ADDCC              ADD
#define ADDX               ADD
#define ADDXCC             ADD
#define TADDCC             ADD
#define TADDCCTV           ADD
#define SUB                ADD
#define SUBCC              ADD
#define SUBX               ADD
#define SUBXCC             ADD
#define TSUBCC             ADD
#define TSUBCCTV           ADD
#define UMUL               MUL
#define UMULCC             MUL
#define SMUL               MUL
#define SMULCC             MUL
#define UDIV               DIV
#define SDIV               DIV
#define UDIVCC             DIV
#define SDIVCC             DIV

// Point all the unimplemented instructions to the UNIMP function

#define LDSBA              UNIMP
#define LDSHA              UNIMP
#define LDUBA              UNIMP
#define LDUHA              UNIMP
#define LDA                LDA_impl
#define LDDA               UNIMP
#define STBA               STA_impl
#define STHA               STA_impl
#define STA                STA_impl
#define STDA               STA_impl
#define LDSTUBA            UNIMP
#define SWAPA              UNIMP
#define LDF                UNIMP //LDF_impl
#define LDDF               UNIMP
#define LDFSR              UNIMP //LDFSR_impl
#define STF                UNIMP //STF_impl
#define STDF               UNIMP
#define STFSR              UNIMP //STFSR_impl
#define STDFQ              UNIMP
#define FBFCC              UNIMP // FBFCC_impl
#define FPOP1              UNIMP // FOP1_impl
#define FPOP2              UNIMP //FOP2_impl
#define LDC                UNIMP
#define LDDC               UNIMP
#define LDCSR              UNIMP
#define STC                UNIMP
#define STDC               UNIMP
#define STCSR              UNIMP
#define STDCQ              UNIMP
#define CBCCC              UNIMP
#define CPOP1              UNIMP
#define CPOP2              UNIMP



#endif

