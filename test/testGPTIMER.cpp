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
    
    ASSERT_EQ(timer.VendorId(), VENDOR_GAISLER );
    ASSERT_EQ(timer.DeviceId(), GAISLER_GPTIMER );
 
    u32 scaler = timer.Read(0x0);
    ASSERT_EQ(scaler, 0xff);

    u32 conf = timer.Read(0x8);
    // One enabled timer, irw 8, 7 implemented
    ASSERT_EQ(conf, 0x1 << 16 | 8 << 3 | 0x7);

    ASSERT_EQ((conf >> 3) & 0b11111, 8);



}

TEST_F(GPTIMERTest, TimerTickOnce)
{
    GPTIMER timer(8, 0x31);
   
    // Tick through the prescaler so that timer 1 counter is decremented 
    for(int i = 0; i <= 0x31; ++i)
        timer.Tick();

    u32 cnt = timer.Read(0x10);
    u32 rld = timer.Read(0x14);
    u32 ctrl = timer.Read(0x18);

    ASSERT_EQ(rld ,0x270f);
    ASSERT_EQ(cnt ,0x270e); // rld minus one
    ASSERT_EQ(ctrl & 0x1, 0x1);
}

TEST_F(GPTIMERTest, TimerTicksPrescalerUnderflow)
{
    GPTIMER timer(8, 0x31);

    timer.Write(0x0, 0x1); // Set scaler to 1
    timer.Write(0x4, 0x1); // Set prescaler to 1
    ASSERT_EQ(timer.Read(0x10) ,0x270f);
    ASSERT_EQ(timer.Read(0x14) ,0x270f);
    ASSERT_EQ(timer.Read(0x18) & 0x1, 0x1);
    ASSERT_EQ(timer.Read(0x04) ,0x1);
 
    for(u64 j = 0x2; j > 0; --j)
        timer.Tick();
    
    // Read counters of timers, ony first one shuold be decremented
    ASSERT_EQ(timer.Read(0x10), 0x270e);
    ASSERT_EQ(timer.Read(0x20), 0x270f);
    ASSERT_EQ(timer.Read(0x30), 0x270f);
    ASSERT_EQ(timer.Read(0x40), 0x270f);
    ASSERT_EQ(timer.Read(0x50), 0x270f);
    u32 r7 = timer.Read(0x60); ASSERT_EQ(r7, 0x270f);
    ASSERT_EQ(timer.Read(0x70), 0x270f);
}


TEST_F(GPTIMERTest, TimerEnableOthers)
{
    GPTIMER timer(8, 0x31);


    timer.Write(0x0, 0x1); // Set scaler to 1
    timer.Write(0x4, 0x1); // Set prescaler to 1
                           //
    u32 ctrl = timer.Read(0x8);
    timer.Write(0x8, ctrl | (0b10101 << 16)); // Enable timer 0, 2, 4
    timer.Reset();

    ASSERT_EQ(timer.Read(0x10) ,0x270f);
    ASSERT_EQ(timer.Read(0x14) ,0x270f); // rld minus one
    ASSERT_EQ(timer.Read(0x18) & 0x1, 0x1);
    ASSERT_EQ(timer.Read(0x04) ,0x1);
 
    ASSERT_EQ((timer.Read(0x08) >> 16) & 0x7f, 0b10101);
 
    for(u64 j = 0x2; j > 0; --j)
        timer.Tick();
    
    // Read counters of timers, ony first, third and fifth  shuold be decremented
    ASSERT_EQ(timer.Read(0x10), 0x270e);
    ASSERT_EQ(timer.Read(0x20), 0x270f);
    ASSERT_EQ(timer.Read(0x30), 0x270e);
    ASSERT_EQ(timer.Read(0x40), 0x270f);
    ASSERT_EQ(timer.Read(0x50), 0x270e);
    u32 r7 = timer.Read(0x60); ASSERT_EQ(r7, 0x270f);
    ASSERT_EQ(timer.Read(0x70), 0x270f);

}

TEST_F(GPTIMERTest, TimerIRQOnUnderflow)
{
 
    GPTIMER timer;

    timer.Write(0x0, 0x1); // Set scaler to 1
    timer.Write(0x4, 0x1); // Set reload to 1
                           //
    u32 ctrl = timer.Read(0x8);
    timer.Write(0x8, ctrl | (0b10101 << 16)); // Enable timer 0, 2, 4
    timer.Reset();

    // Set cntval to lower value
    timer.Write(0x10, 0x2000000);
    timer.Write(0x30, 0x2000000);
    timer.Write(0x50, 0x2000000);




    for(u64 j = 0x4000000; j > 0; --j)
        timer.Tick();
   
    ASSERT_EQ(timer.Read(0x10), 0x0);
    ASSERT_EQ(timer.Read(0x20), 0x270f);
    ASSERT_EQ(timer.Read(0x30), 0x0);
    ASSERT_EQ(timer.Read(0x40), 0x270f);
    ASSERT_EQ(timer.Read(0x50), 0x0);
    ASSERT_EQ(timer.Read(0x60), 0x270f);
    ASSERT_EQ(timer.Read(0x70), 0x270f);

    // Tick again twice, to empty prescaler.
    // Should relaoud counter, not generate IRQ    
    timer.Tick();
    timer.Tick();


    ASSERT_EQ(timer.Read(0x10), 0x270f);
    ASSERT_EQ(timer.Read(0x20), 0x270f);
    ASSERT_EQ(timer.Read(0x30), 0x270f);
    ASSERT_EQ(timer.Read(0x40), 0x270f);
    ASSERT_EQ(timer.Read(0x50), 0x270f);
    ASSERT_EQ(timer.Read(0x60), 0x270f);
    ASSERT_EQ(timer.Read(0x70), 0x270f);
    ASSERT_FALSE(timer.CheckInterrupt());


    timer.Reset();


    // Set cntval to lower value
    timer.Write(0x30, 0x200000);
    // Set interrupt on uderflow:
    timer.InterruptEnable();
    
    for(u64 j = 0x400000; j > 0; --j)
        timer.Tick();
    timer.Tick();
    timer.Tick();

   
    
    ASSERT_TRUE(timer.CheckInterrupt(false));


    // Clear interrupt by setting IP bit to 1
    u32 TCTRL = timer.Read(0x38);
    ASSERT_TRUE((TCTRL >> 4) & 0x1);
    // Write-back of the IP bit will clear it, ref GRIP 36.3.7
    timer.Write(0x38, TCTRL);
    TCTRL = timer.Read(0x38);
    ASSERT_FALSE((TCTRL >> 4) & 0x1);
    ASSERT_FALSE(timer.CheckInterrupt());


    
    timer.Reset();


    // Set cntval to lower value
    timer.Write(0x30, 0x200000);
    // Set interrupt on uderflow:
    timer.InterruptEnable();
    
    for(u64 j = 0x400000; j > 0; --j)
        timer.Tick();
    timer.Tick();
    timer.Tick();

    // Check that timer.CheckInterrupt clear the interrupt when passed true
    ASSERT_TRUE(timer.CheckInterrupt(true));
    ASSERT_FALSE(timer.CheckInterrupt());



}

TEST_F(GPTIMERTest, TimerExplicitReload)
{
 
    GPTIMER timer;

    timer.Write(0x0, 0x1); // Set scaler to 1
    timer.Write(0x4, 0x1); // Set reload to 1
                           //
    u32 ctrl = timer.Read(0x8);
    timer.Write(0x8, ctrl | (0b10101 << 16)); // Enable timer 0, 2, 4
    timer.Reset();

    // Set cntval to lower value
    timer.Write(0x10, 0x20000);
    timer.Write(0x30, 0x20000);
    timer.Write(0x50, 0x20000);

    // Run the timer a bit
    for(u64 j = 0x20000; j > 0; --j)
        timer.Tick();
   
    ASSERT_EQ(timer.Read(0x10), 0x10000);
    ASSERT_EQ(timer.Read(0x20), 0x270f);
    ASSERT_EQ(timer.Read(0x30), 0x10000);
    ASSERT_EQ(timer.Read(0x40), 0x270f);
    ASSERT_EQ(timer.Read(0x50), 0x10000);
    ASSERT_EQ(timer.Read(0x60), 0x270f);
    ASSERT_EQ(timer.Read(0x70), 0x270f);

    // Reload counter for timer 4:
    u32 TCTRL = timer.Read(0x58);
    ASSERT_EQ((TCTRL >> 2) & 0xf, 0x0);
    TCTRL |= (0x1 << 2); // SET LD bit to 1
    timer.Write(0x58, TCTRL);
    ASSERT_EQ(timer.Read(0x50), 0x270f);
    ASSERT_EQ((timer.Read(0x58) >> 2) & 0x1, 0x0); // ld bit should be cleared 



}

