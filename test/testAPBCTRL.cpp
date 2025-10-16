#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"
#include "../src/peripherals/APBCTRL.h"




#include <gtest/gtest.h>

#include <cmath>

#define GR712RC_SYSTEMID 0x7120e70
#define GR740_REV0_SYSTEMID 0x07401038
#define GR740_REV1_SYSTEMID 0x07401039


class APBCTRLTest : public ::testing::Test {

protected:
    APBCTRLTest();

    virtual ~APBCTRLTest();

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



APBCTRLTest::APBCTRLTest() : mmu(mctrl), cpu(mmu, intc)
{  
   	


}

APBCTRLTest::~APBCTRLTest()
{

}

void APBCTRLTest::SetUp()
{
    mctrl.attach_bank<APBCTRL>(0x80000000, mctrl);
}

void APBCTRLTest::TearDown()
{
}

TEST_F(APBCTRLTest, AHB_setup)
{
    // We should be able to read and write to 0x8000000 + [0 .. 256 bytes]

    for(size_t i = 0; i < 256; i = i + 4) {
        mctrl.write32(0x80000000 + i, i*i); // Just add some value
    }

    for(size_t i = 0; i < 256; i += 4) {
        ASSERT_EQ(mctrl.read32(0x80000000 + i), i*i);
    }

    // Unaligned read/write should throw
    EXPECT_THROW(mctrl.read32(0x80000001), std::runtime_error);
    EXPECT_THROW(mctrl.write32(0x80000007, 0xcafebabe), std::runtime_error);
   
    // read/write 8 and 16 should throw for the IO type APBCtrl
    EXPECT_THROW(mctrl.read8(0x80000000), std::logic_error);
    EXPECT_THROW(mctrl.write8(0x80000000, 0xba), std::logic_error);
    EXPECT_THROW(mctrl.read16(0x80000000), std::logic_error);
    EXPECT_THROW(mctrl.write16(0x80000000, 0xbabe), std::logic_error);

}

