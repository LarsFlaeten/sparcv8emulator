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

    MCtrl mctrl;
    MMU mmu;
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
    
        d.psr = cpu.get_psr(); 
        d.p = (pPSR_t)&(d.psr);
       
        cpu.decode(&d);
        
        d.function(&cpu, &d);
        cpu.write_back(&d); 
    }


};



CPUTrapsTest::CPUTrapsTest() : mmu(mctrl), cpu(mmu)
{  
   	


}

CPUTrapsTest::~CPUTrapsTest()
{

}

void CPUTrapsTest::SetUp()
{
    mctrl.attach_bank<RamBank>(0x0, 1*1024*1024); // 1 MB @ 0x0
    
    // Set up IO mapping
    // TODO: Move this MMU functions?
    //for(unsigned a = 0x0; a < 0x100; ++a)
    //    mmu.IOmap[a] = { [&](u32 i)          { return RAM.Read(i/4); },
    //                     [&](u32 i, u32 v)   { RAM.Write(i/4, v);    } };

    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    cpu.reset(entry_va);
 
}

void CPUTrapsTest::TearDown()
{
}

TEST_F(CPUTrapsTest, TestResetState)
{
    EXPECT_EQ(cpu.get_pc(), 0);
    EXPECT_EQ(cpu.get_npc(), 4);

    u32 psr = cpu.get_psr(); 
    
    // Current window pointer should be 0
    EXPECT_EQ(psr & LOBITS5 , 0);
    
    // Supervisor, enable traos and enable fp should all be 1
    EXPECT_EQ((psr >> PSR_SUPER_MODE) & 0x1 , 1);
    EXPECT_EQ((psr >> PSR_PREV_SUPER_MODE) & 0x1 , 0);
    EXPECT_EQ((psr >> PSR_ENABLE_TRAPS) & 0x1 , 1);
    EXPECT_EQ((psr >> PSR_ENABLE_FLOATING_POINT) & 0x1, 1);


    // reset to another memory location:
    cpu.reset(0x1180);
    EXPECT_EQ(cpu.get_pc(), 0x1180);
    EXPECT_EQ(cpu.get_npc(), 0x1184);



}


TEST_F(CPUTrapsTest, Traps_PSR_PS_bit)
{

    u32 psr = cpu.get_psr(); 
    
    DecodeStruct d;
    d.psr = cpu.get_psr(); 
    d.p = (pPSR_t)&(d.psr);


    EXPECT_EQ((psr >> PSR_SUPER_MODE) & 0x1 , 1);
    EXPECT_EQ((psr >> PSR_PREV_SUPER_MODE) & 0x1 , 0);
    
 
    // Trap the CPU in supervisor mode:
    cpu.trap(nullptr, 9);

    // Run the CPU through the trap:
    cpu.set_single_step(true);
    cpu.run(0, nullptr);
    // Return from Trap
    do_RETT_instr(0,0,0);
        
    psr = cpu.get_psr();
    EXPECT_EQ((psr >> PSR_SUPER_MODE) & 0x1 , 1);
    EXPECT_EQ((psr >> PSR_PREV_SUPER_MODE) & 0x1 , 1); // PS should now be set
 
    // Change to user mode:
    psr = psr & ~(1 << 7);
    cpu.set_psr(psr);

    // Trap the CPU in user mode:
    cpu.trap(nullptr, 9);
    // Run the CPU through the trap:
    cpu.set_single_step(true);
    cpu.run(0, nullptr);
    // Return from Trap
    do_RETT_instr(0,0,0);




    psr = cpu.get_psr();
    EXPECT_EQ((psr >> PSR_SUPER_MODE) & 0x1 , 0);
    EXPECT_EQ((psr >> PSR_PREV_SUPER_MODE) & 0x1 , 0); // PS should not be set
 
    // Change to super mode:
    psr = psr | (1 << 7);
    cpu.set_psr(psr);
    
    EXPECT_EQ((psr >> PSR_SUPER_MODE) & 0x1 , 1);
    EXPECT_EQ((psr >> PSR_PREV_SUPER_MODE) & 0x1 , 0);
 
    // Trap the CPU again in supervisor mode:
    cpu.trap(nullptr, 9);
    // Run the CPU through the trap:
    cpu.set_single_step(true);
    cpu.run(0, nullptr);
    // Return from Trap
    do_RETT_instr(0,0,0);



    psr = cpu.get_psr();
    EXPECT_EQ((psr >> PSR_SUPER_MODE) & 0x1 , 1);
    EXPECT_EQ((psr >> PSR_PREV_SUPER_MODE) & 0x1 , 1); // PS should now be set
 


}

