#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"


#include <gtest/gtest.h>

#include <cmath>


class CPUTrapsTest : public ::testing::Test {

protected:
    CPUTrapsTest();

    virtual ~CPUTrapsTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();

    CPU cpu;
    SDRAM<0x01000000> RAM;  // IO: 0x0, 16 MB of RAM

    void do_RETT_instr(u32 rs1, u32 rs2, u32 rd) {
        u32 op3 = 0b111001; // RETT
        do_op3_instr(2, op3, rs1, rs2, rd, 0);
    }
 
    void do_op3_instr(u32 op, u32 op3, u32 rs1, u32 rs2, u32 rd, u32 asi) {
        DecodeStruct d;
        d.opcode = ((op & LOBITS2) << FMTSTARTBIT) 
            | ((rd & LOBITS5) << RDSTARTBIT)
            | ((op3 & LOBITS6) << OP3STARTBIT)
            | ((rs1) << RS1STARTBIT)
            | (0x0 << ISTARTBIT)
            | (asi << ASISTARTBIT)
            | ((rs2)<< RS2STARTBIT); 
    
        d.PSR = cpu.GetPSR(); 
        d.p = (pPSR_t)&(d.PSR);
       
        cpu.Decode(&d);
        
        d.function(&cpu, &d);
        cpu.WriteBack(&d); 
    }


};



CPUTrapsTest::CPUTrapsTest()
{  
   	


}

CPUTrapsTest::~CPUTrapsTest()
{

}

void CPUTrapsTest::SetUp()
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

void CPUTrapsTest::TearDown()
{
}

TEST_F(CPUTrapsTest, TestResetState)
{
    EXPECT_EQ(cpu.GetPC(), 0);
    EXPECT_EQ(cpu.GetnPC(), 4);

    u32 PSR = cpu.GetPSR(); 
    
    // Current window pointer should be 0
    EXPECT_EQ(((pPSR_t)&PSR)->cwp , 0);
    
    // Supervisor, enable traos and enable fp should all be 1
    EXPECT_EQ(((pPSR_t)&PSR)->s , 1);
    EXPECT_EQ(((pPSR_t)&PSR)->ps , 0);
    EXPECT_EQ(((pPSR_t)&PSR)->et , 1);
    EXPECT_EQ(((pPSR_t)&PSR)->ef , 1);

    // reset to another memory location:
    cpu.Reset(0x1180);
    EXPECT_EQ(cpu.GetPC(), 0x1180);
    EXPECT_EQ(cpu.GetnPC(), 0x1184);



}


TEST_F(CPUTrapsTest, Traps_PSR_PS_bit)
{

    u32 PSR = cpu.GetPSR(); 
    
    DecodeStruct d;
    d.PSR = cpu.GetPSR(); 
    d.p = (pPSR_t)&(d.PSR);


    EXPECT_EQ(((pPSR_t)&PSR)->s , 1);
    EXPECT_EQ(((pPSR_t)&PSR)->ps , 0);
 
    // Trap the CPU in supervisor mode:
    cpu.Trap(nullptr, 9);

    // Run the CPU through the trap:
    cpu.SetSingleStep(true);
    cpu.Run(0, nullptr);
    // Return from Trap
    do_RETT_instr(0,0,0);
        
    PSR = cpu.GetPSR();
    EXPECT_EQ(((pPSR_t)&PSR)->s , 1);
    EXPECT_EQ(((pPSR_t)&PSR)->ps , 1); // PS should now be set
 
    // Change to user mode:
    PSR = PSR & ~(1 << 7);
    cpu.SetPSR(PSR);

    // Trap the CPU in user mode:
    cpu.Trap(nullptr, 9);
    // Run the CPU through the trap:
    cpu.SetSingleStep(true);
    cpu.Run(0, nullptr);
    // Return from Trap
    do_RETT_instr(0,0,0);




    PSR = cpu.GetPSR();
    EXPECT_EQ(((pPSR_t)&PSR)->s , 0);
    EXPECT_EQ(((pPSR_t)&PSR)->ps , 0); // PS should not be set
 
    // Change to super mode:
    PSR = PSR | (1 << 7);
    cpu.SetPSR(PSR);
    
    EXPECT_EQ(((pPSR_t)&PSR)->s , 1);
    EXPECT_EQ(((pPSR_t)&PSR)->ps , 0);
 
    // Trap the CPU again in supervisor mode:
    cpu.Trap(nullptr, 9);
    // Run the CPU through the trap:
    cpu.SetSingleStep(true);
    cpu.Run(0, nullptr);
    // Return from Trap
    do_RETT_instr(0,0,0);



    PSR = cpu.GetPSR();
    EXPECT_EQ(((pPSR_t)&PSR)->s , 1);
    EXPECT_EQ(((pPSR_t)&PSR)->ps , 1); // PS should now be set
 


}

