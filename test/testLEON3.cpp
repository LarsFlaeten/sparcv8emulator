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
        
    SDRAM<0x01000000> RAM;   // IO: 0xf0000000, 16 MB of RAM
 
    CPU cpu;
};



LEON3Test::LEON3Test()
    : cpu()

{  
    cpu.SetVerbose(true);
    cpu.SetId(0);

    // Set up IO mapping
    // TODO: Move this MMU functions?
    u32 base_ram = 0xf0000000;
    u32 size_ram = RAM.getSizeBytes();
    u32 start = base_ram/0x10000;
    u32 end = (base_ram + size_ram)/0x10000;
    for(unsigned a = start; a < end; ++a)
        MMU::IOmap[a] = { [&RAM = RAM](u32 i)          { return RAM.Read( (i-0xf0000000)/4); },
                          [&RAM = RAM](u32 i, u32 v)   {        RAM.Write((i-0xf0000000)/4, v);    } };
   
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
    cpu.Decode(&d);

    EXPECT_EQ(d.rd, GLOBALREG4);
    EXPECT_EQ(d.rs1, 17);
    
    d.function(&cpu, &d);
    cpu.WriteBack(&d); 

    // Check asr17 values now in G4:
    u32 asr;
    cpu.ReadReg(GLOBALREG4, &asr);
    EXPECT_EQ((asr >> 28) & 0b1111, 0x0);   // Processor ID
    EXPECT_EQ((asr >> 26) & 0x1, 0x1);      // Support CASA
    EXPECT_EQ((asr >> 26) & 0x1, 0x1);      // Support CASA
        
    EXPECT_EQ(asr & 0x1f, 7);      // NWIN + 1 = NWINDOWS
 
     
    // g1;
    d.opcode = op2;
    cpu.Decode(&d);

    EXPECT_EQ(d.rd, GLOBALREG1);
    EXPECT_EQ(d.rs1, 17);
    
    d.function(&cpu, &d);
    cpu.WriteBack(&d); 

    // Check asr17 values now in G1:
    cpu.ReadReg(GLOBALREG1, &asr);
    EXPECT_EQ((asr >> 28) & 0b1111, 0x0);   // Processor ID
    EXPECT_EQ((asr >> 26) & 0x1, 0x1);      // Support CASA
    EXPECT_EQ((asr >> 26) & 0x1, 0x1);      // Support CASA
        
    EXPECT_EQ(asr & 0x1f, 7);      // NWIN + 1 = NWINDOWS
 



}

TEST_F(LEON3Test, CASA_swap)
{
    u32 op = 0xf7e4c161; // casa [%l3], 0xb, %g1, %i3
     
    // g4;
    DecodeStruct d;
 
    // Map the PSR structure to the decode PSR variable just once
    d.p = (pPSR_t) &(d.PSR);

   
    d.opcode = op;
    cpu.Decode(&d);

    EXPECT_EQ(d.rd, INREG3);
    EXPECT_EQ(d.rs1, LOCALREG3);
    EXPECT_EQ(d.imm_disp_rs2 & 0x1f, GLOBALREG1);
    
    // Verify MMU is turned off for this test:
    ASSERT_FALSE(MMU::GetEnabled());
 
    // Write something to memory:
    u32 mem_value = 0x3ffffdff;
    MMU::MemAccess<intent_store>(0xf04e01f4, mem_value, CROSS_ENDIAN);

    // Write regs:
    cpu.WriteReg(0xf04e01f4, LOCALREG3); // The address of the value in memory
    cpu.WriteReg(0x3ffffdff, GLOBALREG1); // The value to be compared with
    cpu.WriteReg(0x3ffffe00, INREG3); // The value to be compared with
    
    u32 check;
    cpu.ReadReg(LOCALREG3, &check);
    EXPECT_EQ(check, 0xf04e01f4);
    cpu.ReadReg(GLOBALREG1, &check);
    EXPECT_EQ(check, 0x3ffffdff);
    cpu.ReadReg(INREG3, &check);
    EXPECT_EQ(check, 0x3ffffe00);

    d.function(&cpu, &d);

    // Check compare and swap has been performed:
    u32 l3val; cpu.ReadReg(LOCALREG3, &l3val);
    EXPECT_EQ(l3val, 0xf04e01f4);
    MMU::MemAccess<intent_load>(l3val, mem_value, CROSS_ENDIAN);
    EXPECT_EQ(mem_value, 0x3ffffe00); // The value from rd (%i3) should now be in mwmory
                                      // at location pointed by %l3

    // The previus value in memory (0x3ffffdff) should now be in %i3 (rd)
    u32 i3val; cpu.ReadReg(INREG3, &i3val);
    EXPECT_EQ(i3val, 0x3ffffdff);
 





}

TEST_F(LEON3Test, CASA_noswap)
{
    u32 op = 0xf7e4c161; // casa [%l3], 0xb, %g1, %i3
     
    // g4;
    DecodeStruct d;
 
    // Map the PSR structure to the decode PSR variable just once
    d.p = (pPSR_t) &(d.PSR);

   
    d.opcode = op;
    cpu.Decode(&d);

    EXPECT_EQ(d.rd, INREG3);
    EXPECT_EQ(d.rs1, LOCALREG3);
    EXPECT_EQ(d.imm_disp_rs2 & 0x1f, GLOBALREG1);
    
    // Verify MMU is turned off for this test:
    ASSERT_FALSE(MMU::GetEnabled());
 
    // Write something to memory:
    u32 mem_value = 0x3ffffdff;
    MMU::MemAccess<intent_store>(0xf04e01f4, mem_value, CROSS_ENDIAN);

    // Write regs:
    cpu.WriteReg(0xf04e01f4, LOCALREG3); // The address of the value in memory
    cpu.WriteReg(0x3ffffd0f, GLOBALREG1); // The value to be compared with
    cpu.WriteReg(0x3ffffe00, INREG3); // The value to be compared with
    
    u32 check;
    cpu.ReadReg(LOCALREG3, &check);
    EXPECT_EQ(check, 0xf04e01f4);
    cpu.ReadReg(GLOBALREG1, &check);
    EXPECT_EQ(check, 0x3ffffd0f);
    cpu.ReadReg(INREG3, &check);
    EXPECT_EQ(check, 0x3ffffe00);

    d.function(&cpu, &d);

    // Check compare and swap has not been performed:
    u32 l3val; cpu.ReadReg(LOCALREG3, &l3val);
    EXPECT_EQ(l3val, 0xf04e01f4);
    MMU::MemAccess<intent_load>(l3val, mem_value, CROSS_ENDIAN);
    EXPECT_EQ(mem_value, 0x3ffffdff); // The value in memory at location pointed by %l3
                                      // Should be same as before

    // The value in memory (0x3ffffdff) should also now be in %i3 (rd)
    u32 i3val; cpu.ReadReg(INREG3, &i3val);
    EXPECT_EQ(i3val, 0x3ffffdff);
 
}

