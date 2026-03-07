#include "CPU.h"
#include "MMU.h"

#include <bitset>

#include <cmath>

void CPU::LDF_impl(pDecode_t d)
{
    bool super = (psr >> 7) & 0x1;

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} ldf       [{:#08x}], %f{}\n", d->pc, d->ev, d->rd);
#endif
    
    if (d->p->ef == 0) {
        trap(d,  SPARC_FP_DISABLED   );
    } else if (d->ev & LOBITS2) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        u32 value = 0;
        int ret = mmu.MemAccess<intent_load, 4>(d->ev, value, CROSS_ENDIAN, super);
        //os << "ldf [" << d->ev << "], " << *(float*)(&value) << "f (0x" << std::hex << value << std::dec << ") [" << std::bitset<32>(value) << "]\n";
        if((ret < 0) && !mmu.GetNoFault())
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        else {
            freg[d->rd] = value;
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}

void CPU::LDDF_impl(pDecode_t d)
{
    bool super = (psr >> 7) & 0x1;

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} lddf       [{:#08x}], %f{}\n", d->pc, d->ev, d->rd);
#endif
    
    if (d->p->ef == 0) {
        trap(d,  SPARC_FP_DISABLED   );
    } else if ((d->ev % 8)!=0) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else if (d->rd & 0x1) {
        // See sparc manual B2 suggestion of behavior when using uneven fp reg
        // set ftt to invalid_fp_register
        u8 ftt = 0x6; // invalid_fp_register, see Table 4-4
        fsr = (fsr & ~(7 << 14) ) | ((ftt & 7) << 14);
        trap(d, SPARC_FP_EXCEPTION);
   
    } else {
        u32 value_lo, value_hi = 0;
        int ret1 = mmu.MemAccess<intent_load, 4>(d->ev, value_hi, CROSS_ENDIAN, super);
        int ret2 = mmu.MemAccess<intent_load, 4>(d->ev+4, value_lo, CROSS_ENDIAN, super);
        
        if(((ret1 < 0) || (ret2 < 0 ))&& !mmu.GetNoFault())
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        else {
            freg[d->rd] = value_hi;
            freg[d->rd+1] = value_lo;
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}

// ------------------------------------------------
// Reads a new fsr value from memory address pointed by rs1 and rs2 and stores it into fsr
void CPU::LDFSR_impl (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} ldfsr    [{:#08x}] => $fsr\n", d->pc, d->ev);
#endif

    if (d->p->ef == 0) {
        trap (d,  SPARC_FP_DISABLED   );
    } else if (d->ev & LOBITS2) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        u32 va = d->ev;
        u32 value = 0; // To avoid warning
        u32 ret1 = mmu.MemAccess<intent_load,4>(va, value, CROSS_ENDIAN);
        if((ret1 < 0) && !mmu.GetNoFault())
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        else {
            fsr = value;
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}

void CPU::STF_impl(pDecode_t d)
{
    bool super = (psr >> 7) & 0x1;

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} stf       %f{}, [{:#08x}]\n", d->pc, d->rd, d->ev);
#endif
    
    if (d->p->ef == 0) {
        trap(d,  SPARC_FP_DISABLED   );
    } else if (d->ev & LOBITS2) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        u32 value = freg[d->rd];
        int ret = mmu.MemAccess<intent_store, 4>(d->ev, value, CROSS_ENDIAN, super);
        if((ret < 0) && !mmu.GetNoFault())
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        else {
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}

void CPU::STDF_impl(pDecode_t d)
{
    bool super = (psr >> 7) & 0x1;

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} stdf       %f{}, [{:#08x}]\n", d->pc, d->rd, d->ev);
#endif
    
    if (d->p->ef == 0) {
        trap(d,  SPARC_FP_DISABLED   );
    } else if ((d->ev % 8) != 0 ) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else if (d->rd & 0x1) {
        // See sparc manual B2 suggestion of behavior when using uneven fp reg
        // set ftt to invalid_fp_register
        u8 ftt = 0x6; // invalid_fp_register, see Table 4-4
        fsr = (fsr & ~(7 << 14) ) | ((ftt & 7) << 14);
        trap(d, SPARC_FP_EXCEPTION);
    } else {
        u32 value_hi = freg[d->rd];
        u32 value_lo = freg[d->rd+1];
        int ret1 = mmu.MemAccess<intent_store, 4>(d->ev, value_hi, CROSS_ENDIAN, super);
        int ret2 = mmu.MemAccess<intent_store, 4>(d->ev+4, value_lo, CROSS_ENDIAN, super);
        if(((ret1 < 0) || (ret2 < 0))&& !mmu.GetNoFault())
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        else {
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}


// ------------------------------------------------
// Reads the fsr value and stores it in memory address pointed by rs1 and rs2
void CPU::STFSR_impl (pDecode_t d)
{
#ifdef CPU_VERBOSE
        os << std::format("{:#08x} stfsr    {} = {:#08x}\n", d->pc, DispRegStr(d->ev), fsr);
#endif

    if (d->p->ef == 0) {
        trap (d,  SPARC_FP_DISABLED   );
    } else if (d->ev & LOBITS2) {
        trap(d, SPARC_MEMORY_ADDR_NOT_ALIGNED);
    } else {
        u32 va = d->ev;
        u32 value = fsr;
        u32 ret1 = mmu.MemAccess<intent_store,4>(va, value, CROSS_ENDIAN);
        if((ret1 < 0) && !mmu.GetNoFault())
            trap(d,  SPARC_DATA_ACCESS_EXCEPTION); 
        else {
            d->pc = d->npc;
            d->npc += 4;
        }
    }
}



void CPU::STDFQ_impl(pDecode_t d)
{
    throw std::runtime_error("STDFQ UNIMP");
}

std::string cond_str(u8 cond) {
    switch(cond) {
        case(0b1000): return("FBA");
        case(0b0000): return("FBN");
        case(0b0111): return("FBU");
        case(0b0110): return("FBG");
        case(0b0101): return("FBUG");
        case(0b0100): return("FBL");
        case(0b0011): return("FBUL");
        case(0b0010): return("FBLG");
        case(0b0001): return("FBNE");
        case(0b1001): return("FBE");
        case(0b1010): return("FBUE");
        case(0b1011): return("FBGE");
        case(0b1100): return("FBUGE");
        case(0b1101): return("FBLE");
        case(0b1110): return("FBULE");
        case(0b1111): return("FBO");
        default: return("?");

    }

}

std::string fcc_str(u8 fcc) {
    switch(fcc) {
        // fcc  | Relation
        // 0    | f rs1 = f rs2 E 0
        // 1    | f rs1 < f rs2 L 1
        // 2    | f rs1 > f rs2 G 2
        // 3    | f rs1 ? f rs2 U 3 (unordered)
        case(0): return ("E");
        case(1): return ("L");
        case(2): return ("G");
        case(3): return ("U");
        default: return ("?");
    }

}

// Branch on Floating-point Condition Codes Instructions
void CPU::FBFCC_impl(pDecode_t d)
{
    if (d->p->ef == 0) {
        trap (d,  SPARC_FP_DISABLED   );
        return;
    } 

    u8 cond = (d->opcode >> 25) & 0xf;
    
    u8 fcc = (fsr >> 10) & 0x3;

#ifdef CPU_VERBOSE
        os << std::format("{:#08x} fbfcc    cond = {}, fcc = {}\n", d->pc, cond_str(cond), fcc_str(fcc));
#endif


    bool branch = false;
    
    switch(cond)
    {
        // fcc  | Relation
        // 0    | f rs1 = f rs2 E 0
        // 1    | f rs1 < f rs2 L 1
        // 2    | f rs1 > f rs2 G 2
        // 3    | f rs1 ? f rs2 U 3 (unordered)
        case(0b1000): branch = true; break; // FBA
        case(0b0000): branch = false; break; // FBN
        case(0b0111): branch = (fcc == 3); break; // FBU
        case(0b0110): branch = (fcc == 2); break; // FBG
        case(0b0101): branch = (fcc == 3) || (fcc == 2); break; // FBUG
        case(0b0100): branch = (fcc == 1); break; // FBL
        case(0b0011): branch = (fcc == 3) || (fcc == 1); break;// FBUL
        case(0b0010): branch = (fcc == 1) || (fcc == 2); break;// FBLG
        case(0b0001): branch = (fcc != 0); break;// FBNE
        case(0b1001): branch = (fcc == 0); break;// FBE
        case(0b1010): branch = (fcc == 3) || (fcc == 0); break;// FBUE
        case(0b1011): branch = (fcc == 2) || (fcc == 0); break;// FBGE
        case(0b1100): branch = (fcc == 3) || (fcc == 2) || (fcc == 0); break;// FBUGE
        case(0b1101): branch = (fcc == 1) || (fcc == 0); break;// FBLE
        case(0b1110): branch = (fcc == 3) || (fcc == 1) || (fcc == 0); break;// FBULE
        case(0b1111): branch = (fcc == 0) || (fcc == 1) || (fcc == 2); break;// FBO
        default: throw std::runtime_error("Should never get here");

    }

    bool conditional = (cond != 0b0000) && (cond != 0b1000);
    bool annul = (d->opcode >> 29) & 0x1;
    if(branch && conditional)
        annul = false;
    
    // Same logic as BICC
    u32 temp = d->npc;
    if (branch)
        d->npc = d->pc + 4 * sign_ext22(d->imm_disp_rs2);
    else 
        d->npc += 4;
    d->pc = temp;

    if (annul) {
        d->pc = d->npc;
        d->npc += 4;
    }

}

std::string fops(u32 opf) {
    switch(opf) {
        // Single precision, rs1 fop rs2 type
        case(0b001001001): return "fMULs"; // fMULs
        case(0b001001101): return "fDIVs"; // fDIVs
        case(0b001000001): return "fADDs"; // fADDs
        case(0b001000101): return "fSUBs"; // fSUBs
        
        case(0b001010001): return "fcmps";
        case(0b001010101): return "fcmpes";
        
        case(0b000000001): return "fmovs";
        case(0b000000101): return "fnegs";
        case(0b000001001): return "fabss";
        
        case(0b000101001): return "fsqrts";

        default:
            return "?";
    }

}

void CPU::FOP1_impl(pDecode_t d)
{
    if (d->p->ef == 0) {
        trap (d,  SPARC_FP_DISABLED   );
        return;
    } 

    // Need to get rs2 and opf
    u8 rs2 = d->opcode & LOBITS5;
    u16 opf = (d->opcode >> 5) & LOBITS9;
    u8 ftt = 0;
    u32 v1, v2, vret;
    float f1, f2, fret;
    double fdtoi_input = 0;
    v1 = freg[d->rs1];
    v2 = freg[rs2];
    f1 = std::bit_cast<float>(v1);
    f2 = std::bit_cast<float>(v2);
    
    bool dbl = false;
    
    int32_t r;

    switch(opf) {
        // Single precision, rs1 fop rs2 type
        case(0b001001001): fret = f1 * f2; break; // fMULs
        case(0b001001101): fret = f1 / f2; break; // fDIVs
        case(0b001000001): fret = f1 + f2; break; // fADDs
        case(0b001000101): fret = f1 - f2; break; // fSUBs
        
        // Unary ops
        case(0b000000001): fret = f2; break; // fmovs
        case(0b000000101): fret = -f2; break; // fnegs
        case(0b000001001): fret = fabs(f2); break; // fabss
        
        case(0b000101001): fret = sqrtf(f2); break; // fsqrts

        case(0b011000100): r = std::bit_cast<int32_t>(v2); fret = (float)r; break; // fitos

        // fstoi - to make it simple, we bitcast the int via a float,
        // and get bit casted to u32 further down
        case(0b011010001): r = (int)f2; fret = std::bit_cast<float>(r); break; // fstoi
        
        // Fdtoi a bit more complicated. Get the double first, and the cast the resulting int
        // via float as for fstoi. And we need to check uneven regs for the double
        case(0b011010010):
            if(rs2 & 0x1) {
                // See sparc manual B2 suggestion of behavior when using uneven fp reg
                // set ftt to invalid_fp_register
                ftt = 0x6; // invalid_fp_register, see Table 4-4
                fsr = (fsr & ~(7 << 14) ) | ((ftt & 7) << 14);
                trap(d, SPARC_FP_EXCEPTION);
                return;
            }
            fdtoi_input = std::bit_cast<double>((((u64)freg[rs2]) << 32) | (freg[rs2+1]));
            // TODO: Check for overflow??
            r = (int)fdtoi_input;
            fret = std::bit_cast<float>(r); 
            break;
        
        // fdtos
        case(0b011000110):
            // Check input reg (double) for uneveness
            if(rs2 & 0x1) {
                // See sparc manual B2 suggestion of behavior when using uneven fp reg
                // set ftt to invalid_fp_register
                ftt = 0x6; // invalid_fp_register, see Table 4-4
                fsr = (fsr & ~(7 << 14) ) | ((ftt & 7) << 14);
                trap(d, SPARC_FP_EXCEPTION);
                return;
            }
            fdtoi_input = std::bit_cast<double>((((u64)freg[rs2]) << 32) | (freg[rs2+1]));
            // TODO: Check for overflow??
            fret = (float)fdtoi_input; 
            break;

        
        case(0b000101010):  // sqrtdf
        case(0b001000010):  // faddd
        case(0b001000110):  // fsubd
        case(0b001001010):  // fmuld
        case(0b001001110):  // fdivd
        case(0b001101001):  // fsmuld
        case(0b011001000):  // fitod
        case(0b011001001):  // fstod
            dbl = true;
            break;

        default:
            trap(d, SPARC_ILLEGAL_INSTRUCTION);
            return;
    }

#ifdef CPU_VERBOSE
    {
        // TODO: Unary ops not printed correct here
        os << std::format("{:#08x} fop1      %f{} {} %f{} -> %f{}\n", d->pc, d->rs1, fops(opf), rs2, d->rd);
        //os << "  (" << f1 << "f " << fops(opf) << " " << f2 << "f = " << fret << "f\n";
    }
#endif

    if(dbl) {
        // Check uneven regs for double ops. For smuld, fitod, fstod only check rd
        if(d->rd & 0x1 || ((opf!=0b001101001 && opf!=0b011001000 && opf!=0b011001001) && (rs2 & 0x1 || d->rs1 & 0x1))  ){
            // See sparc manual B2 suggestion of behavior when using uneven fp reg
            // set ftt to invalid_fp_register
            u8 ftt = 0x6; // invalid_fp_register, see Table 4-4
            fsr = (fsr & ~(7 << 14) ) | ((ftt & 7) << 14);
            trap(d, SPARC_FP_EXCEPTION);
            return;
        }

        double dret;
        u64 v1 = (((u64)freg[d->rs1]) << 32) | (freg[d->rs1+1]);
        u64 v2 = (((u64)freg[rs2]) << 32) | (freg[rs2+1]);
        u32 v2_s = freg[rs2];
        double d1 = std::bit_cast<double>(v1);
        double d2 = std::bit_cast<double>(v2);
        int32_t r;
        switch(opf) {
            case(0b000101010):  dret = sqrt(d2); break; // sqrtdf
            case(0b001000010):  dret = d1 + d2; break;  // faddd
            case(0b001000110):  dret = d1 - d2; break;// fsubd
            case(0b001001010):  dret = d1 * d2; break; // fmuld
            case(0b001001110):  dret = d1 / d2; break; // fdivd
            case(0b001101001):  dret = (double)f1 * (double)f2; break;// fsmuld
            case(0b011001000):  r = std::bit_cast<int32_t>(v2_s); dret = (double)r; break; // fitod
            case(0b011001001):  dret = (double)f2; break; // fstod
            default: throw std::logic_error("Should never get here");
        }
        u64 vdret = std::bit_cast<u64>(dret);
        freg[d->rd] = (u32)(vdret >> 32);
        freg[d->rd+1] = (u32)(vdret & 0xffffffff);
    } else {
        // Single precision
        // Place fret in destination register
        vret = std::bit_cast<u32>(fret);
        freg[d->rd] = vret;
    }


    d->pc = d->npc;
    d->npc += 4;

}

// fcc codes, ref sparc manual Table B-2
inline u8 get_fcc(float f1, float f2) {
    if(f1 == f2)
        return 0;
    else if(f1 < f2)
        return 1;
    else if(f1 > f2)
        return 2;
    else
        return 3;
}

// fcc codes, ref sparc manual Table B-2
inline u8 get_fccd(double f1, double f2) {
    if(f1 == f2)
        return 0;
    else if(f1 < f2)
        return 1;
    else if(f1 > f2)
        return 2;
    else
        return 3;
}

void CPU::FOP2_impl(pDecode_t d)
{
    if (d->p->ef == 0) {
        trap (d,  SPARC_FP_DISABLED   );
        return;
    } 

    // Need to get rs2 and opf
    u8 rs2 = d->opcode & LOBITS5;
    u16 opf = (d->opcode >> 5) & LOBITS9;

    bool isdouble = ((opf >> 1) & 0x1) && 0x1;

    if(!isdouble) {
        u32 v1, v2;
        float f1, f2;
        v1 = freg[d->rs1];
        v2 = freg[rs2];
        f1 = std::bit_cast<float>(v1);
        f2 = std::bit_cast<float>(v2);

        auto fcc = get_fcc(f1, f2);

#ifdef CPU_VERBOSE
        {
            os << std::format("{:#08x} fop2      %f{} {} %f{} (-> fcc = {})\n", d->pc, d->rs1, fops(opf), rs2, fcc);
            //os << "  (" << f1 << "f " << fops(opf) << " " << f2 << "f = " << fret << "f\n";
        }
#endif

        switch(opf) {

            case(0b001010001): // Fcmps 
                fsr = (fsr & ~(3 << 10) ) | (fcc & 3) << 10;
                break;
            case(0b001010101): // fcmpes
                if(isnanf(f1) || isnanf(f2)) {
                    // An IEEE_754_exception floating-point trap type indicates that a floating-point
                    // exception occurred that conforms to the ANSI/IEEE Standard 754-1985. The
                    // exception type is encoded in the cexc field. Note that aexc, fcc, and the destina-
                    // tion f register are not affected by an IEEE_754_exception trap.
                    
                    // set ftt to IEEE_754_exception
                    u8 ftt = 0x1; // IEE_754_exception
                    fsr = (fsr & ~(7 << 14) ) | ((ftt & 7) << 14);
                    
                    // ceec bit 4 (NVc) is set:
                    fsr = (fsr & ~(LOBITS5) ) | (0x1 << 4);
                    

                    trap(d, SPARC_FP_EXCEPTION);

                    return;
                } else {
                    fsr = (fsr & ~(3 << 10) ) | ((fcc & 3) << 10);
                    break;
                }
                

            default:
                // set ftt to unimplemented fpOP
                u8 ftt = 0x3; // unimp fpop
                fsr = (fsr & ~(7 << 14) ) | ((ftt & 7) << 14);
                trap(d, SPARC_FP_EXCEPTION);
                throw std::runtime_error("FPOP in fop2 not implemented, see trap/ftt");
                return;
            


        }
    } else {
        if(d->rd & 0x1 || rs2 & 0x1 || d->rs1 & 0x1) {
            // See sparc manual B2 suggestion of behavior when using uneven fp reg
            // set ftt to invalid_fp_register
            u8 ftt = 0x6; // invalid_fp_register, see Table 4-4
            fsr = (fsr & ~(7 << 14) ) | ((ftt & 7) << 14);
            trap(d, SPARC_FP_EXCEPTION);
            return;
        }

        double f1, f2;
        u64 v1 = (((u64)freg[d->rs1]) << 32) | (freg[d->rs1+1]);
        u64 v2 = (((u64)freg[rs2]) << 32) | (freg[rs2+1]);
        f1 = std::bit_cast<double>(v1);
        f2 = std::bit_cast<double>(v2);

        auto fcc = get_fccd(f1, f2);

#ifdef CPU_VERBOSE
        {
            os << std::format("{:#08x} fop2      %f{} {} %f{} (-> fcc = {})\n", d->pc, d->rs1, fops(opf), rs2, fcc);
            //os << "  (" << f1 << "f " << fops(opf) << " " << f2 << "f = " << fret << "f\n";
        }
#endif

        switch(opf) {

            case(0b001010010): // Fcmpd 
                fsr = (fsr & ~(3 << 10) ) | (fcc & 3) << 10;
                break;
            case(0b001010110): // fcmped
                if(std::isnan(f1) || std::isnan(f2)) {
                    // An IEEE_754_exception floating-point trap type indicates that a floating-point
                    // exception occurred that conforms to the ANSI/IEEE Standard 754-1985. The
                    // exception type is encoded in the cexc field. Note that aexc, fcc, and the destina-
                    // tion f register are not affected by an IEEE_754_exception trap.
                    
                    // set ftt to IEEE_754_exception
                    u8 ftt = 0x1; // IEE_754_exception
                    fsr = (fsr & ~(7 << 14) ) | ((ftt & 7) << 14);
                    
                    // ceec bit 4 (NVc) is set:
                    fsr = (fsr & ~(LOBITS5) ) | (0x1 << 4);
                    

                    trap(d, SPARC_FP_EXCEPTION);

                    return;
                } else {
                    fsr = (fsr & ~(3 << 10) ) | ((fcc & 3) << 10);
                    break;
                }
                

            default:
                // set ftt to unimplemented fpOP
                u8 ftt = 0x3; // unimp fpop
                fsr = (fsr & ~(7 << 14) ) | ((ftt & 7) << 14);
                trap(d, SPARC_FP_EXCEPTION);
                throw std::runtime_error("FPOP in fop2 not implemented, see trap/ftt");
                return;
        }


    }
    d->pc = d->npc;
    d->npc += 4;
}

