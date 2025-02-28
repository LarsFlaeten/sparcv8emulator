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

    CPU cpu;
    SDRAM<0x01000000> RAM;  // IO: 0x0, 16 MB of RAM

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
     
        d.p = (pPSR_t)&(d.PSR);
        d.p->s = 1; // Set supervisor mode 
        d.p->et = 1; // Enable traps 
       
        cpu.Decode(&d);
        
        d.function(&cpu, &d);
        cpu.WriteBack(&d); 
    }


};



LDSTFSRTest::LDSTFSRTest()
{  
   	


}

LDSTFSRTest::~LDSTFSRTest()
{

}

void LDSTFSRTest::SetUp()
{
    // Set up IO mapping
    // TODO: Move this MMU functions?
    for(unsigned a = 0x0; a < 0x100; ++a)
        MMU::IOmap[a] = { [&](u32 i)          { return RAM.Read(i/4); },
                         [&](u32 i, u32 v)   { RAM.Write(i/4, v);    } };

    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    cpu.Reset(entry_va);
 
}

void LDSTFSRTest::TearDown()
{
}

// c1 28 a0 dc, stfsr from Linux kernel boot sequence
TEST_F(LDSTFSRTest, C128A0DC)
{
    

        //op = stfsr $fsr -> [$g2 + 220]
        cpu.SetFSR(0xcafebabe);
         
        cpu.WriteReg(0x000f0300, GLOBALREG2); 
    
        DecodeStruct d;
        d.opcode = 0xc128a0dc;
        d.p = (pPSR_t)&(d.PSR);
        d.p->s = 1; // Set supervisor mode 
        d.p->et = 1; // Enable traps 
  
        cpu.SetVerbose(true);     
        cpu.Decode(&d);
        
        d.function(&cpu, &d);
        cpu.WriteBack(&d);
        ASSERT_EQ(cpu.GetTrapType(), 0);
 

        // The fsr value should now be in [$g2 + 220]:
        u32 value;
        MMU::MemAccess<intent_load,4>(0x000f03dc, value, CROSS_ENDIAN);
        ASSERT_EQ(value, 0xcafebabe); 
}    

TEST_F(LDSTFSRTest, STFSRG2G3)
{
    

        cpu.SetFSR(0xcafebabe);
         
        cpu.WriteReg(0x000f0000, GLOBALREG2); 
        cpu.WriteReg(0xbab0, GLOBALREG3); 
    
        do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_EQ(cpu.GetTrapType(), 0);
        // The fsr value should now be in [$g2 + $g3]:
        u32 value;
        MMU::MemAccess<intent_load,4>(0x000fbab0, value, CROSS_ENDIAN);
        ASSERT_EQ(value, 0xcafebabe); 
}  

TEST_F(LDSTFSRTest, STFSR_TrapUnaligned)
{

        cpu.SetFSR(0xcafebabe);
         
        cpu.WriteReg(0x000f0000, GLOBALREG2); 
        cpu.WriteReg(0x0, GLOBALREG3); 
    
        do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_EQ(cpu.GetTrapType(), 0);
        cpu.Reset(0x0);

        cpu.WriteReg(0x000f0000, GLOBALREG2); 
        cpu.WriteReg(0x1, GLOBALREG3); 
        do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_GT(cpu.GetTrapType(), 0);
        cpu.Reset(0x0);
 
        cpu.WriteReg(0x000f0000, GLOBALREG2); 
        cpu.WriteReg(0x2, GLOBALREG3); 
        do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_GT(cpu.GetTrapType(), 0);
        cpu.Reset(0x0);
 
        cpu.WriteReg(0x000f0000, GLOBALREG2); 
        cpu.WriteReg(0x3, GLOBALREG3); 
        do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_GT(cpu.GetTrapType(), 0);
        cpu.Reset(0x0);
 
        cpu.WriteReg(0x000f0002, GLOBALREG2); 
        cpu.WriteReg(0x1, GLOBALREG3); 
        do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_GT(cpu.GetTrapType(), 0);
        cpu.Reset(0x0);
         
        cpu.WriteReg(0x000f0000, GLOBALREG2); 
        cpu.WriteReg(0x0, GLOBALREG3); 
        do_STFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_EQ(cpu.GetTrapType(), 0);
        cpu.Reset(0x0);
 
}


TEST_F(LDSTFSRTest, LDFSRG2G3)
{
        u32 value = 0xcafebabe;
        MMU::MemAccess<intent_store,4>(0x000fbab0, value, CROSS_ENDIAN);
         
        cpu.WriteReg(0x000f0000, GLOBALREG2); 
        cpu.WriteReg(0xbab0, GLOBALREG3); 
    
        do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_EQ(cpu.GetTrapType(), 0);
        
        // The fsr value should now be cafebabe, set fro memory:
        ASSERT_EQ(cpu.GetFSR(), 0xcafebabe); 
}  

TEST_F(LDSTFSRTest, LSDFSR_TrapUnaligned)
{
        u32 value = 0xcafebabe;
        MMU::MemAccess<intent_store,4>(0x000fbab0, value, CROSS_ENDIAN);
 
        cpu.WriteReg(0x000f0000, GLOBALREG2); 
        cpu.WriteReg(0xbab0, GLOBALREG3); 
    
        do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_EQ(cpu.GetTrapType(), 0);
        // The fsr value should now be cafebabe, set fro memory:
        ASSERT_EQ(cpu.GetFSR(), 0xcafebabe); 
        cpu.Reset(0x0);
        
        
        cpu.WriteReg(0x000f0000, GLOBALREG2); 
        cpu.WriteReg(0x1, GLOBALREG3); 
        do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_GT(cpu.GetTrapType(), 0);
        cpu.Reset(0x0);
 
        cpu.WriteReg(0x000f0000, GLOBALREG2); 
        cpu.WriteReg(0x2, GLOBALREG3); 
        do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_GT(cpu.GetTrapType(), 0);
        cpu.Reset(0x0);
 
        cpu.WriteReg(0x000f0000, GLOBALREG2); 
        cpu.WriteReg(0x3, GLOBALREG3); 
        do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_GT(cpu.GetTrapType(), 0);
        cpu.Reset(0x0);
 
        cpu.WriteReg(0x000f0002, GLOBALREG2); 
        cpu.WriteReg(0x1, GLOBALREG3); 
        do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_GT(cpu.GetTrapType(), 0);
        cpu.Reset(0x0);
 

        value = 0xdeadbeef;
        MMU::MemAccess<intent_store,4>(0x000fbab0, value, CROSS_ENDIAN);
         
        cpu.WriteReg(0x000f0000, GLOBALREG2); 
        cpu.WriteReg(0xbab0, GLOBALREG3); 
        do_LDFSR_instr(GLOBALREG2, GLOBALREG3, GLOBALREG0);
        ASSERT_EQ(cpu.GetTrapType(), 0);
        ASSERT_EQ(cpu.GetFSR(), 0xdeadbeef); 
 
}

/*
TEST_F(LDSTFSRTest, WRWIM_allOnes)
{    
    // 
    
    cpu.WriteReg(0x0, LOCALREG4); // write all ones to L4
    cpu.WriteReg(0x0, LOCALREG5); 
     
    do_LDFSR
    
    // WIM should only have bits set up the number of windows:
    // Sparc reference:
    // WRWIM with all
    // bits set to 1, followed by a RDWIM, yields a bit vector in which the imple-
    // mented windows (and only the implemented windows) are indicated by 1’s.
    ASSERT_EQ(cpu.GetWIM(), (0x1 << (NWINDOWS))-1); // E.G for NWINDOWS = 8, WIM shall read 0b11111111
    ASSERT_EQ(cpu.GetWIM(), 0b11111111); // This will brea if we change NWINDOWS

    // Check if instruction RDWIM yields the same
    op3 = 0b101010; // RDWIM
    do_op3_instr(2, op3, LOCALREG4, LOCALREG5, LOCALREG0);

    // WIM is now in rd (L0)    
    u32 l0;
    cpu.ReadReg(LOCALREG0, &l0);
 
    ASSERT_EQ(cpu.GetWIM(), l0);
}
*/

