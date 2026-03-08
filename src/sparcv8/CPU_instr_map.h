// SPDX-License-Identifier: MIT
#pragma once

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

// Floating point instructions:
#ifdef FPU_IMPLEMENTED
#define LDF                LDF_impl
#define LDDF               LDDF_impl
#define LDFSR              LDFSR_impl
#define STF                STF_impl
#define STDF               STDF_impl
#define STFSR              STFSR_impl
#define STDFQ              STDFQ_impl
#define FBFCC              FBFCC_impl
#define FPOP1              FOP1_impl
#define FPOP2              FOP2_impl
#else
#define LDF                UNIMP
#define LDDF               UNIMP
#define LDFSR              UNIMP
#define STF                UNIMP
#define STDF               UNIMP
#define STFSR              UNIMP
#define STDFQ              UNIMP
#define FBFCC              UNIMP
#define FPOP1              UNIMP
#define FPOP2              UNIMP
#endif



