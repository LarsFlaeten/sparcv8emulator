#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"


#include <gtest/gtest.h>

#include <cmath>


class LDSTFSRTest : public ::testing::Test {

protected:
    LDSTFSRTest();

    virtual ~LDSTFSRTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();

    IRQMP intc;
    MCtrl mctrl;
    CPU cpu;
   
    void do_STFSR_instr(u32 rs1, u32 rs2, u32 rd) {
        u32 op3 = 0b100101;
        do_op3_instr(3, op3, rs1, rs2, rd);
    }
 
    void do_LDFSR_instr(u32 rs1, u32 rs2, u32 rd) {
        u32 op3 = 0b100001;
        do_op3_instr(3, op3, rs1, rs2, rd);
    }
 
    void do_op3_instr(u32 op, u32 op3, u32 rs1, u32 rs2, u32 rd) {
        DecodeStruct d;
        d.opcode = ((op & LOBITS2) << FMTSTARTBIT) 
            ^ ((rd & LOBITS5) << RDSTARTBIT)
            ^ ((op3 & LOBITS6) << OP3STARTBIT)
            ^ ((rs1) << RS1STARTBIT)
            ^ (0x0 << ISTARTBIT)
            ^ ((rs2)<< RS2STARTBIT); 
     
        d.p = (pPSR_t)&(d.psr);
        d.p->s = 1; // Set supervisor mode 
        d.p->et = 1; // Enable traps 
       
        cpu.decode(&d);
        
        d.function(&cpu, &d);
        cpu.write_back(&d); 
    }


};



LDSTFSRTest::LDSTFSRTest() : intc(1), cpu(mctrl, intc, 0)
{  
   	


}

LDSTFSRTest::~LDSTFSRTest()
{

}

void LDSTFSRTest::SetUp()
{
    mctrl.attach_bank<RamBank>(0x0, 1*1024*1024); // 1 MB @ 0x0
   
    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    cpu.reset(entry_va);
 
}

void LDSTFSRTest::TearDown()
{
}

// c1 28 a0 dc, stfsr from Linux kernel boot sequence
TEST_F(LDSTFSRTest, C128A0DC)
{
    auto& mmu = cpu.get_mmu();

    //op = stfsr $fsr -> [$g2 + 220]
    cpu.set_fsr(0xcafebabe);
        
    cpu.write_reg(0x000f0300, GLOBALREG2); 

    DecodeStruct d;
    d.opcode = 0xc128a0dc;
    d.p = (pPSR_t)&(d.psr);
    d.p->s = 1; // Set supervisor mode 
    d.p->et = 1; // Enable traps 

    cpu.decode(&d);
    
    d.function(&cpu, &d);
    cpu.write_back(&d);
    ASSERT_EQ(cpu.get_trap_type(), 0);


    // The fsr value should now be in [$g2 + 220]:
    u32 value;
    mmu.MemAccess<intent_load,4>(0x000f03dc, value, CROSS_ENDIAN);
    ASSERT_EQ(value, 0xcafebabe); 
}    

TEST_F(LDSTFSRTest, STFSRG2G3)
{
    auto& mmu = cpu.get_mmu();

    cpu.set_fsr(0xcafebabe);
        
    cpu.write_reg(0x000f0000, GLOBALREG2); 
    cpu.write_reg(0xbab0, GLOBALREG3); 

    do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    // The fsr value should now be in [$g2 + $g3]:
    u32 value;
    mmu.MemAccess<intent_load,4>(0x000fbab0, value, CROSS_ENDIAN);
    ASSERT_EQ(value, 0xcafebabe); 
}  

TEST_F(LDSTFSRTest, STFSR_TrapUnaligned)
{

        cpu.set_fsr(0xcafebabe);
         
        cpu.write_reg(0x000f0000, GLOBALREG2); 
        cpu.write_reg(0x0, GLOBALREG3); 
    
        do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_EQ(cpu.get_trap_type(), 0);
        cpu.reset(0x0);

        cpu.write_reg(0x000f0000, GLOBALREG2); 
        cpu.write_reg(0x1, GLOBALREG3); 
        do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_GT(cpu.get_trap_type(), 0);
        cpu.reset(0x0);
 
        cpu.write_reg(0x000f0000, GLOBALREG2); 
        cpu.write_reg(0x2, GLOBALREG3); 
        do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_GT(cpu.get_trap_type(), 0);
        cpu.reset(0x0);
 
        cpu.write_reg(0x000f0000, GLOBALREG2); 
        cpu.write_reg(0x3, GLOBALREG3); 
        do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_GT(cpu.get_trap_type(), 0);
        cpu.reset(0x0);
 
        cpu.write_reg(0x000f0002, GLOBALREG2); 
        cpu.write_reg(0x1, GLOBALREG3); 
        do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_GT(cpu.get_trap_type(), 0);
        cpu.reset(0x0);
         
        cpu.write_reg(0x000f0000, GLOBALREG2); 
        cpu.write_reg(0x0, GLOBALREG3); 
        do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_EQ(cpu.get_trap_type(), 0);
        cpu.reset(0x0);
 
}


TEST_F(LDSTFSRTest, LDFSRG2G3)
{
    auto& mmu = cpu.get_mmu();

    u32 value = 0xcafebabe;
    mmu.MemAccess<intent_store,4>(0x000fbab0, value, CROSS_ENDIAN);
        
    cpu.write_reg(0x000f0000, GLOBALREG2); 
    cpu.write_reg(0xbab0, GLOBALREG3); 

    do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    
    // The fsr value should now be cafebabe, set fro memory:
    ASSERT_EQ(cpu.get_fsr(), 0xcafebabe); 
}  

TEST_F(LDSTFSRTest, LSDFSR_TrapUnaligned)
{
    auto& mmu = cpu.get_mmu();

    u32 value = 0xcafebabe;
    mmu.MemAccess<intent_store,4>(0x000fbab0, value, CROSS_ENDIAN);

    cpu.write_reg(0x000f0000, GLOBALREG2); 
    cpu.write_reg(0xbab0, GLOBALREG3); 

    do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    // The fsr value should now be cafebabe, set fro memory:
    ASSERT_EQ(cpu.get_fsr(), 0xcafebabe); 
    cpu.reset(0x0);
    
    
    cpu.write_reg(0x000f0000, GLOBALREG2); 
    cpu.write_reg(0x1, GLOBALREG3); 
    do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
    ASSERT_GT(cpu.get_trap_type(), 0);
    cpu.reset(0x0);

    cpu.write_reg(0x000f0000, GLOBALREG2); 
    cpu.write_reg(0x2, GLOBALREG3); 
    do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
    ASSERT_GT(cpu.get_trap_type(), 0);
    cpu.reset(0x0);

    cpu.write_reg(0x000f0000, GLOBALREG2); 
    cpu.write_reg(0x3, GLOBALREG3); 
    do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
    ASSERT_GT(cpu.get_trap_type(), 0);
    cpu.reset(0x0);

    cpu.write_reg(0x000f0002, GLOBALREG2); 
    cpu.write_reg(0x1, GLOBALREG3); 
    do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
    ASSERT_GT(cpu.get_trap_type(), 0);
    cpu.reset(0x0);


    value = 0xdeadbeef;
    mmu.MemAccess<intent_store,4>(0x000fbab0, value, CROSS_ENDIAN);
        
    cpu.write_reg(0x000f0000, GLOBALREG2); 
    cpu.write_reg(0xbab0, GLOBALREG3); 
    do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
    ASSERT_EQ(cpu.get_trap_type(), 0);
    ASSERT_EQ(cpu.get_fsr(), 0xdeadbeef); 

}
