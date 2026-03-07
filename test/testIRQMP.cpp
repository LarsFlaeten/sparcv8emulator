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

    MCtrl mctrl{};
    
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

TEST_F(IRQMPTest, trigger_irq_CheckPending)
{
    IRQMP intc(1);
    CPU cpu(mctrl, intc, 0);
    intc.set_cpu_ptr(&cpu, 0);

    intc.trigger_irq(8);
    u32 ipend = intc.read(0x04);
    ASSERT_TRUE(ipend & 0x1 << 8);

    // No pending for cpu 0, since interrupt mask is not set
    unsigned int irq = intc.get_irq_hint(0);
    ASSERT_EQ(irq, 0);

    // Clear IPEND
    intc.write(0x04, 0);
    ipend = intc.read(0x04);
    ASSERT_EQ(ipend, 0x0U);

    // Enable MP interrupt mask for IRQ 8:
    intc.write(0x40, 0x1U << 8);

    intc.trigger_irq(8);
 
    ipend = intc.read(0x04);
    ASSERT_TRUE(ipend & 0x1 << 8);

    irq = intc.get_irq_hint(0);
    ASSERT_EQ(irq, 8);
    intc.clear_irq(8,0);
    ASSERT_EQ(intc.get_irq_hint(0), 0);
 
   
    intc.write(0x04, 0x0); 
    ASSERT_EQ(intc.get_irq_hint(0), 0);

    intc.trigger_irq(3);
    intc.trigger_irq(8);
    intc.trigger_irq(11);

    // The mask will ensure only IRQ8 is pending:
    ASSERT_EQ(intc.get_irq_hint(0), 8);
    intc.clear_irq(8,0);
    ASSERT_EQ(intc.get_irq_hint(0), 0);

    
    // Enable MP interrupt mask for IRQ 3,8,11:
    intc.write(0x40, 0x1 << 3 | 0x1 << 8 | 0x1 << 11);

    intc.trigger_irq(3);
    intc.trigger_irq(8);
    intc.trigger_irq(11);

   
    ASSERT_EQ(intc.get_irq_hint(0), 11);
    intc.clear_irq(11,0);

    ASSERT_EQ(intc.get_irq_hint(0), 8);
    intc.clear_irq(8,0);

    ASSERT_EQ(intc.get_irq_hint(0), 3);
    intc.clear_irq(3,0);

    ASSERT_EQ(intc.read(0x04), 0);

    ASSERT_EQ(intc.get_irq_hint(0), 0);
    intc.clear_irq(0,0);

    ASSERT_EQ(intc.read(0x04), 0);





}


TEST_F(IRQMPTest, InterruptPriorityTest_PendingDoesNotGetLost)
{
    IRQMP intc(1);
    CPU cpu(mctrl, intc, 0);
    intc.set_cpu_ptr(&cpu, 0);

    cpu.reset(0x0);
    // enable traps:
    cpu.set_psr(cpu.get_psr() | (0x1u << 5));

    // Enable MP interrupt mask for IRQ 8:
    intc.write(0x40, 0b1 << 8);
    // Enable MP interrupt mask for IRQ 3,8,11:
    intc.write(0x40, 0x1 << 3 | 0x1 << 8 | 0x1 << 12);


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
    intc.trigger_irq(8);         // uses IRQMP logic for delivering
    //cpu.run(1, nullptr);        // allow CPU to observe IRL change

    // CPU is in trap, so must NOT take IRQ yet
    EXPECT_EQ(cpu.get_irl(), 0)
        << "Timer IRQ must be pending, even while in trap.";
    intc.trigger_irq(3);  
    EXPECT_EQ(intc.get_irq_hint(0), 8);

    //
    // 3) Trigger a higher-priority interrupt (IRL=12)
    //
    intc.trigger_irq(12);
    //cpu.run(1, nullptr);

    // Now highest priority must be selected
    EXPECT_EQ(intc.get_irq_hint(0), 12)
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
    intc.clear_irq(12,0);
    
    // IRL should now fall back to pending IRQ 8
    EXPECT_EQ(intc.get_irq_hint(0), 8)
        << "After clearing IRQ12, pending timer IRQ8 must be restored.";

    intc.clear_irq(8,0);
    
    // IRL should now fall back to pending IRQ 3
    EXPECT_EQ(intc.get_irq_hint(0), 3)
        << "After clearing IRQ8, pending IRQ3 must be restored.";

}

TEST_F(IRQMPTest, ICLEAR_clears_ipend_bits_1_to_15_only) {
    IRQMP intc(3);

    // 1) Seed IPEND with a known pattern: all bits 0..15 set.
    // (Bit 0 is "reserved" but we set it to verify it is NOT cleared.)
    intc.write(IRQMP_IPEND_OS, 0x0000FFFFu);

    ASSERT_EQ(intc.read(IRQMP_IPEND_OS) & 0x0000FFFFu, 0x0000FFFFu);

    // 2) Writing 0 to ICLEAR has no effect.
    intc.write(IRQMP_ICLEAR_OS, 0x00000000u);
    ASSERT_EQ(intc.read(IRQMP_IPEND_OS) & 0x0000FFFFu, 0x0000FFFFu);

    // 3) Clear a single bit in range 1..15 (example: bit 13)
    intc.write(IRQMP_ICLEAR_OS, (1u << 13));
    {
        u32 ipend = intc.read(IRQMP_IPEND_OS);
        ASSERT_EQ((ipend & (1u << 13)), 0u);          // bit 13 cleared
        ASSERT_NE((ipend & (1u << 12)), 0u);          // neighbor unchanged
        ASSERT_NE((ipend & (1u << 14)), 0u);          // neighbor unchanged
        ASSERT_NE((ipend & (1u << 0)),  0u);          // bit 0 unchanged (reserved)
    }

    // 4) Clear multiple bits in range 1..15 at once
    const u32 multi = (1u << 1) | (1u << 7) | (1u << 15);
    intc.write(IRQMP_ICLEAR_OS, multi);
    {
        u32 ipend = intc.read(IRQMP_IPEND_OS);
        ASSERT_EQ((ipend & (1u << 1)),  0u);
        ASSERT_EQ((ipend & (1u << 7)),  0u);
        ASSERT_EQ((ipend & (1u << 15)), 0u);

        // Still keep reserved bit 0 set.
        ASSERT_NE((ipend & (1u << 0)), 0u);
    }

    // 5) Extended interrupt bits (16+) are ignored in your emulator.
    // Verify writing them does NOT affect lower 0..15 bits.
    const u32 before = intc.read(IRQMP_IPEND_OS) & 0x0000FFFFu;
    intc.write(IRQMP_ICLEAR_OS, 0xFFFF0000u);
    const u32 after  = intc.read(IRQMP_IPEND_OS) & 0x0000FFFFu;
    ASSERT_EQ(after, before);
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
    u32 mpstat = intc.read(0x10);
    u8 numcpus = ((mpstat >> LEON3_IRQMPSTATUS_CPUNR) & 0xf) + 1;
    bool broadcast = (mpstat >> LEON3_IRQMPSTATUS_BROADCAST) & 0x1;
    ASSERT_EQ(numcpus, 1);
    ASSERT_FALSE(broadcast);
    
}



TEST_F(IRQMPTest, MPStatus_Broadcast)
{

    IRQMP intc(2);
    // Read MPSTAT
    u32 mpstat = intc.read(0x10);
    u8 numcpus = ((mpstat >> LEON3_IRQMPSTATUS_CPUNR) & 0xf) + 1;
    bool broadcast = (mpstat >> LEON3_IRQMPSTATUS_BROADCAST) & 0x1;
    ASSERT_EQ(numcpus, 2);
    ASSERT_TRUE(broadcast);

}

TEST_F(IRQMPTest, BroadcastRegMask)
{
    IRQMP intc(2);

    ASSERT_EQ(intc.read(LEON3_BRDCST_REG), 0);

    // Write all ones, check that it is masked correctly:
    intc.write(LEON3_BRDCST_REG, 0xffffffff);

    ASSERT_EQ(intc.read(LEON3_BRDCST_REG) & 0x1, 0);

    ASSERT_EQ(intc.read(LEON3_BRDCST_REG) >> 1, 0x7fff);

    ASSERT_EQ((intc.read(LEON3_BRDCST_REG) >> 1) & 0x1, 0x1);

    ASSERT_EQ((intc.read(LEON3_BRDCST_REG) >> 15) & 0x1, 0x1);

    ASSERT_EQ(intc.read(LEON3_BRDCST_REG) >> 16, 0x0);
    

}


    TEST_F(IRQMPTest, SMP_init)
    {

        IRQMP intc(3);

        // Read MPSTAT
        u32 mpstat = intc.read(0x10);
        u8 numcpus = ((mpstat >> LEON3_IRQMPSTATUS_CPUNR) & 0xf) + 1;
        bool broadcast = (mpstat >> LEON3_IRQMPSTATUS_BROADCAST) & 0x1;
        ASSERT_EQ(numcpus, 3);
        ASSERT_TRUE(broadcast);

        bool eirq = (mpstat >> 26) & 0x1;
        ASSERT_FALSE(eirq);

        // Check that only cpu 0 is running, 1 and 2 should be in powerdown
        ASSERT_EQ(mpstat & 0x1, 0); // 0 = running
        ASSERT_EQ((mpstat >> 1) & 0x1, 1); // 1 = powerdown
        ASSERT_EQ((mpstat >> 2) & 0x1, 1); // 1 = powerdown
        

    }

TEST_F(IRQMPTest, SMP_correctPIMASK)
{

    IRQMP intc(8);

    CPU cpu(mctrl, intc, 0);
    // Just use the saem CPU for all 8
    intc.set_cpu_ptr(&cpu, 0);
    intc.set_cpu_ptr(&cpu, 1);
    intc.set_cpu_ptr(&cpu, 2);
    intc.set_cpu_ptr(&cpu, 3);
    intc.set_cpu_ptr(&cpu, 4);
    intc.set_cpu_ptr(&cpu, 5);
    intc.set_cpu_ptr(&cpu, 6);
    intc.set_cpu_ptr(&cpu, 7);

    intc.write(0x40, 0x1U << 1);
    intc.write(0x44, 0x1U << 2);
    intc.write(0x48, 0x1U << 3);
    intc.write(0x4C, 0x1U << 4);
    intc.write(0x50, 0x1U << 5);
    intc.write(0x54, 0x1U << 6);
    intc.write(0x58, 0x1U << 7);
    intc.write(0x5C, 0x1U << 8);
    
    intc.trigger_irq(1);
    intc.trigger_irq(2);
    intc.trigger_irq(3);
    intc.trigger_irq(4);
    intc.trigger_irq(5);
    intc.trigger_irq(6);
    intc.trigger_irq(7);
    intc.trigger_irq(8);

    ASSERT_EQ(intc.get_irq_hint(0), 1);
    ASSERT_EQ(intc.get_irq_hint(1), 2);
    ASSERT_EQ(intc.get_irq_hint(2), 3);
    ASSERT_EQ(intc.get_irq_hint(3), 4);
    ASSERT_EQ(intc.get_irq_hint(4), 5);
    ASSERT_EQ(intc.get_irq_hint(5), 6);
    ASSERT_EQ(intc.get_irq_hint(6), 7);
    ASSERT_EQ(intc.get_irq_hint(7), 8);
    
    // Mask again, should now get no irq pending
    intc.write(0x50, 0x0U);
    ASSERT_EQ(intc.get_irq_hint(4), 0);

    // Clear all
    intc.clear_irq(1, 0);
    ASSERT_EQ(intc.get_irq_hint(0), 0);

    intc.clear_irq(2, 1);
    intc.clear_irq(3, 2);
    intc.clear_irq(4, 3);
    intc.clear_irq(5, 4);
    intc.clear_irq(6, 5);
    intc.clear_irq(7, 6);
    intc.clear_irq(8, 7);

    // enable broadcast of all irqs 1-8
    intc.write(0x14, 0x1FEU);

    intc.trigger_irq(1);
    intc.trigger_irq(2);
    intc.trigger_irq(3);
    intc.trigger_irq(4);
    intc.trigger_irq(5);
    intc.trigger_irq(6);
    intc.trigger_irq(7);
    intc.trigger_irq(8);

    // Should get same result due to masking:
    ASSERT_EQ(intc.get_irq_hint(0), 1);
    ASSERT_EQ(intc.get_irq_hint(1), 2);
    ASSERT_EQ(intc.get_irq_hint(2), 3);
    ASSERT_EQ(intc.get_irq_hint(3), 4);
    ASSERT_EQ(intc.get_irq_hint(4), 0);
    ASSERT_EQ(intc.get_irq_hint(5), 6);
    ASSERT_EQ(intc.get_irq_hint(6), 7);
    ASSERT_EQ(intc.get_irq_hint(7), 8);
    
    // Also unmask 13 for a few of them
    intc.write(0x40, 0x1U << 1 | 0x1U << 13);
    intc.write(0x44, 0x1U << 2);
    intc.write(0x48, 0x1U << 3);
    intc.write(0x4C, 0x1U << 4);
    intc.write(0x50, 0x1U << 5);
    intc.write(0x54, 0x1U << 6 | 0x1U << 13);
    intc.write(0x58, 0x1U << 7 | 0x1U << 13);
    intc.write(0x5C, 0x1U << 8);
    
    // enable broadcast of all irqs 1-8 + 13
    intc.write(0x14, 0x1FEU | 0x1U << 13);


    intc.trigger_irq(13);
    ASSERT_EQ(intc.get_irq_hint(0), 13);
    ASSERT_EQ(intc.get_irq_hint(1), 2);
    ASSERT_EQ(intc.get_irq_hint(2), 3);
    ASSERT_EQ(intc.get_irq_hint(3), 4);
    ASSERT_EQ(intc.get_irq_hint(4), 0); // Still 0, since IRL was not latched due to earlier mask
    ASSERT_EQ(intc.get_irq_hint(5), 13);
    ASSERT_EQ(intc.get_irq_hint(6), 13);
    ASSERT_EQ(intc.get_irq_hint(7), 8);



}

TEST_F(IRQMPTest, SMP_broadcast_irl8)
{

    IRQMP intc(3);

    CPU cpu(mctrl, intc, 0);
    // Just use the saem CPU for all three
    intc.set_cpu_ptr(&cpu, 0);
    intc.set_cpu_ptr(&cpu, 1);
    intc.set_cpu_ptr(&cpu, 2);


    // enable broadcast for interrupt 8:
    intc.write(0x14, 0x1 << 8);
    // Unmask 3 and 8 for all cpus:
    intc.write(0x40, 0x1U << 3 | 0x1U << 8);
    intc.write(0x44, 0x1U << 3 | 0x1U << 8);
    intc.write(0x48, 0x1U << 3 | 0x1U << 8);
    

    // Trigger irl 3:
    intc.trigger_irq(3);

    ASSERT_EQ(intc.get_irq_hint(0), 3);

    intc.trigger_irq(8); // Should trigger broadcast to all CPUs

    ASSERT_EQ(intc.get_irq_hint(0), 8);
    ASSERT_EQ(intc.get_irq_hint(1), 8);
    ASSERT_EQ(intc.get_irq_hint(2), 8);
    



}

TEST_F(IRQMPTest, SMP_MPSTAT_wakeup)
{

    IRQMP intc(3);

    CPU cpu(mctrl, intc, 0);
    // Just use the saem CPU for all three
    intc.set_cpu_ptr(&cpu, 0);
    intc.set_cpu_ptr(&cpu, 1);
    intc.set_cpu_ptr(&cpu, 2);

    auto mpstat = intc.read(0x10);
    ASSERT_EQ(mpstat & 0x1U, 0);
    ASSERT_EQ((mpstat >> 1) & 0x1U, 1); // cpu 1 should be in powerdown
    ASSERT_EQ((mpstat >> 2) & 0x1U, 1); // cpu 2 should be in powerdown
    
    // Wake up 1 & 2:
    intc.write(0x10, mpstat | (0x1 << 1));
    intc.write(0x10, mpstat | (0x1 << 2));
    
    mpstat = intc.read(0x10);
    ASSERT_EQ((mpstat >> 1) & 0x1U, 0); // cpu 1 should be running
    ASSERT_EQ((mpstat >> 2) & 0x1U, 0); // cpu 2 should be running




}

TEST_F(IRQMPTest, SMP_active_cpus_bookkeeping)
{

    IRQMP intc(8);

    CPU cpu(mctrl, intc, 0);
    // Just use the same CPU for all three
    intc.set_cpu_ptr(&cpu, 0);
    intc.set_cpu_ptr(&cpu, 1);
    intc.set_cpu_ptr(&cpu, 2);
    intc.set_cpu_ptr(&cpu, 3);
    intc.set_cpu_ptr(&cpu, 4);
    intc.set_cpu_ptr(&cpu, 5);
    intc.set_cpu_ptr(&cpu, 6);
    intc.set_cpu_ptr(&cpu, 7);

    // Set broadcast for IRL 8:
    intc.write(0x14U, 0x1U << 8);

    ASSERT_EQ(intc.get_number_active_cpus(), 0)
        << "No CPUS should be marked as active in the barrier IRQ";

    // Read mpstatus, check that only CPU 0 is running
    auto mpstat = intc.read(0x10);
    ASSERT_EQ(mpstat >> 28, 8-1) << "8 CPUS should exist for this test case";
    ASSERT_EQ(mpstat & 0xFFFFU, 0xFFFEU) << "Only CPU0 should be running"; // All bits except bit 0 set
    
    // The number of CPUs participating in barrier ops shohuld still be zero:
    ASSERT_EQ(intc.get_number_active_cpus(), 0);

    // Unmask IRL 8 for cpu 0:
    intc.write(0x40, 0x1U << 8);
    // The number of CPUs participating in barrier ops should now be 1:
    ASSERT_EQ(intc.get_number_active_cpus(), 1);
    
    // Check that IRL 8 is only visible to CPU 0
    intc.trigger_irq(8);
    ASSERT_EQ(intc.get_irq_hint(0), 8);
    ASSERT_EQ(intc.get_irq_hint(1), 0);
    ASSERT_EQ(intc.get_irq_hint(2), 0);
    ASSERT_EQ(intc.get_irq_hint(3), 0);
    ASSERT_EQ(intc.get_irq_hint(4), 0);
    ASSERT_EQ(intc.get_irq_hint(5), 0);
    ASSERT_EQ(intc.get_irq_hint(6), 0);
    ASSERT_EQ(intc.get_irq_hint(7), 0);
    intc.clear_irq(8,0);

    // Start CPU 1:
    intc.write(0x10, 0x1U << 1);
    // The number of CPUs participating in barrier ops should still be 1:
    ASSERT_EQ(intc.get_number_active_cpus(), 1);
    // Unmask IRL 8 for CPU 1:
    intc.write(0x44, 0x1U << 8);
    // The number of CPUs participating in barrier ops should now be 2:
    ASSERT_EQ(intc.get_number_active_cpus(), 2);
    // ..and both CPU 0 and 1 sohuld see the broadcasted IRL 8:
    intc.trigger_irq(8);
    ASSERT_EQ(intc.get_irq_hint(0), 8);
    ASSERT_EQ(intc.get_irq_hint(1), 8);
    ASSERT_EQ(intc.get_irq_hint(2), 0);
    ASSERT_EQ(intc.get_irq_hint(3), 0);
    ASSERT_EQ(intc.get_irq_hint(4), 0);
    ASSERT_EQ(intc.get_irq_hint(5), 0);
    ASSERT_EQ(intc.get_irq_hint(6), 0);
    ASSERT_EQ(intc.get_irq_hint(7), 0);
    intc.clear_irq(8,0);
    intc.clear_irq(8,1);
    

    

    



}

TEST_F(IRQMPTest, SMP_PIFORCE_write)
{

    IRQMP intc(8);

    CPU cpu(mctrl, intc, 0);
    // Just use the same CPU for all three
    intc.set_cpu_ptr(&cpu, 0);
    intc.set_cpu_ptr(&cpu, 1);
    intc.set_cpu_ptr(&cpu, 2);
    intc.set_cpu_ptr(&cpu, 3);
    intc.set_cpu_ptr(&cpu, 4);
    intc.set_cpu_ptr(&cpu, 5);
    intc.set_cpu_ptr(&cpu, 6);
    intc.set_cpu_ptr(&cpu, 7);

    // Imitate linux SMP boot:
    intc.write(0x40, 0xa100); // PIMASK cpu 0, IRL 15, 13, 8
    intc.write(0x14, 0x100); // BCST for IRL 8
    
    // Wake up second cpu:
    intc.write(0x10, 0x2);
    intc.write(0x44, 0xa100); // PIMASK cpu 1, IRL 15, 13, 8

    // Force IRL 13 for second cpu:
    intc.write(0x84, 0x2000);

    ASSERT_EQ(intc.get_irq_hint(1), 13);
    ASSERT_EQ(intc.get_irq_hint(0), 0);

    // Force IRL 13 for first cpu:
    intc.write(0x80, 0x2000);

    ASSERT_EQ(intc.get_irq_hint(1), 13);
    ASSERT_EQ(intc.get_irq_hint(0), 13);



}

