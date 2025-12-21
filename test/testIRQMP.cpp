#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"
#include "../src/peripherals/IRQMP.h"
#include "../src/peripherals/MCTRL.h"
#include "../src/peripherals/gaisler/ambapp.h"



#include <gtest/gtest.h>

#include <cmath>



class IRQMPTest : public ::testing::Test {

protected:
    IRQMPTest();

    virtual ~IRQMPTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();


};



IRQMPTest::IRQMPTest()
{  
   	


}

IRQMPTest::~IRQMPTest()
{

}

void IRQMPTest::SetUp()
{


}

void IRQMPTest::TearDown()
{
}

TEST_F(IRQMPTest, TriggerIRQ_CheckPending)
{
    IRQMP intc(1);

    intc.TriggerIRQ(8);
    u32 ipend = intc.Read(0x04);
    ASSERT_FALSE(ipend & 0x1 << 8);

    // Enable MP interrupt mask for IRQ 8:
    intc.Write(0x40, 0b1 << 8);

    intc.TriggerIRQ(8);
 
    ipend = intc.Read(0x04);
    ASSERT_TRUE(ipend & 0x1 << 8);

    unsigned int irq = intc.GetNextIRQPending(0);
    ASSERT_EQ(irq, 8);
    intc.ClearIRQ(8);
    ASSERT_EQ(intc.GetNextIRQPending(0), 0);
 
   
    intc.Write(0x04, 0x0); 
    ASSERT_EQ(intc.GetNextIRQPending(0), 0);

    intc.TriggerIRQ(3);
    intc.TriggerIRQ(8);
    intc.TriggerIRQ(11);

    // The mask will ensure only IRQ8 is pending:
    ASSERT_EQ(intc.GetNextIRQPending(0), 8);
    intc.ClearIRQ(8);
    ASSERT_EQ(intc.GetNextIRQPending(0), 0);

    
    // Enable MP interrupt mask for IRQ 3,8,11:
    intc.Write(0x40, 0x1 << 3 | 0x1 << 8 | 0x1 << 11);

    intc.TriggerIRQ(3);
    intc.TriggerIRQ(8);
    intc.TriggerIRQ(11);

   
    ASSERT_EQ(intc.GetNextIRQPending(0), 11);
    intc.ClearIRQ(11);

    ASSERT_EQ(intc.GetNextIRQPending(0), 8);
    intc.ClearIRQ(8);

    ASSERT_EQ(intc.GetNextIRQPending(0), 3);
    intc.ClearIRQ(3);

    ASSERT_EQ(intc.Read(0x04), 0);

    ASSERT_EQ(intc.GetNextIRQPending(0), 0);
    intc.ClearIRQ(0);

    ASSERT_EQ(intc.Read(0x04), 0);





}


TEST_F(IRQMPTest, InterruptPriorityTest_PendingDoesNotGetLost)
{
    IRQMP intc(1);
    MCtrl mctrl{};
    MMU mmu(mctrl);
    CPU cpu(mmu, intc);
    cpu.reset(0x0);
    // enable traps:
    cpu.set_psr(cpu.get_psr() | (0x1u << 5));

    // Enable MP interrupt mask for IRQ 8:
    intc.Write(0x40, 0b1 << 8);
    // Enable MP interrupt mask for IRQ 3,8,11:
    intc.Write(0x40, 0x1 << 3 | 0x1 << 8 | 0x1 << 12);


    //
    // 1) Put CPU into a trap (simulate page fault or similar)
    //
    cpu.trap(nullptr, 9);       // trap 9 (just arbitrary)
    //cpu.run(1, nullptr);        // process trap entry
    ASSERT_NE(cpu.get_trap_type(), 0) 
        << "CPU must be inside a trap for this test.";


    //
    // 2) Trigger timer interrupt (IRL=8)
    //
    intc.TriggerIRQ(8);         // uses IRQMP logic for delivering
    //cpu.run(1, nullptr);        // allow CPU to observe IRL change

    // CPU is in trap, so must NOT take IRQ yet
    EXPECT_EQ(cpu.get_irl(), 0)
        << "Timer IRQ must be pending, even while in trap.";
    intc.TriggerIRQ(3);  
    EXPECT_EQ(intc.GetNextIRQPending(0), 8);

    //
    // 3) Trigger a higher-priority interrupt (IRL=12)
    //
    intc.TriggerIRQ(12);
    //cpu.run(1, nullptr);

    // Now highest priority must be selected
    EXPECT_EQ(intc.GetNextIRQPending(0), 12)
        << "Higher IRQ=12 must override IRL, but lower IRQ=8 must remain pending.";



    //
    // 4) Finish trap handler (RETT)
    //
    // enable traps:
    cpu.set_psr(cpu.get_psr() & ~(0x1u << 5));

    // CPU must enter ISR for 12 first
    EXPECT_NE(cpu.get_trap_type(), 0)
        << "CPU must trap to IRQ 12 handler after trap return.";
    

    //
    // 5) Simulate interrupt handler acknowledging IRQ 12
    //
    intc.ClearIRQ(12);
    
    // IRL should now fall back to pending IRQ 8
    EXPECT_EQ(intc.GetNextIRQPending(0), 8)
        << "After clearing IRQ12, pending timer IRQ8 must be restored.";

    intc.ClearIRQ(8);
    
    // IRL should now fall back to pending IRQ 3
    EXPECT_EQ(intc.GetNextIRQPending(0), 3)
        << "After clearing IRQ8, pending IRQ3 must be restored.";

}

// From linux 5.10.216/leon_amba.h
#define LEON3_IRQMPSTATUS_CPUNR     28
#define LEON3_IRQMPSTATUS_BROADCAST 27
#define LEON3_IRQMPAMPCTRL_NCTRL 28
#define LEON3_BRDCST_REG 0x14
TEST_F(IRQMPTest, MPStatus_SingleCPUNoBroadcast)
{

    IRQMP intc(1);
    // Read MPSTAT
    u32 mpstat = intc.Read(0x10);
    u8 numcpus = ((mpstat >> LEON3_IRQMPSTATUS_CPUNR) & 0xf) + 1;
    bool broadcast = (mpstat >> LEON3_IRQMPSTATUS_BROADCAST) & 0x1;
    ASSERT_EQ(numcpus, 1);
    ASSERT_FALSE(broadcast);
    
}



TEST_F(IRQMPTest, MPStatus_Broadcast)
{

    IRQMP intc(2);
    // Read MPSTAT
    u32 mpstat = intc.Read(0x10);
    u8 numcpus = ((mpstat >> LEON3_IRQMPSTATUS_CPUNR) & 0xf) + 1;
    bool broadcast = (mpstat >> LEON3_IRQMPSTATUS_BROADCAST) & 0x1;
    ASSERT_EQ(numcpus, 2);
    ASSERT_TRUE(broadcast);

}

TEST_F(IRQMPTest, BroadcastRegMask)
{
    IRQMP intc(2);

    ASSERT_EQ(intc.Read(LEON3_BRDCST_REG), 0);

    // Write all ones, check that it is masked correctly:
    intc.Write(LEON3_BRDCST_REG, 0xffffffff);

    ASSERT_EQ(intc.Read(LEON3_BRDCST_REG) & 0x1, 0);

    ASSERT_EQ(intc.Read(LEON3_BRDCST_REG) >> 1, 0x7fff);

    ASSERT_EQ((intc.Read(LEON3_BRDCST_REG) >> 1) & 0x1, 0x1);

    ASSERT_EQ((intc.Read(LEON3_BRDCST_REG) >> 15) & 0x1, 0x1);

    ASSERT_EQ(intc.Read(LEON3_BRDCST_REG) >> 16, 0x0);
    

}


