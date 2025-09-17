#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"
#include "../src/peripherals/GPTIMER.h"
#include "../src/peripherals/gaisler/ambapp.h"




#include <gtest/gtest.h>

#include <cmath>



class GPTIMERTest : public ::testing::Test {

protected:
    GPTIMERTest();

    virtual ~GPTIMERTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();


};



GPTIMERTest::GPTIMERTest()
{  
   	


}

GPTIMERTest::~GPTIMERTest()
{

}

void GPTIMERTest::SetUp()
{


}

void GPTIMERTest::TearDown()
{
}

TEST_F(GPTIMERTest, TimerEnable)
{
    GPTIMER timer(8, 0xff);
    
    ASSERT_EQ(timer.vendor_id(), VENDOR_GAISLER );
    ASSERT_EQ(timer.device_id(), GAISLER_GPTIMER );
 
    u32 scaler = timer.read(0x0);
    ASSERT_EQ(scaler, 0xff);

    u32 conf = timer.read(0x8);
    // One enabled timer, irw 8, 7 implemented
    //ASSERT_EQ(conf, 0x1 << 16 | 8 << 3 | 0x7);
    // No enabled timer, SI=1, irq 8, 2 implemented
    ASSERT_EQ(conf, 0x1 << 8 | 8 << 3 | 0x2);


    ASSERT_EQ((conf >> 3) & 0b11111, 8);



}

TEST_F(GPTIMERTest, TimerTickOnce)
{
    GPTIMER timer(8, 0x31);
    u32 conf = timer.read(0x8);
    timer.write(0x8, conf | 1 << 16);
    timer.reset();



    // Tick through the prescaler so that timer 1 counter is decremented 
    for(int i = 0; i <= 0x31; ++i)
        timer.Tick();

    u32 cnt = timer.read(0x10);
    u32 rld = timer.read(0x14);
    u32 ctrl = timer.read(0x18);

    ASSERT_EQ(rld ,0x270f);
    ASSERT_EQ(cnt ,0x270e); // rld minus one
    ASSERT_EQ(ctrl & 0x1, 0x1);
}

TEST_F(GPTIMERTest, TimerTicksPrescalerUnderflow)
{
    GPTIMER timer(8, 0x31);
    u32 conf = timer.read(0x8);
    timer.write(0x8, conf | 1 << 16);
    timer.reset();


    timer.write(0x0, 0x1); // Set scaler to 1
    timer.write(0x4, 0x1); // Set prescaler to 1
    ASSERT_EQ(timer.read(0x10) ,0x270f);
    ASSERT_EQ(timer.read(0x14) ,0x270f);
    ASSERT_EQ(timer.read(0x18) & 0x1, 0x1);
    ASSERT_EQ(timer.read(0x04) ,0x1);
 
    for(u64 j = 0x2; j > 0; --j)
        timer.Tick();
    
    // Read counters of timers, ony first one shuold be decremented
    ASSERT_EQ(timer.read(0x10), 0x270e);
    ASSERT_EQ(timer.read(0x20), 0x270f);
    ASSERT_EQ(timer.read(0x30), 0x0);
    ASSERT_EQ(timer.read(0x40), 0x0);
    ASSERT_EQ(timer.read(0x50), 0x0);
    ASSERT_EQ(timer.read(0x60), 0x0);
    ASSERT_EQ(timer.read(0x70), 0x0);
}


TEST_F(GPTIMERTest, TimerEnableOthers)
{
    GPTIMER timer(8, 0x31);


    timer.write(0x0, 0x1); // Set scaler to 1
    timer.write(0x4, 0x1); // Set prescaler to 1
                           //
    u32 ctrl = timer.read(0x8);
    timer.write(0x8, ctrl | (0b11 << 16)); // Enable timer 1, 2
    timer.reset();

    ASSERT_EQ(timer.read(0x10) ,0x270f);
    ASSERT_EQ(timer.read(0x14) ,0x270f);
    ASSERT_EQ(timer.read(0x18) & 0x1, 0x1);
    ASSERT_EQ(timer.read(0x04) ,0x1);
 
    ASSERT_EQ((timer.read(0x08) >> 16) & 0x7f, 0b00); // Not shown as enabled, reg is not readable
    ASSERT_EQ((timer.read(0x18) ) & 0x1, 0b1);
    ASSERT_EQ((timer.read(0x28) ) & 0x1, 0b1);
                                             
 
    for(u64 j = 0x2; j > 0; --j)
        timer.Tick();
    
    // Read counters of timers, ony first, third and fifth  shuold be decremented
    ASSERT_EQ(timer.read(0x10), 0x270e);
    ASSERT_EQ(timer.read(0x20), 0x270e);
    ASSERT_EQ(timer.read(0x30), 0);
    ASSERT_EQ(timer.read(0x40), 0);
    ASSERT_EQ(timer.read(0x50), 0);
    ASSERT_EQ(timer.read(0x60), 0);
    ASSERT_EQ(timer.read(0x70), 0);

}

TEST_F(GPTIMERTest, TimerIRQOnUnderflow)
{
 
    GPTIMER timer;

    timer.write(0x0, 0x1); // Set scaler to 1
    timer.write(0x4, 0x1); // Set reload to 1
                           //
    u32 ctrl = timer.read(0x8);
    timer.write(0x8, ctrl | (0b10 << 16)); // Enable timer 2, not 1
    timer.reset();

    // Set cntval to some value
    timer.write(0x10, 0x2000000);
    timer.write(0x20, 0x2000000);
    timer.write(0x50, 0x2000000);




    for(u64 j = 0x4000000; j > 0; --j)
        timer.Tick();
   
    ASSERT_EQ(timer.read(0x10), 0x2000000);
    ASSERT_EQ(timer.read(0x20), 0x0);
    ASSERT_EQ(timer.read(0x30), 0x0);
    ASSERT_EQ(timer.read(0x40), 0x0);
    ASSERT_EQ(timer.read(0x50), 0x0);
    ASSERT_EQ(timer.read(0x60), 0x0);
    ASSERT_EQ(timer.read(0x70), 0x0);

    // Tick again twice, to empty prescaler.
    // Should relaoud counter, not generate IRQ    
    timer.Tick();
    timer.Tick();


    ASSERT_EQ(timer.read(0x10), 0x2000000);
    ASSERT_EQ(timer.read(0x20), 0x270f);
    ASSERT_EQ(timer.read(0x30), 0x0);
    ASSERT_EQ(timer.read(0x40), 0x0);
    ASSERT_EQ(timer.read(0x50), 0x0);
    ASSERT_EQ(timer.read(0x60), 0x0);
    ASSERT_EQ(timer.read(0x70), 0x0);
    ASSERT_FALSE(timer.check_interrupt());


    timer.reset();


    // Set cntval to lower value
    timer.write(0x30, 0x200000);
    // Set interrupt on uderflow:
    timer.interrupt_enable();
    
    for(u64 j = 0x400000; j > 0; --j)
        timer.Tick();
    timer.Tick();
    timer.Tick();

   
    
    ASSERT_TRUE(timer.check_interrupt(false));


    // Clear interrupt by setting IP bit to 1
    u32 TCTRL = timer.read(0x28);
    ASSERT_TRUE((TCTRL >> 4) & 0x1);
    // Write-back of the IP bit will clear it, ref GRIP 36.3.7
    timer.write(0x28, TCTRL);
    TCTRL = timer.read(0x28);
    ASSERT_FALSE((TCTRL >> 4) & 0x1);

 
    ASSERT_FALSE(timer.check_interrupt());


    
    timer.reset();


    // Set cntval to lower value
    timer.write(0x20, 0x200000);
    // Set interrupt on uderflow:
    timer.interrupt_enable();
    
    for(u64 j = 0x400000; j > 0; --j)
        timer.Tick();
    timer.Tick();
    timer.Tick();

    // Check that timer.check_interrupt clear the interrupt when passed true
    ASSERT_TRUE(timer.check_interrupt(false));
    ASSERT_TRUE(timer.check_interrupt(true)); 
    ASSERT_FALSE(timer.check_interrupt());



}

TEST_F(GPTIMERTest, TimerNoEnableNoTick)
{
    GPTIMER timer;

    timer.write(0x0, 0x1); // Set scaler to 1
    timer.write(0x4, 0x1); // Set reload to 1
                           //
    //u32 ctrl = timer.read(0x8);
    timer.reset();

    // Set cntval to some value
    timer.write(0x10, 0x20000);
    timer.write(0x20, 0x20000);
    timer.write(0x30, 0x20000); // From here and downwards will have no effect
    timer.write(0x40, 0x20000);
    timer.write(0x50, 0x20000);
    timer.write(0x60, 0x20000);
    timer.write(0x70, 0x20000);

    // Run the timer a bit
    for(u64 j = 0x20000; j > 0; --j)
        timer.Tick();
   
    // NO change in timer value should occur, since no timers are enabled
    ASSERT_EQ(timer.read(0x10), 0x20000);
    ASSERT_EQ(timer.read(0x20), 0x20000);
    ASSERT_EQ(timer.read(0x30), 0x0);
    ASSERT_EQ(timer.read(0x40), 0x0);
    ASSERT_EQ(timer.read(0x50), 0x0);
    ASSERT_EQ(timer.read(0x60), 0x0);
    ASSERT_EQ(timer.read(0x70), 0x0);
}


TEST_F(GPTIMERTest, TimerBootSequenceOps)
{
 
    GPTIMER timer;
    
    // Linux boot sequence
    //
    timer.write(0x18, 0x12); // Restart set, clear interrupt
    timer.write(0x10, 0x0);
    timer.write(0x14, 0x270f);
    timer.write(0x18, 0x0);

    timer.Tick();
    timer.Tick();
    // At this stage, timer is disabled, no innterrupts pending
    ASSERT_FALSE(timer.check_interrupt(false)); 

    timer.Tick();
    timer.Tick();

    timer.write(0x18, 0xf); // Start to enable things. Timer is reloaded and enabled
    auto nctrl = timer.read(0x18); // Bits 2 and 4 will clear themselves, sp values hosul be 0b01011
    ASSERT_EQ(nctrl, 0b01011);
    ASSERT_EQ(timer.read(0x10), 0x270f);
    ASSERT_EQ(timer.read(0x14), 0x270f); 
 

}
  
TEST_F(GPTIMERTest, IP_WritesOf0HasNoEffect)
{
 
    GPTIMER timer;

    timer.write(0x0, 0x2); // Set scaler to 2
    timer.write(0x4, 0x2); // Set reload to 2
 
    u32 ctrl = timer.read(0x8);
    timer.write(0x8, ctrl | (0b1 << 16)); // Enable timer 1
    timer.write(0x18, 0xb); // enable interruptsfor timer 1

    // Set timer to some value
    timer.write(0x10, 0x20000);
 
     // Run the timer a bit, ensure it underflows
    for(u64 j = 0x400003; j > 0; --j)
        timer.Tick();

    // Timer 1 should now signal interrupt:
    ASSERT_EQ((timer.read(0x18) >> 4) & 0x1, 0x1);

    // Try to write 0 to IP bit, should still signal interrupt:
    timer.write(0x18, 0xb);

    // Timer 1 should still signal interrupt:
    ASSERT_EQ((timer.read(0x18) >> 4) & 0x1, 0x1);

    // Try to write 1 to IP bit, this should clear the interrupt:
    timer.write(0x18, 0x1b);

    // Timer 1 should now not signal interrupt:
    ASSERT_EQ((timer.read(0x18) >> 4) & 0x1, 0x0);



}
 

TEST_F(GPTIMERTest, TimerNoRSBitNoReload)
{
 
    GPTIMER timer;

    timer.write(0x0, 0x2); // Set scaler to 2
    timer.write(0x4, 0x2); // Set reload to 2
 
    u32 ctrl = timer.read(0x8);
    timer.write(0x8, ctrl | (0b1 << 16)); // Enable timer 1
    timer.reset();

    // Clear RS bit:
    auto nctrl = timer.read(0x18);
    timer.write(0x18, nctrl & ~(0x1 << 1));

    // Set timer to some value
    timer.write(0x10, 0x20000);
 
     // Run the timer a bit, ensure it underflows
    for(u64 j = 0x400003; j > 0; --j)
        timer.Tick();

    // Timer 1 should now be at -1 and be disabled: 
    ASSERT_EQ(timer.read(0x10), 0xffffffff);
    
    nctrl = timer.read(0x18);
    ASSERT_EQ(nctrl & 0x1, 0x0); // Disabled
 

}
 
TEST_F(GPTIMERTest, TimerExplicitReload_enableCentrally)
{
 
    GPTIMER timer;

    timer.write(0x0, 0x1); // Set scaler to 1
    timer.write(0x4, 0x1); // Set reload to 1
                           //
    u32 ctrl = timer.read(0x8);
    timer.write(0x8, ctrl | (0b01 << 16)); // Enable timer 1, 3, 5
    timer.reset();

    // Check enable bit on individual timers and CONFIG reg
    ASSERT_EQ((timer.read(0x8) >> 16) & 0x7f, 0b00); // Not shown as enabled, reg is not readable 
    ASSERT_EQ(timer.read(0x18) & 0x1, 0x1); 
    ASSERT_EQ(timer.read(0x28) & 0x1, 0x0); 
    ASSERT_EQ(timer.read(0x38) & 0x1, 0x0); 
    ASSERT_EQ(timer.read(0x48) & 0x1, 0x0); 
    ASSERT_EQ(timer.read(0x58) & 0x1, 0x0); 
    ASSERT_EQ(timer.read(0x68) & 0x1, 0x0); 
    ASSERT_EQ(timer.read(0x78) & 0x1, 0x0); 
 




    // Set cntval to lower value
    timer.write(0x10, 0x20000);
    timer.write(0x30, 0x20000);
    timer.write(0x50, 0x20000);

    // Run the timer a bit
    for(u64 j = 0x20000; j > 0; --j)
        timer.Tick();
   
    ASSERT_EQ(timer.read(0x10), 0x10000);
    ASSERT_EQ(timer.read(0x20), 0x270f);
    ASSERT_EQ(timer.read(0x30), 0x0);
    ASSERT_EQ(timer.read(0x40), 0x0);
    ASSERT_EQ(timer.read(0x50), 0x0);
    ASSERT_EQ(timer.read(0x60), 0x0);
    ASSERT_EQ(timer.read(0x70), 0x0);

    // Reload counter for timer 1:
    u32 TCTRL = timer.read(0x18);
    ASSERT_EQ((TCTRL >> 2) & 0x1, 0x0);
    TCTRL |= (0x1 << 2); // SET LD bit to 1
    timer.write(0x18, TCTRL);
    ASSERT_EQ(timer.read(0x10), 0x270f);
    ASSERT_EQ((timer.read(0x18) >> 2) & 0x1, 0x0); // ld bit should be cleared 



}
TEST_F(GPTIMERTest, TimerExplicitReload_LEONState)
{
    GPTIMER timer;
    timer.set_LEON_state();

    ASSERT_EQ(timer.read(0x0), 0x24);
    ASSERT_EQ(timer.read(0x4), 0x31);
    ASSERT_EQ(timer.read(0x8), 0x142);
    ASSERT_EQ(timer.read(0xC), 0x0);
    ASSERT_EQ(timer.read(0x10), 0xffffffff);
    ASSERT_EQ(timer.read(0x14), 0xffffffff);
    ASSERT_EQ(timer.read(0x18), 0x3);
    ASSERT_EQ(timer.read(0x1C), 0x0);
    ASSERT_EQ(timer.read(0x20), 0x0);
    ASSERT_EQ(timer.read(0x24), 0x0);
    ASSERT_EQ(timer.read(0x28), 0x0);
    ASSERT_EQ(timer.read(0x2C), 0x0);












}
 
TEST_F(GPTIMERTest, TimerExplicitReload_enableIndividually)
{
 
    GPTIMER timer;

    timer.write(0x0, 0x1); // Set scaler to 1
    timer.write(0x4, 0x1); // Set reload to 1
                           //

    timer.write(0x18, 0xf);
    timer.write(0x28, 0x0);
    timer.write(0x38, 0xf); // WIll have no effect
    timer.write(0x48, 0x0);
    timer.write(0x58, 0xf);
    timer.write(0x68, 0x0);
    timer.write(0x78, 0x0);








    // Check enable bit on individual timers and CONFIG reg
    ASSERT_EQ((timer.read(0x8) >> 16) & 0x7f, 0b00); // Not visble, this reg is not readable
    ASSERT_EQ(timer.read(0x18) & 0x1, 0x1); 
    ASSERT_EQ(timer.read(0x28) & 0x1, 0x0); 
    ASSERT_EQ(timer.read(0x38) & 0x1, 0x0); 
    ASSERT_EQ(timer.read(0x48) & 0x1, 0x0); 
    ASSERT_EQ(timer.read(0x58) & 0x1, 0x0); 
    ASSERT_EQ(timer.read(0x68) & 0x1, 0x0); 
    ASSERT_EQ(timer.read(0x78) & 0x1, 0x0); 
 


    // Set cntval to lower value
    timer.write(0x10, 0x20000);
    timer.write(0x30, 0x20000);
    timer.write(0x50, 0x20000);

    // Run the timer a bit
    for(u64 j = 0x20000; j > 0; --j)
        timer.Tick();
   
    ASSERT_EQ(timer.read(0x10), 0x10000);
    ASSERT_EQ(timer.read(0x20), 0x270f);
    ASSERT_EQ(timer.read(0x30), 0x0);
    ASSERT_EQ(timer.read(0x40), 0x0);
    ASSERT_EQ(timer.read(0x50), 0x0);
    ASSERT_EQ(timer.read(0x60), 0x0);
    ASSERT_EQ(timer.read(0x70), 0x0);

    // Reload counter for timer 4:
    u32 TCTRL = timer.read(0x18);
    ASSERT_EQ((TCTRL >> 2) & 0x1, 0x0);
    TCTRL |= (0x1 << 2); // SET LD bit to 1
    timer.write(0x18, TCTRL);
    ASSERT_EQ(timer.read(0x10), 0x270f);
    ASSERT_EQ((timer.read(0x18) >> 2) & 0x1, 0x0); // ld bit should be cleared 



}

