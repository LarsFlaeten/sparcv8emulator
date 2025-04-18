#ifndef _dis_h_
#define _dis_h_

#include "sparcv8/CPU.h"

void disDecode(u32 PC, u32 opcode);

// Format 1 instruction
static std::string format1_str = "call    ";

// Format 2 instructions
static std::string format2_str[8] = {
    "unimp   "    , "unimp   "    , "b"           , "unimp   ",
    "sethi   "    , "unimp   "    , "fbfcc   "    , "cbccc   "};

// Format 3 instructions 
static std::string format3_str[128] = {
    "add     ", "and     ", "or      ", "xor     ",  
    "sub     ", "andn    ", "orn     ", "xnor    ",
    "addx    ", "unimp   ", "umul    ", "smul    ",
    "subx    ", "unimp   ", "udiv    ", "sdiv    ",
    "addcc   ", "andcc   ", "orcc    ", "xorcc   ",
    "subcc   ", "andncc  ", "orncc   ", "xnorcc  ",
    "addxcc  ", "unimp   ", "umulcc  ", "smulcc  ",
    "subxcc  ", "unimp   ", "udivcc  ", "sdivcc  ",
    "taddcc  ", "tsubcc  ", "taddcctv", "tsubcctv",
    "mulscc  ", "sll     ", "srl     ", "sra     ",
    "rd      ", "rd      ", "rd      ", "rd      ",
    "unimp   ", "unimp   ", "unimp   ", "unimp   ",
    "wr      ", "wr      ", "wr      ", "wr      ",
    "fpop1   ", "fpop2   ", "cpop1   ", "cpop2   ",
    "jmpl    ", "rett    ", "ticc    ", "flush   ",
    "save    ", "restore ", "unimp   ", "unimp   ",
    "ld      ", "ldub    ", "lduh    ", "ldd     ",  
    "st      ", "stb     ", "sth     ", "std     ",
    "unimp   ", "ldsb    ", "ldsh    ", "unimp   ",
    "unimp   ", "ldstub  ", "unimp   ", "swap    ",
    "lda     ", "lduba   ", "lduha   ", "ldda    ",
    "sta     ", "stba    ", "stha    ", "stda    ",
    "unimp   ", "ldsba   ", "ldsha   ", "unimp   ",
    "unimp   ", "ldstuba ", "unimp   ", "swapa   ",
    "ldf     ", "ldfsr   ", "unimp   ", "lddf    ",
    "stf     ", "stfsr   ", "stdfq   ", "stdf    ",
    "unimp   ", "unimp   ", "unimp   ", "unimp   ",
    "unimp   ", "unimp   ", "unimp   ", "unimp   ",
    "ldc     ", "ldcsr   ", "unimp   ", "lddc    ",
    "stc     ", "stcsr   ", "stdcq   ", "stdc    ",
    "unimp   ", "unimp   ", "unimp   ", "unimp   ",
    "unimp   ", "unimp   ", "unimp   ", "unimp   "
    };



#endif
