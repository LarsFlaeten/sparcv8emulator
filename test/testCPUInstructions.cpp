#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"


#include <gtest/gtest.h>

#include <cmath>


class CPUInstructionsTest : public ::testing::Test {

protected:
    CPUInstructionsTest();

    virtual ~CPUInstructionsTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();

    CPU cpu;
    SDRAM<0x01000000> RAM;  // IO: 0x0, 16 MB of RAM

};



CPUInstructionsTest::CPUInstructionsTest()
{  
   	


}

CPUInstructionsTest::~CPUInstructionsTest()
{

}

void CPUInstructionsTest::SetUp()
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

void CPUInstructionsTest::TearDown()
{
}

TEST_F(CPUInstructionsTest, WRWIM_allOnes)
{    
    // adress to read is taken from LOCALREG4
    cpu.WriteReg((u32)0-1, LOCALREG4); // write all ones to L4
    cpu.WriteReg(0x0,  LOCALREG5); // write all ones to L4
     
    // Construct WRWIM opcode
    DecodeStruct d;
    u32 op = 0b110010; // WRWIM
    d.opcode = ((2 & LOBITS2) << FMTSTARTBIT) 
        ^ ((LOCALREG0 & LOBITS5) << RDSTARTBIT)
        ^ ((op & LOBITS6) << OP3STARTBIT)
        ^ ((LOCALREG4) << RS1STARTBIT)
        ^ (0x0 << ISTARTBIT)
        ^ ((LOCALREG5)<< RS2STARTBIT); 
        
    d.p = (pPSR_t)&(d.PSR);
    d.p->s = 1; // Set supervisor mode 
    
    cpu.Decode(&d);
    
    d.function(&cpu, &d);
    cpu.WriteBack(&d); 

    
    // WIM should only have bits set up the number of windows:
    // Sparc reference:
    // WRWIM with all
    // bits set to 1, followed by a RDWIM, yields a bit vector in which the imple-
    // mented windows (and only the implemented windows) are indicated by 1’s.
    ASSERT_EQ(cpu.GetWIM(), (0x1 << (NWINDOWS))-1); // E.G for NWINDOWS = 8, WIM shall read 0b11111111
    ASSERT_EQ(cpu.GetWIM(), 0b11111111); // This will brea if we change NWINDOWS

    // Check if instruction RDWIM yields the same
    op = 0b101010; // RDWIM
    d.opcode = ((2 & LOBITS2) << FMTSTARTBIT) 
        ^ ((LOCALREG0 & LOBITS5) << RDSTARTBIT)
        ^ ((op & LOBITS6) << OP3STARTBIT)
        ^ ((LOCALREG4) << RS1STARTBIT)
        ^ (0x0 << ISTARTBIT)
        ^ ((LOCALREG5)<< RS2STARTBIT); 
    cpu.Decode(&d);
    
    d.function(&cpu, &d);
    cpu.WriteBack(&d); 

    // WIM is now in rd (L0)    
    u32 l0;
    cpu.ReadReg(LOCALREG0, &l0);
 
    ASSERT_EQ(cpu.GetWIM(), l0);

    



} 
