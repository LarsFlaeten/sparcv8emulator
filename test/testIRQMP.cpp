#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"
#include "../src/peripherals/IRQMP.h"
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
    IRQMP intc;

    intc.TriggerIRQ(8);
    u32 ipend = intc.Read(0x04);
    ASSERT_FALSE(ipend & 0x1 << 8);

    // Enable MP interrupt mask for IRQ 8:
    intc.Write(0x40, 0b1 << 8);

    intc.TriggerIRQ(8);
 
    ipend = intc.Read(0x04);
    ASSERT_TRUE(ipend & 0x1 << 8);

    unsigned int irq = intc.GetNextIRQPending();
    ASSERT_EQ(irq, 8);
    intc.ClearIRQ(8);
    ASSERT_EQ(intc.GetNextIRQPending(), 0);
 
   
    intc.Write(0x04, 0x0); 
    ASSERT_EQ(intc.GetNextIRQPending(), 0);

    intc.TriggerIRQ(3);
    intc.TriggerIRQ(8);
    intc.TriggerIRQ(11);

    // The mask will ensure only IRQ8 is pending:
    ASSERT_EQ(intc.GetNextIRQPending(), 8);
    intc.ClearIRQ(8);
    ASSERT_EQ(intc.GetNextIRQPending(), 0);

    
    // Enable MP interrupt mask for IRQ 3,8,11:
    intc.Write(0x40, 0x1 << 3 | 0x1 << 8 | 0x1 << 11);

    intc.TriggerIRQ(3);
    intc.TriggerIRQ(8);
    intc.TriggerIRQ(11);

   
    ASSERT_EQ(intc.GetNextIRQPending(), 11);
    intc.ClearIRQ(11);

    ASSERT_EQ(intc.GetNextIRQPending(), 8);
    intc.ClearIRQ(8);

    ASSERT_EQ(intc.GetNextIRQPending(), 3);
    intc.ClearIRQ(3);

    ASSERT_EQ(intc.Read(0x04), 0);

    ASSERT_EQ(intc.GetNextIRQPending(), 0);
    intc.ClearIRQ(0);

    ASSERT_EQ(intc.Read(0x04), 0);





}


