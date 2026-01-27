#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"


#include <gtest/gtest.h>

#include <cmath>

#include <bit>


class FPUTest : public ::testing::Test {

protected:
    FPUTest();

    virtual ~FPUTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();

    IRQMP intc;
    MCtrl mctrl;
    CPU cpu;
   
    void _STFSR(u32 rs1, u32 rs2, u32 rd) {
        u32 op3 = 0b100101;
        do_op3_instr(3, op3, rs1, rs2, rd);
    }

    void _STF(u32 fr, u32 addr) {
        cpu.write_reg(addr, LOCALREG1); 
        cpu.write_reg(0, LOCALREG2);
        u32 op3 = 0b100100;
        do_op3_instr(3, op3, LOCALREG1, LOCALREG2, fr);
    }

    void _STDF(u32 fr, u32 addr) {
        cpu.write_reg(addr, LOCALREG1); 
        cpu.write_reg(0, LOCALREG2);
        
        u32 op3 = 0b100111;
        do_op3_instr(3, op3, LOCALREG1, LOCALREG2, fr);
    }
 
    void _LDFSR(u32 rs1, u32 rs2, u32 rd) {
        u32 op3 = 0b100001;
        do_op3_instr(3, op3, rs1, rs2, rd);
    }

    void _LDF(u32 addr, u32 fr) {
        cpu.write_reg(addr, LOCALREG1); 
        cpu.write_reg(0, LOCALREG2);
        u32 op3 = 0b100000;
        do_op3_instr(3, op3, LOCALREG1, LOCALREG2, fr);
    }

    void _LDDF(u32 addr, u32 fr) {
        cpu.write_reg(addr, LOCALREG1); 
        cpu.write_reg(0, LOCALREG2);
        u32 op3 = 0b100011;
        do_op3_instr(3, op3, LOCALREG1, LOCALREG2, fr);
    }
 
    void do_op3_instr(u32 op, u32 op3, u32 rs1, u32 rs2, u32 rd) {
        DecodeStruct d = {};
        d.opcode = ((op & LOBITS2) << FMTSTARTBIT) 
            | ((rd & LOBITS5) << RDSTARTBIT)
            | ((op3 & LOBITS6) << OP3STARTBIT)
            | ((rs1) << RS1STARTBIT)
            | (0x0 << ISTARTBIT)
            | ((rs2)<< RS2STARTBIT); 
     
        d.p = (pPSR_t)&(d.psr);
        d.p->s = 1; // Set supervisor mode 
        d.p->et = 1; // Enable traps 
       
        cpu.decode(&d);
        
        d.function(&cpu, &d);
        cpu.write_back(&d); 
    }

    void do_fop1_instr(u32 opf, u32 rs1, u32 rs2, u32 rd) {
        DecodeStruct d = {};
        d.opcode = ((2 & LOBITS2) << FMTSTARTBIT) 
            | ((rd & LOBITS5) << RDSTARTBIT)
            | ((0b110100 & LOBITS6) << OP3STARTBIT)
            | ((rs1 & LOBITS5) << RS1STARTBIT)
            | ((opf & LOBITS9) << 5)
            | ((rs2 & LOBITS5) << RS2STARTBIT); 
     
        d.p = (pPSR_t)&(d.psr);
        d.p->s = 1; // Set supervisor mode 
        d.p->et = 1; // Enable traps 
       
        cpu.decode(&d);
        
        d.function(&cpu, &d);
        cpu.write_back(&d); 
    }

    void do_fop2_instr(u32 opf, u32 rs1, u32 rs2) {
        DecodeStruct d = {};
        d.opcode = ((2 & LOBITS2) << FMTSTARTBIT) 
            | ((0 & LOBITS5) << RDSTARTBIT)
            | ((0b110101 & LOBITS6) << OP3STARTBIT)
            | ((rs1 & LOBITS5) << RS1STARTBIT)
            | ((opf & LOBITS9)  << 5)
            | ((rs2 & LOBITS5)<< RS2STARTBIT); 
     
        d.p = (pPSR_t)&(d.psr);
        d.p->s = 1; // Set supervisor mode 
        d.p->et = 1; // Enable traps 
       
        cpu.decode(&d);
        
        d.function(&cpu, &d);
        cpu.write_back(&d); 
    }

    void set_float(float value, u8 freg_no) {
        auto vf = std::bit_cast<u32>(value);
        mctrl.write32(0x40001000, vf);
        _LDF(0x40001000, freg_no);
    }

    float get_float(u8 freg_no) {
        _STF(freg_no, 0x40001000);
        auto vf = mctrl.read32(0x40001000);
        return std::bit_cast<float>(vf);
    }

    void set_double(double value, u8 freg_no) {
        auto tmp = std::bit_cast<u64>(value);
        mctrl.write64(0x40001000, tmp);
        _LDDF(0x40001000, freg_no);
    }

    double get_double(u8 freg_no) {
        _STDF(freg_no, 0x40001000);
        auto tmp = mctrl.read64(0x40001000);
        return std::bit_cast<double>(tmp);
    }


};



FPUTest::FPUTest() : intc(1), cpu(mctrl, intc, 0)
{  
   	


}

FPUTest::~FPUTest()
{

}

void FPUTest::SetUp()
{
    mctrl.attach_bank<RamBank>(0x40000000, 1*1024*1024); // 1 MB @ 0x40000000
   
    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    cpu.reset(entry_va);
 
}

void FPUTest::TearDown()
{
}

TEST_F(FPUTest, LDF_STF)
{
    
    u32 ftest1 = 0x41460000; // 12.375f
    u32 ftest2 = 0x3f800000; // 1.0f
    u32 ftest3 = 0x3e800000; // 0.25f
    u32 ftest4 = 0x3ec00000; // 0.375f

    mctrl.write32(0x40001000, ftest1);
    mctrl.write32(0x40001004, ftest2);
    mctrl.write32(0x40001008, ftest3);
    mctrl.write32(0x4000100C, ftest4);

    _LDF(0x40001000, 0);
    _LDF(0x40001004, 1);
    _LDF(0x40001008, 2);
    _LDF(0x4000100C, 3);
    

    _STF(0, 0x40002000);
    _STF(1, 0x40002004);
    _STF(2, 0x40002008);
    _STF(3, 0x4000200C);
    
    auto v1 = mctrl.read32(0x40002000);
    auto v2 = mctrl.read32(0x40002004);
    auto v3 = mctrl.read32(0x40002008);
    auto v4 = mctrl.read32(0x4000200C);

    ASSERT_EQ(v1, ftest1);
    ASSERT_EQ(v2, ftest2);
    ASSERT_EQ(v3, ftest3);
    ASSERT_EQ(v4, ftest4);

    auto f1 = std::bit_cast<float>(v1);
    auto f2 = std::bit_cast<float>(v2);
    auto f3 = std::bit_cast<float>(v3);
    auto f4 = std::bit_cast<float>(v4);
    
    ASSERT_EQ(f1, 12.375f);
    ASSERT_EQ(f2, 1.0f);
    ASSERT_EQ(f3, 0.25f);
    ASSERT_EQ(f4, 0.375f);
    
}

TEST_F(FPUTest, LDF_STF_Traps)
{
    
    u32 ftest1 = 0x41460000; // 12.375f
    u32 ftest2 = 0x3f800000; // 1.0f
    u32 ftest3 = 0x3e800000; // 0.25f
    u32 ftest4 = 0x3ec00000; // 0.375f

    mctrl.write32(0x40001000, ftest1);
    mctrl.write32(0x40001004, ftest2);
    mctrl.write32(0x40001008, ftest3);
    mctrl.write32(0x4000100C, ftest4);

    _LDF(0x40001001, 0); // Should Trap due to unaligned
    ASSERT_EQ(cpu.get_trap_type(), SPARC_MEMORY_ADDR_NOT_ALIGNED);

    cpu.reset(0);
    ASSERT_EQ(cpu.get_trap_type(), 0);

    _STF(0, 0x40002001);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_MEMORY_ADDR_NOT_ALIGNED);

    cpu.reset(0);
    ASSERT_EQ(cpu.get_trap_type(), 0);

    // Disable FPU:
    auto psr = cpu.get_psr();
    pPSR_t p = (pPSR_t)&(psr);
    p->ef = 0;
    cpu.set_psr(psr);

    _LDF(0x40001000, 0); // Should Trap due to FPU disabled
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_DISABLED);

    cpu.reset(0);
    cpu.set_psr(psr);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    
    _STF(0, 0x40002001);
    // Should Trap due to FPU disabled
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_DISABLED);

}

TEST_F(FPUTest, FOP1_SINGLE)
{
    float f1 = 6.743368f; 
    set_float(f1, 6);
    float fn1 = -f1;
    set_float(fn1, 7);
    
    // fmovs
    do_fop1_instr(0b000000001, 0, 6, 30);
    auto fmov = get_float(30);
    ASSERT_EQ(f1, fmov);

    // fnegs
    do_fop1_instr(0b000000101, 0, 6, 30);
    auto fnegs = get_float(30);
    ASSERT_EQ(-f1, fnegs);

    // fabss on f1 (positive), should equal f1
    do_fop1_instr(0b000001001, 0, 6, 30);
    auto fabss = get_float(30);
    ASSERT_EQ(f1, fabss);
    
    //fabs on fn1 (negative), should equal f1
    do_fop1_instr(0b000001001, 0, 7, 30); 
    auto fna1 = get_float(30);
    ASSERT_EQ(f1, fna1);
    
    // fsqtrs
    set_float(2.0f, 4);
    do_fop1_instr(0b000101001, 0, 4, 30);
    auto fsq = get_float(30);
    ASSERT_EQ(fsq, sqrtf(2.0f));
    
    // fadds
    set_float(2.0f, 3);
    set_float(3.0f, 4);
    do_fop1_instr(0b001000001, 3, 4, 26);
    auto fadd = get_float(26);
    ASSERT_EQ(fadd, 5.0f);
    
    // fsubs
    set_float(42.0f, 3);
    set_float(3.0f, 4);
    do_fop1_instr(0b001000101, 3, 4, 26);
    auto sub = get_float(26);
    ASSERT_EQ(sub, 39.0f);
 
    // fmuls
    set_float(42.0f, 3);
    set_float(3.0f, 4);
    do_fop1_instr(0b001001001, 3, 4, 26);
    auto mul = get_float(26);
    ASSERT_EQ(mul, 126.0f);
 
    // fdivs
    set_float(42.0f, 3);
    set_float(2.0f, 4);
    do_fop1_instr(0b001001101, 3, 4, 26);
    auto div = get_float(26);
    ASSERT_EQ(div, 21.0f);

    // fsmuld
    set_float(42.0f, 3);
    set_float(3.0f, 4);
    do_fop1_instr(0b001101001, 3, 4, 26);
    auto smuld = get_double(26);
    ASSERT_EQ(smuld, 126.0);
    ASSERT_EQ(cpu.get_trap_type(), 0);

    //fsmuld should trap on uneven rd, but not rs1 and rs2
    do_fop1_instr(0b001101001, 2, 5, 26);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    do_fop1_instr(0b001101001, 2, 4, 25);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    

    
 

}

TEST_F(FPUTest, FOP1_DOUBLE)
{
    set_double(1.0, 0);
    set_double(2.0, 2);

    ASSERT_EQ(get_double(0), 1.0);
    ASSERT_EQ(get_double(2), 2.0);
    
    // movd (need double moves)
    double d1 = 5.37225636535526; 
    set_double(d1, 6); // regs 6 and 7
    do_fop1_instr(0b000000001, 0, 6, 8);
    do_fop1_instr(0b000000001, 0, 7, 9);
    ASSERT_EQ(get_double(8), d1);

    // fnegs for double - only operate in first register, when source and dest are the same
    set_double(d1, 6); // regs 6 and 7
    do_fop1_instr(0b000000101, 0, 6, 6);
    ASSERT_EQ(get_double(6), -d1);

    // fabs for double - only operate in first register, when source and dest are the same
    set_double(d1, 6); // regs 6 and 7
    do_fop1_instr(0b000001001, 0, 6, 6);
    ASSERT_EQ(get_double(6), d1);

    set_double(-d1, 6); // regs 6 and 7
    do_fop1_instr(0b000001001, 0, 6, 6);
    ASSERT_EQ(get_double(6), d1);

    // Alternatively, we can do fabss/fnegs + fmovs
    set_double(d1, 6); // regs 6 and 7
    do_fop1_instr(0b000000101, 0, 6, 8);
    do_fop1_instr(0b000000001, 0, 7, 9);
    ASSERT_EQ(get_double(8), -d1);

    set_double(d1, 6); // regs 6 and 7
    do_fop1_instr(0b000001001, 0, 6, 8);
    do_fop1_instr(0b000000001, 0, 7, 9);
    ASSERT_EQ(get_double(8), d1);

    set_double(-d1, 6); // regs 6 and 7
    do_fop1_instr(0b000001001, 0, 6, 8);
    do_fop1_instr(0b000000001, 0, 7, 9);
    ASSERT_EQ(get_double(8), d1);

    // fsqrtd
    set_double(2.0, 4);
    do_fop1_instr(0b000101010, 0, 4, 30);
    auto dsq = get_double(30);
    ASSERT_EQ(dsq, sqrt(2.0));    
    ASSERT_EQ(cpu.get_trap_type(), 0);
    // faddd
    set_double(2.0, 4);
    set_double(7.0, 6);
    do_fop1_instr(0b001000010, 4, 6, 30);
    ASSERT_EQ(get_double(30), 9.0);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    // fsubd
    set_double(42.0, 4);
    set_double(13.0, 10);
    do_fop1_instr(0b001000110, 4, 10, 30);
    ASSERT_EQ(get_double(30), 29.0);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    // fmuld
    set_double(42.0, 4);
    set_double(13.0, 10);
    do_fop1_instr(0b001001010, 4, 10, 30);
    ASSERT_EQ(get_double(30), 546.0);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    // fdivd
    set_double(42.0, 4);
    set_double(2.0, 10);
    do_fop1_instr(0b001001110, 4, 10, 20);
    ASSERT_EQ(get_double(20), 21.0);
    ASSERT_EQ(cpu.get_trap_type(), 0);

    // Check traps on uneven regs:
    // fsqrtd
    u32 opf = 0b000101010;
    do_fop1_instr(opf, 1, 4, 30);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    do_fop1_instr(opf, 0, 5, 30);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    do_fop1_instr(opf, 0, 4, 29);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    // faddd
    opf = 0b001000010;
    do_fop1_instr(opf, 1, 4, 30);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    do_fop1_instr(opf, 0, 5, 30);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    do_fop1_instr(opf, 0, 4, 29);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    // fsubd
    opf = 0b001000110;
    do_fop1_instr(opf, 1, 4, 30);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    do_fop1_instr(opf, 0, 5, 30);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    do_fop1_instr(opf, 0, 4, 29);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    // fmuld
    opf = 0b001001010;
    do_fop1_instr(opf, 1, 4, 30);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    do_fop1_instr(opf, 0, 5, 30);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    do_fop1_instr(opf, 0, 4, 29);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    // fdivd
    opf = 0b001001110;
    do_fop1_instr(opf, 1, 4, 30);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    do_fop1_instr(opf, 0, 5, 30);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    do_fop1_instr(opf, 0, 4, 29);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    
}

TEST_F(FPUTest, FOP2_FCMPS_FCMPES)
{
    float f1 = 1.0f;
    float f2 = -1.0f;
    float f3 = 0.0f/0.0f; // nan

    ASSERT_TRUE(isnanf(f3));    

    auto vf1 = std::bit_cast<u32>(f1);
    auto vf2 = std::bit_cast<u32>(f2);
    auto vf3 = std::bit_cast<u32>(f3);

    mctrl.write32(0x40001000, vf1);
    mctrl.write32(0x40001004, vf2);
    mctrl.write32(0x40001008, vf3);
    
    _LDF(0x40001000, 1); 
    _LDF(0x40001004, 2); 
    _LDF(0x40001008, 3); 
    
    u8 opf = 0b001010001; // fcmps
    
    // Compare f1 and f2, should be LARGER (fcc == 2)
    do_fop2_instr(opf, 1, 2);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 2);

    // Compare f2 and f1, should be LESS (fcc == 1)
    do_fop2_instr(opf, 2, 1);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 1);

    // Compare f2 and f2, should be EQ (fcc == 0)
    do_fop2_instr(opf, 2, 2);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 0);

    // Compare f3 and f2, should be UNORDERED (fcc == 3), and no trap for fcmps
    do_fop2_instr(opf, 3, 2);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 3);

    opf = 0b001010101; // fcmpes

    // Compare f1 and f2, should be LARGER (fcc == 2)
    do_fop2_instr(opf, 1, 2);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 2);

    // Compare f2 and f1, should be LESS (fcc == 1)
    do_fop2_instr(opf, 2, 1);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 1);

    // Compare f2 and f2, should be EQ (fcc == 0)
    do_fop2_instr(opf, 2, 2);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 0);

    // Compare f3 and f2, should be UNORDERED (fcc == 3), and trap for fcmpes
    do_fop2_instr(opf, 3, 2);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) && LOBITS3, 1); // ftt == 1 (IEEE Exception)
    ASSERT_EQ((cpu.get_fsr()) & LOBITS5, 0b10000); // cexc / currect exception = 10000 (nvc)
    
}

TEST_F(FPUTest, FOP2_FCMPD_FCMPED)
{
    double f1 = 1.0;
    double f2 = -1.0;
    double f3 = 0.0/0.0; // nan

    ASSERT_TRUE(std::isnan(f3));    

    set_double(f1, 2);
    set_double(f2, 4);
    set_double(f3, 6);
    
    u8 opf = 0b001010010; // fcmpd
    
    // Check that uneven regs trap
    do_fop2_instr(opf, 3, 6);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);

    set_double(f1, 2);
    set_double(f2, 4);
    set_double(f3, 6);
    do_fop2_instr(opf, 4, 9);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);
    

    // Compare f1 and f2, should be LARGER (fcc == 2)
    set_double(f1, 2);
    set_double(f2, 4);
    set_double(f3, 6);
    do_fop2_instr(opf, 2, 4);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 2);

    

    // Compare f2 and f1, should be LESS (fcc == 1)
    do_fop2_instr(opf, 4, 2);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 1);

    // Compare f2 and f2, should be EQ (fcc == 0)
    do_fop2_instr(opf, 4, 4);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 0);

    // Compare f3 and f2, should be UNORDERED (fcc == 3), and no trap for fcmps
    do_fop2_instr(opf, 6, 4);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 3);

    opf = 0b001010110; // fcmpes

    // Check that uneven regs trap
    do_fop2_instr(opf, 3, 6);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);

    set_double(f1, 2);
    set_double(f2, 4);
    set_double(f3, 6);
    do_fop2_instr(opf, 4, 9);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & LOBITS3, 6); // ftt == 6 (Invalid FP reg)
    cpu.reset(0);

    set_double(f1, 2);
    set_double(f2, 4);
    set_double(f3, 6);
    // Compare f1 and f2, should be LARGER (fcc == 2)
    do_fop2_instr(opf, 2, 4);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 2);

    // Compare f2 and f1, should be LESS (fcc == 1)
    do_fop2_instr(opf, 4, 2);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 1);

    // Compare f2 and f2, should be EQ (fcc == 0)
    do_fop2_instr(opf, 4, 4);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 10) & 0x3, 0);

    // Compare f3 and f2, should be UNORDERED (fcc == 3), and trap for fcmpes
    do_fop2_instr(opf, 6, 4);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) && LOBITS3, 1); // ftt == 1 (IEEE Exception)
    ASSERT_EQ((cpu.get_fsr()) & LOBITS5, 0b10000); // cexc / currect exception = 10000 (nvc)
    
}


TEST_F(FPUTest, LDDF_STDF)
{
    double d1 = 12.375;
    double d2 = 1.0;
    double d3 = 0.25;
    double d4 = 0.375;

    auto ud1 = std::bit_cast<u64>(d1);
    auto ud2 = std::bit_cast<u64>(d2);
    auto ud3 = std::bit_cast<u64>(d3);
    auto ud4 = std::bit_cast<u64>(d4);
    
    ASSERT_EQ(ud1, 0x4028c00000000000);
    ASSERT_EQ(ud2, 0x3ff0000000000000);
    ASSERT_EQ(ud3, 0x3fd0000000000000);
    ASSERT_EQ(ud4, 0x3fd8000000000000);

    mctrl.write64(0x40003000, ud1);
    mctrl.write64(0x40003008, ud2);
    mctrl.write64(0x40003010, ud3);
    mctrl.write64(0x40003018, ud4);
    
    _LDDF(0x40003000, 20);
    _LDDF(0x40003008, 22);
    _LDDF(0x40003010, 24);
    _LDDF(0x40003018, 26);
    

    _STDF(20, 0x40004000);
    _STDF(22, 0x40004008);
    _STDF(24, 0x40004010);
    _STDF(26, 0x40004018);
    
    auto v1 = mctrl.read64(0x40004000);
    auto v2 = mctrl.read64(0x40004008);
    auto v3 = mctrl.read64(0x40004010);
    auto v4 = mctrl.read64(0x40004018);
    
    ASSERT_EQ(v1, 0x4028c00000000000);
    ASSERT_EQ(v2, 0x3ff0000000000000);
    ASSERT_EQ(v3, 0x3fd0000000000000);
    ASSERT_EQ(v4, 0x3fd8000000000000);

    auto rd1 = std::bit_cast<double>(v1);
    auto rd2 = std::bit_cast<double>(v2);
    auto rd3 = std::bit_cast<double>(v3);
    auto rd4 = std::bit_cast<double>(v4);

    ASSERT_EQ(rd1, d1);
    ASSERT_EQ(rd2, d2);
    ASSERT_EQ(rd3, d3);
    ASSERT_EQ(rd4, d4);
    
    

    
    
}

TEST_F(FPUTest, LDDF_STDF_Traps)
{
    double d1 = 12.375;
    double d2 = 1.0;
    double d3 = 0.25;
    double d4 = 0.375;

    auto ud1 = std::bit_cast<u64>(d1);
    auto ud2 = std::bit_cast<u64>(d2);
    auto ud3 = std::bit_cast<u64>(d3);
    auto ud4 = std::bit_cast<u64>(d4);
    
    ASSERT_EQ(ud1, 0x4028c00000000000);
    ASSERT_EQ(ud2, 0x3ff0000000000000);
    ASSERT_EQ(ud3, 0x3fd0000000000000);
    ASSERT_EQ(ud4, 0x3fd8000000000000);

    mctrl.write64(0x40003000, ud1);
    mctrl.write64(0x40003008, ud2);
    mctrl.write64(0x40003010, ud3);
    mctrl.write64(0x40003018, ud4);
    
    _LDDF(0x40003001, 20); // Should Trap due to unaligned
    ASSERT_EQ(cpu.get_trap_type(), SPARC_MEMORY_ADDR_NOT_ALIGNED);

    cpu.reset(0);
    ASSERT_EQ(cpu.get_trap_type(), 0);

    _STDF(20, 0x40002001);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_MEMORY_ADDR_NOT_ALIGNED);

    cpu.reset(0);
    ASSERT_EQ(cpu.get_trap_type(), 0);


    // Disable FPU:
    auto psr = cpu.get_psr();
    pPSR_t p = (pPSR_t)&(psr);
    p->ef = 0;
    cpu.set_psr(psr);

    _LDDF(0x40001000, 0); // Should Trap due to FPU disabled
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_DISABLED);

    cpu.reset(0);
    cpu.set_psr(psr);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    
    _STDF(0, 0x40002000);
    // Should Trap due to FPU disabled
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_DISABLED);

    // Check Trap on uneven freg on LOAD and STORE Double
    cpu.reset(0);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 14) & 0x7, 0); // FSR.ftt = 0 (None)

    _LDDF(0x40001000, 1); // Should Trap due to odd freg
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    // Get error type:
    ASSERT_EQ((cpu.get_fsr() >> 14) & 0x7, 6); // FSR.ftt = invalid_fp_register

    cpu.reset(0);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ((cpu.get_fsr() >> 14) & 0x7, 0); // FSR.ftt = 0 (None)

    _STDF(7, 0x40002000); // Should Trap due to odd freg
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    // Get error type:
    ASSERT_EQ((cpu.get_fsr() >> 14) & 0x7, 6); // FSR.ftt = invalid_fp_register
}

TEST_F(FPUTest, Conversions)
{
    // fitos
    mctrl.write32(0x40007000, (int)42);
    _LDF(0x40007000, 2); // Put the signed int 42 in freg 2
    do_fop1_instr(0b011000100, 0, 2, 30);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ(get_float(30), 42.0f); // Should now be converted to float

    mctrl.write32(0x40007000, (int)-42);
    _LDF(0x40007000, 2); // Put the signed int -42 in freg 2
    do_fop1_instr(0b011000100, 0, 2, 30);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ(get_float(30), -42.0f); // Should now be converted to float

    // fitod
    mctrl.write32(0x40007000, (int)42);
    _LDF(0x40007000, 2); // Put the signed int 42 in freg 2
    do_fop1_instr(0b011001000, 0, 2, 30);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ(get_double(30), 42.0); // Should now be converted to float

    mctrl.write32(0x40007000, (int)-42);
    _LDF(0x40007000, 2); // Put the signed int -42 in freg 2
    do_fop1_instr(0b011001000, 0, 2, 30);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ(get_double(30), -42.0); // Should now be converted to float

    // uneven rs1 and rs2 is ok, but not uneven rd for fitod:
    do_fop1_instr(0b011001000, 1, 2, 30);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    cpu.reset(0);
    do_fop1_instr(0b011001000, 0, 3, 30);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    cpu.reset(0);
    do_fop1_instr(0b011001000, 0, 2, 27);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & 0x7, 6); // FSR.ftt = invalid_fp_register
    cpu.reset(0);

    // fstoi
    set_float(42.0f, 4);
    do_fop1_instr(0b011010001, 0, 4, 12);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    _STF(12, 0x40002000);
    int32_t res = std::bit_cast<int32_t>(mctrl.read32(0x40002000));
    ASSERT_EQ(res, 42);

    // Uneven regs should not trap
    set_float(-542.0f, 9);
    do_fop1_instr(0b011010001, 0, 9, 13);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    _STF(13, 0x40002000);
    res = std::bit_cast<int32_t>(mctrl.read32(0x40002000));
    ASSERT_EQ(res, -542);
    
    // fdtoi
    set_double(42.0, 2);
    do_fop1_instr(0b011010010, 0, 2, 14);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    _STF(14, 0x40002000);
    res = std::bit_cast<int32_t>(mctrl.read32(0x40002000));
    ASSERT_EQ(res, 42);

    set_double(-542.0, 8);
    do_fop1_instr(0b0011010010, 0, 8, 13);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    _STF(13, 0x40002000);
    res = std::bit_cast<int32_t>(mctrl.read32(0x40002000));
    ASSERT_EQ(res, -542);

    // uneven regs for the double should trap
    do_fop1_instr(0b0011010010, 0, 9, 12);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & 0x7, 6); // FSR.ftt = invalid_fp_register
    cpu.reset(0x0);

    // fstod
    set_float(123.0f, 4);
    do_fop1_instr(0b011001001, 0, 4, 12);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    _STDF(12, 0x40002000);
    double resd = std::bit_cast<double>(mctrl.read64(0x40002000));
    ASSERT_EQ(resd, 123.0);

    // Uneven reg for the sinlge should not trap
    set_float(-65111.3f, 3);
    do_fop1_instr(0b011001001, 0, 3, 12);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    _STDF(12, 0x40002000);
    resd = std::bit_cast<double>(mctrl.read64(0x40002000));
    ASSERT_LT(fabs(resd - -65111.3), 0.1);

    // uneven regs for the double should trap
    do_fop1_instr(0b011001001, 0, 4, 13);
    ASSERT_EQ(cpu.get_trap_type(), SPARC_FP_EXCEPTION);
    ASSERT_EQ((cpu.get_fsr() >> 14) & 0x7, 6); // FSR.ftt = invalid_fp_register
    cpu.reset(0x0);
 
}
