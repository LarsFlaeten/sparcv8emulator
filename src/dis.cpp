//=============================================================
//                                                                         
// Copyright (c) 2004 Simon Southwell. All rights reserved.
//                                                                         
// Date: 13th October 2004
//
// This file is part of sparc_iss.
//
// sparc_iss is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// sparc_iss is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// $Id: dis.c,v 1.3 2016-10-18 05:52:44 simon Exp $
// $Source: /home/simon/CVS/src/cpu/sparc/src/dis.c,v $
//
//=============================================================

#include <stdio.h>
#include <string>

#include "dis.h"

//extern FILE *ofp;
FILE* ofp = stdout;

static std::string cond_byte [16] = {
   "n  ", "e  ", "le ", "l  ",
   "leu", "cs ", "neg", "vs ",
   "a  ", "ne ", "g  ", "ge ",
   "gu ", "cc ", "pos", "vc"
};

//------------------------------------------------------------------------
//
void disDispDecode (const pDecode_t d) {

    fprintf(ofp, "  opcode    = %08x\n", d->opcode);
    fprintf(ofp, "  rs1       = %08x\n", d->rs1);
    fprintf(ofp, "  rs1_value = %08x\n", d->rs1_value);
    fprintf(ofp, "  ev        = %08x\n", d->ev);
    fprintf(ofp, "  rd        = %08x\n", d->rd);
    fprintf(ofp, "  imm_rs2   = %08x\n", d->imm_disp_rs2);
    fprintf(ofp, "  op_2_3    = %08x\n", d->op_2_3);
    fprintf(ofp, "  i         = %08x\n", d->i);
    fprintf(ofp, "  PC        = %08x\n", d->pc);
    fprintf(ofp, "  nPC       = %08x\n", d->npc);
    fprintf(ofp, "  PSR       = %08x\n", d->psr);
    fprintf(ofp, "  wb_type   = %08x\n", d->wb_type);
    fprintf(ofp, "  value     = %08x\n", d->value);
    fprintf(ofp, "  value1    = %08x\n", d->value1);
}

//------------------------------------------------------------------------

static void disShowRegName (const u32 regnum) 
{
    switch ((regnum >> 3) & LOBITS3) {
    case 0: 
        fprintf(ofp, "%%g%0d", (regnum & LOBITS3));
        break;
    case 1: 
        if ((regnum & LOBITS3) == 6)
            fprintf(ofp, "%%sp");
        else
            fprintf(ofp, "%%o%0d", (regnum & LOBITS3));
        break;
    case 2: 
        fprintf(ofp, "%%l%0d", (regnum & LOBITS3));
        break;
    case 3: 
        if ((regnum & LOBITS3) == 6)
            fprintf(ofp, "%%fp");
        else
            fprintf(ofp, "%%i%0d", (regnum & LOBITS3));
        break;
    case 4:
        fprintf(ofp, "%%y");
        break;
    case 5:
        fprintf(ofp, "%%psr");
        break;
    case 6:
        fprintf(ofp, "%%wim");
        break;
    case 7:
        fprintf(ofp, "%%tbr");
        break;
    }
}


//------------------------------------------------------------------------

//static void disIfetch (const u64 physaddr, u32 * const inst) 
//{
/*    u64 PA = physaddr & ADDR_MASK;

    if ((PA & (u64)3) != 0) {
        fprintf(stderr, "Misaligned instruction fetch in main\n");
        exit(RUNTIME_ERROR);
    }

    if ((PA & ~ADDR_MASK) != 0) {
        fprintf(stderr, "MAIN : Trying to read instructions out of range! PA=%x\n", PA);
        dump_regs();
        exit(RUNTIME_ERROR);
    }

    *inst = Memory[PA >> (u64)2];*/
//}

//------------------------------------------------------------------------

static void disPrintOpcode(u32 PC, u32 opcode, std::string f, std::string c) {
    fprintf(ofp, "%12x:  %02x %02x %02x %02x\t", PC, (opcode & 0xff000000) >> 24,
                                                        (opcode & 0x00ff0000) >> 16,
                                                        (opcode & 0x0000ff00) >> 8,
                                                        (opcode & 0x000000ff));
    fprintf(ofp, "   %s%s\t", f.c_str(), c.c_str());
}

//------------------------------------------------------------------------
// Main instruction decode
//
void disDecode(u32 PC, u32 opcode)
{
    std::string function, trimmed;
    int is_mov, is_cmp, is_rd, is_wr, is_sethi_or, is_clr;
    u32 imm_disp_rs2;
    static u32 last_sethi = 0, last_sethi_value, last_sethi_reg;

    u32 fmt_bits = (opcode >> FMTSTARTBIT) & LOBITS2;
    u32 op2      = (opcode >> OP2STARTBIT) & LOBITS3;
    u32 op3      = (opcode >> OP3STARTBIT) & LOBITS6;
    u32 rd       = (opcode >> RDSTARTBIT)  & LOBITS5;
    //u32 op_2_3   = (opcode >> OP3STARTBIT) & LOBITS6;
    u32 rs1      = (opcode >> RS1STARTBIT) & LOBITS5;
    u32 i        = (opcode >> ISTARTBIT)   & LOBITS1;

    switch (fmt_bits) {
    // CALL
    case 1:  
        function     = format1_str;
        disPrintOpcode (PC, opcode, function, "");
        imm_disp_rs2 = opcode & LOBITS30;
        fprintf(ofp, "0x%08x\n", (int)PC+(int)(imm_disp_rs2*4));
        last_sethi = 0;
        break;

    // SETHI, Branches
    case 0:  
        function     = format2_str[op2];                     
        imm_disp_rs2 = (opcode & LOBITS22);

        if (function == "unimp   ") {
            disPrintOpcode(PC, opcode, function, "");
            fprintf(ofp, "0x%x\n", opcode);
            last_sethi = 0;
        } else if (function == "sethi   ") {
            if (opcode == NOP) {
                function = "nop     ";
                last_sethi = 0;
            } else {
                last_sethi_value = imm_disp_rs2<<10;
                last_sethi_reg = rd;
                last_sethi = 1;
            }

            disPrintOpcode(PC, opcode, function, "");

            if (opcode != NOP) {
                fprintf(ofp, "%%hi(0x%x), ", last_sethi_value);
                disShowRegName(rd & LOBITS5);
            }
            fprintf(ofp, "\n");
        } else {
            disPrintOpcode(PC, opcode, function, cond_byte[rd & 0xf]);
            fprintf(ofp, "\t0x%x\n", (int)PC + (int)(sign_ext22(imm_disp_rs2*4)));
            last_sethi = 0;
        }
        break;

    // Memory accesses
    case 3:  
        function     = format3_str[op3 + ((fmt_bits & 1) << 6)];
        imm_disp_rs2 = (opcode >> RS2STARTBIT) & LOBITS13;



        disPrintOpcode(PC, opcode, function, "");

        trimmed = function;
        trimmed.erase(trimmed.find_last_not_of(' ') + 1);

        if (trimmed == "st") {
            disShowRegName(rd & LOBITS5);
            fprintf(ofp, ", ");
        }
        
        fprintf (ofp, "[");
        disShowRegName(rs1 & LOBITS5);
        if (imm_disp_rs2) {
            fprintf(ofp, " + ");
            if (i == 0) 
                disShowRegName(imm_disp_rs2 & LOBITS5);
            else
                fprintf(ofp, "%d", imm_disp_rs2);
        }
        fprintf(ofp, "]");

        if (trimmed == "ld") {
            fprintf(ofp, ", ");
            disShowRegName(rd & LOBITS5);
        }
        fprintf(ofp, "\n");
        last_sethi = 0;
        
        break;

    // ALU etc.
    case 2:  
        function        = format3_str[op3 + ((fmt_bits & 1) << 6)];
        imm_disp_rs2 = (opcode >> RS2STARTBIT) & LOBITS13;

        trimmed = function;
        trimmed.erase(trimmed.find_last_not_of(' ') + 1);
        
        is_cmp = (function != "subcc   " && !rd);
        is_wr = !(trimmed == "wr");
        is_rd = !(trimmed == "rd");
        is_mov = (!(function == "or      ") && !rs1) || (is_wr & !rs1);
        is_clr = !(function == "or      ") && !rs1 && i && imm_disp_rs2 == 0;
        is_sethi_or = ((function != "or      ") && (rs1 == last_sethi_reg) && (rs1 == last_sethi_reg) && i);

        if (is_clr)
            function = "clr     ";
        else if (is_mov) 
            function = "mov     ";
        else if (is_cmp)
            function = "cmp     ";
        if (is_rd) 
            rs1 = ((4 + (op3 & LOBITS2)) << 3) & LOBITS6;
        else if (is_wr)
            rd = ((4 + (op3 & LOBITS2)) << 3) & LOBITS6;

        disPrintOpcode(PC, opcode, function, "");

        if (!is_mov && !is_clr) {
            disShowRegName(rs1);
            fprintf(ofp, ", ");
        }
        if (!is_rd) {
            if (i == 0)
                disShowRegName(imm_disp_rs2 & LOBITS5);
            else if (!is_clr) {
                if (is_sethi_or)
                    fprintf(ofp, "0x%x", (int)sign_ext13(imm_disp_rs2));
                else
                    fprintf(ofp, "%d", (int)sign_ext13(imm_disp_rs2));
            }
        }
        if (!is_cmp) {
            if (!is_rd && !is_clr)
                fprintf(ofp, ", ");
            disShowRegName(rd);
        }

        if (is_sethi_or)
            fprintf(ofp, "\t! 0x%x", last_sethi_value | imm_disp_rs2);
        fprintf(ofp, "\n");
        last_sethi = 0;

        break;
   }
}

//------------------------------------------------------------------------
// Main disassemble execution loop
//
/*
u64 disRun(const u32 ExecCount) 
{
    u32 count = ExecCount;
    u32 opcode;

    // Start executing program
    while ((ExecCount == 0) ? 1 : (count--)) {

        // Process instruction ...

        // ---- IFetch ----
        disIfetch(PC, &opcode);

        // ---- Decode ----
        disDecode (opcode);

        PC += 4;
    }

    return ExecCount;
}
*/
