#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"


#include <gtest/gtest.h>

#include <cmath>


class CPUEmuTest : public ::testing::Test {

protected:
    CPUEmuTest();

    virtual ~CPUEmuTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();

    MCtrl mctrl;
    MMU mmu;
    IRQMP intc;
    CPU cpu;
};



CPUEmuTest::CPUEmuTest() : mmu(mctrl), cpu(mmu, intc)
{  
   	


}

CPUEmuTest::~CPUEmuTest()
{

}

void CPUEmuTest::SetUp()
{
    mctrl.attach_bank<RamBank>(0x0, 1*1024*1024); // 1 MB @ 0x0
    
    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    cpu.reset(entry_va);
 
}

void CPUEmuTest::TearDown()
{
}

TEST_F(CPUEmuTest, TestResetState)
{
    EXPECT_EQ(cpu.get_pc(), 0);
    EXPECT_EQ(cpu.get_npc(), 4);

    u32 psr = cpu.get_psr(); 
    
    // Current window pointer should be 0
    EXPECT_EQ(psr & LOBITS5 , 0);
    
    // Supervisor, enable traos and enable fp should all be 1
    EXPECT_EQ((psr >> PSR_SUPER_MODE) & 0x1 , 1);
    EXPECT_EQ((psr >> PSR_ENABLE_TRAPS) & 0x1 , 1);
    EXPECT_EQ((psr >> PSR_ENABLE_FLOATING_POINT) & 0x1, 1);

    // reset to another memory location:
    cpu.reset(0x1180);
    EXPECT_EQ(cpu.get_pc(), 0x1180);
    EXPECT_EQ(cpu.get_npc(), 0x1184);



}


