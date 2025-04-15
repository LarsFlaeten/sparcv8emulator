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

    CPU cpu;
    SDRAM<0x01000000> RAM;  // IO: 0x0, 16 MB of RAM

};



CPUEmuTest::CPUEmuTest()
{  
   	


}

CPUEmuTest::~CPUEmuTest()
{

}

void CPUEmuTest::SetUp()
{
    // Set up IO mapping
    // TODO: Move this MMU functions?
    for(unsigned a = 0x0; a < 0x100; ++a)
        MMU::IOmap[a] = { [&](u32 i)          { return RAM.Read(i/4); },
                         [&](u32 i, u32 v)   { RAM.Write(i/4, v);    } };

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


TEST_F(CPUEmuTest, AddBreakpoint)
{
    cpu.add_user_breakpoint(0x00100000);

    const auto& bps = cpu.get_user_breakpoints();
    ASSERT_FALSE(bps.find(0x00100000) == bps.end());
}


TEST_F(CPUEmuTest, RemoveBreakpoint)
{
    cpu.add_user_breakpoint(0x00100000);

    const auto& bps = cpu.get_user_breakpoints();
    ASSERT_FALSE(bps.find(0x00100000) == bps.end());


    cpu.remove_user_breakpoint(0x00100000);
    ASSERT_TRUE(bps.find(0x00100000) == bps.end());



}


TEST_F(CPUEmuTest, RemoveBreakpoint2)
{
    cpu.add_user_breakpoint(0x00100000);
    cpu.add_user_breakpoint(0x00100000);
    cpu.add_user_breakpoint(0x00100000);
    cpu.add_user_breakpoint(0x00100000);
    cpu.add_user_breakpoint(0x00100000);
    cpu.add_user_breakpoint(0x00100000);
    cpu.add_user_breakpoint(0x00100000);
    cpu.add_user_breakpoint(0x00100000);
    cpu.add_user_breakpoint(0x00100000);
    cpu.add_user_breakpoint(0x00100000);

    const auto& bps = cpu.get_user_breakpoints();
    ASSERT_FALSE(bps.find(0x00100000) == bps.end());


    // All breakpoints above should be removed
    cpu.remove_user_breakpoint(0x00100000);
    ASSERT_TRUE(bps.find(0x00100000) == bps.end());



}
