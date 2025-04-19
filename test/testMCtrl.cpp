#include "../src/sparcv8/CPU.h"
#include "../src/peripherals/MCTRL.h"
#include "../src/peripherals/gaisler/ambapp.h"

#define GR712RC_SYSTEMID 0x7120e70
#define GR740_REV0_SYSTEMID 0x07401038
#define GR740_REV1_SYSTEMID 0x07401039

#include "../src/peripherals/gaisler/ambapp_ids.h"
#include <gtest/gtest.h>

#include <cmath>


class MCtrlTest : public ::testing::Test {

protected:
    MCtrlTest();

    virtual ~MCtrlTest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();

    MCtrl mctrl;

};



MCtrlTest::MCtrlTest()
{  
   	


}

MCtrlTest::~MCtrlTest()
{

}

void MCtrlTest::SetUp()
{
   

 
}

void MCtrlTest::TearDown()
{
}

TEST_F(MCtrlTest, TestROMBehavior)
{
    mctrl.attach_bank<RomBank<1 * 1024 * 1024>>(0x80000000);
    mctrl.attach_bank<RomBank<1 * 1024 * 1024>>(0xfff00000);

    mctrl.write32(0xfffffff0, GR740_REV1_SYSTEMID); // Device id at
    
    // mst0 at 0xfffff000 - LEON3
    u32 value = (VENDOR_GAISLER << 24) | (GAISLER_LEON3 << 12) | (AMB_VERSION << 5);
    mctrl.write32(0xfffff000, value);

    // Make it a ROM
    mctrl.find_bank(0xfff00000)->lock();
    
    // Writing should throw:
    EXPECT_THROW(mctrl.write32(0xfffff010, value), std::runtime_error);

    EXPECT_EQ(mctrl.read32(0xfffffff0), GR740_REV1_SYSTEMID);
    EXPECT_EQ(mctrl.read32(0xfffff000), value);
    

       
}

TEST_F(MCtrlTest, TestRAMBehavior) {
    mctrl.attach_bank<RamBank>(0x40000000, 32 * 1024 * 1024);
    mctrl.write32(0x40000000, 0xcafebabe);

    EXPECT_EQ(mctrl.read8(0x40000000), 0xca);
    EXPECT_EQ(mctrl.read8(0x40000001), 0xfe);
    EXPECT_EQ(mctrl.read8(0x40000002), 0xba);
    EXPECT_EQ(mctrl.read8(0x40000003), 0xbe);
 
    EXPECT_EQ(mctrl.read16(0x40000000), 0xcafe);
    EXPECT_EQ(mctrl.read16(0x40000002), 0xbabe);
    
    EXPECT_EQ(mctrl.read32(0x40000000), 0xcafebabe);

    // attaching with overlap should throw
    EXPECT_THROW(mctrl.attach_bank<RamBank>(0x40010000, 1 * 1024 * 1024), std::runtime_error);

    // Test little endian for the sake of it:
    mctrl.attach_bank<RamBank>(0x60000000, 1 * 1024 * 1024, Endian::Little);
    mctrl.write32(0x60000000, 0xcafebabe);

    EXPECT_EQ(mctrl.read8(0x60000000), 0xbe);
    EXPECT_EQ(mctrl.read8(0x60000001), 0xba);
    EXPECT_EQ(mctrl.read8(0x60000002), 0xfe);
    EXPECT_EQ(mctrl.read8(0x60000003), 0xca);
 
    EXPECT_EQ(mctrl.read16(0x60000000), 0xbabe);
    EXPECT_EQ(mctrl.read16(0x60000002), 0xcafe);
    
    EXPECT_EQ(mctrl.read32(0x60000000), 0xcafebabe);
    
    // Writing outside banks should throw
    EXPECT_THROW(mctrl.write32(0x80000000, 0xcafebabe),std::out_of_range);
      

}