#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"




#include <gtest/gtest.h>

#include <cmath>



class LEON3Test : public ::testing::Test {

protected:
    LEON3Test();

    virtual ~LEON3Test();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();

    IRQMP intc;    
    MCtrl mctrl;
    CPU cpu;
};



LEON3Test::LEON3Test()
    : intc(1), cpu(mctrl, intc, 0)

{  
    cpu.set_verbose(true);
    cpu.reset(0x0);
    
    mctrl.attach_bank<RamBank>(0xf0400000, 1*1024*1024); // 1 MB @ 0x0
}

LEON3Test::~LEON3Test()
{

}

void LEON3Test::SetUp()
{


}

void LEON3Test::TearDown()
{
}

TEST_F(LEON3Test, ASR17Values)
{

    u32 op1 = 0x89444000; // Rd asr17 to g4
    u32 op2 = 0x83444000; // Rd asr17 to g1
     
    // g4;
    DecodeStruct d;
    d.opcode = op1;
    cpu.decode(&d);

    EXPECT_EQ(d.rd, GLOBALREG4);
    EXPECT_EQ(d.rs1, 17);
    
    d.function(&cpu, &d);
    cpu.write_back(&d); 

    // Check asr17 values now in G4:
    u32 asr;
    cpu.read_reg(GLOBALREG4, &asr);
    EXPECT_EQ((asr >> 28) & 0b1111, 0x0);   // Processor ID
    EXPECT_EQ((asr >> 26) & 0x1, 0x1);      // Support CASA
    EXPECT_EQ((asr >> 26) & 0x1, 0x1);      // Support CASA
        
    EXPECT_EQ(asr & 0x1f, 7);      // NWIN + 1 = NWINDOWS
 
     
    // g1;
    d.opcode = op2;
    cpu.decode(&d);

    EXPECT_EQ(d.rd, GLOBALREG1);
    EXPECT_EQ(d.rs1, 17);
    
    d.function(&cpu, &d);
    cpu.write_back(&d); 

    // Check asr17 values now in G1:
    cpu.read_reg(GLOBALREG1, &asr);
    EXPECT_EQ((asr >> 28) & 0b1111, 0x0);   // Processor ID
    EXPECT_EQ((asr >> 26) & 0x1, 0x1);      // Support CASA
    EXPECT_EQ((asr >> 26) & 0x1, 0x1);      // Support CASA
        
    EXPECT_EQ(asr & 0x1f, 7);      // NWIN + 1 = NWINDOWS
 



}

TEST_F(LEON3Test, CASA_swap)
{
    auto& mmu = cpu.get_mmu();

    u32 op = 0xf7e4c161; // casa [%l3], 0xb, %g1, %i3
     
    // g4;
    DecodeStruct d;
    
    d.psr = cpu.get_psr();
 
    // Map the PSR structure to the decode PSR variable just once
    d.p = (pPSR_t) &(d.psr);

   
    d.opcode = op;
    cpu.decode(&d);

    EXPECT_EQ(d.rd, INREG3);
    EXPECT_EQ(d.rs1, LOCALREG3);
    EXPECT_EQ(d.imm_disp_rs2 & 0x1f, GLOBALREG1);
    
    // Verify MMU is turned off for this test:
    ASSERT_FALSE(mmu.GetEnabled());
 
    // Write something to memory:
    u32 mem_value = 0x3ffffdff;
    auto ret = mmu.MemAccess<intent_store>(0xf04e01f4, mem_value, CROSS_ENDIAN);
    ASSERT_EQ(ret, 0);

    // Write regs:
    cpu.write_reg(0xf04e01f4, LOCALREG3); // The address of the value in memory
    cpu.write_reg(0x3ffffdff, GLOBALREG1); // The value to be compared with
    cpu.write_reg(0x3ffffe00, INREG3); // The value to be compared with
    
    u32 check;
    cpu.read_reg(LOCALREG3, &check);
    EXPECT_EQ(check, 0xf04e01f4);
    cpu.read_reg(GLOBALREG1, &check);
    EXPECT_EQ(check, 0x3ffffdff);
    cpu.read_reg(INREG3, &check);
    EXPECT_EQ(check, 0x3ffffe00);

    d.function(&cpu, &d);

    // Check compare and swap has been performed:
    u32 l3val; cpu.read_reg(LOCALREG3, &l3val);
    EXPECT_EQ(l3val, 0xf04e01f4);
    ret = mmu.MemAccess<intent_load>(l3val, mem_value, CROSS_ENDIAN);
    ASSERT_EQ(ret, 0);
    
    EXPECT_EQ(mem_value, 0x3ffffe00); // The value from rd (%i3) should now be in mwmory
                                      // at location pointed by %l3

    // The previus value in memory (0x3ffffdff) should now be in %i3 (rd)
    u32 i3val; cpu.read_reg(INREG3, &i3val);
    EXPECT_EQ(i3val, 0x3ffffdff);
 





}

TEST_F(LEON3Test, CASA_noswap)
{
    auto& mmu = cpu.get_mmu();
    
    u32 op = 0xf7e4c161; // casa [%l3], 0xb, %g1, %i3
     
    // g4;
    DecodeStruct d;
    d.psr = cpu.get_psr();
 
    // Map the PSR structure to the decode PSR variable just once
    d.p = (pPSR_t) &(d.psr);

   
    d.opcode = op;
    cpu.decode(&d);

    EXPECT_EQ(d.rd, INREG3);
    EXPECT_EQ(d.rs1, LOCALREG3);
    EXPECT_EQ(d.imm_disp_rs2 & 0x1f, GLOBALREG1);
    
    // Verify MMU is turned off for this test:
    ASSERT_FALSE(mmu.GetEnabled());
 
    // Write something to memory:
    u32 mem_value = 0x3ffffdff;
    auto ret = mmu.MemAccess<intent_store>(0xf04e01f4, mem_value, CROSS_ENDIAN);
    ASSERT_EQ(ret, 0);
    
    // Write regs:
    cpu.write_reg(0xf04e01f4, LOCALREG3); // The address of the value in memory
    cpu.write_reg(0x3ffffd0f, GLOBALREG1); // The value to be compared with
    cpu.write_reg(0x3ffffe00, INREG3); // The value to be compared with
    
    u32 check;
    cpu.read_reg(LOCALREG3, &check);
    EXPECT_EQ(check, 0xf04e01f4);
    cpu.read_reg(GLOBALREG1, &check);
    EXPECT_EQ(check, 0x3ffffd0f);
    cpu.read_reg(INREG3, &check);
    EXPECT_EQ(check, 0x3ffffe00);

    d.function(&cpu, &d);

    // Check compare and swap has not been performed:
    u32 l3val; cpu.read_reg(LOCALREG3, &l3val);
    EXPECT_EQ(l3val, 0xf04e01f4);
    ret = mmu.MemAccess<intent_load>(l3val, mem_value, CROSS_ENDIAN);
    ASSERT_EQ(ret, 0);
    
    EXPECT_EQ(mem_value, 0x3ffffdff); // The value in memory at location pointed by %l3
                                      // Should be same as before

    // The value in memory (0x3ffffdff) should also now be in %i3 (rd)
    u32 i3val; cpu.read_reg(INREG3, &i3val);
    EXPECT_EQ(i3val, 0x3ffffdff);
 
}

