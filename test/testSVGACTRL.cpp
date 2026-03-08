// SPDX-License-Identifier: MIT
#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"
#include "../src/peripherals/SVGA.h"
#include "../src/peripherals/gaisler/ambapp.h"




#include <gtest/gtest.h>

#include <cmath>



class SVGATest : public ::testing::Test {

protected:
    SVGATest();

    virtual ~SVGATest();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();

    IRQMP intc;
    MCtrl mctrl;
    CPU cpu;

};



SVGATest::SVGATest() : intc(1), cpu(mctrl, intc, 0)
{  
   	


}

SVGATest::~SVGATest()
{

}

void SVGATest::SetUp()
{
    // Video RAM bank
    mctrl.attach_bank<RamBank>(0x20000000, 8 * 1024 * 1024); // 8MB video memory
}

void SVGATest::TearDown()
{
}

TEST_F(SVGATest, Reset)
{
    SVGA svga(mctrl);
    
    ASSERT_EQ(svga.vendor_id(), VENDOR_GAISLER );
    ASSERT_EQ(svga.device_id(), GAISLER_SVGACTRL );

    ASSERT_EQ(svga.read(0x4), 0); // VLEN should be zero after reset
    // Write something to vlen
    svga.write(0x4, 0b111);

    // enable core:
    svga.write(0x0, 0x1); // enable
    ASSERT_EQ(svga.read(0x0), 0x1);
    
    // disable core:
    svga.write(0x0, 0x0); // disable
    ASSERT_EQ(svga.read(0x0), 0x0);
    
    // Check vlen is still there
    ASSERT_EQ(svga.read(0x4), 0b111);


    svga.write(0x0, 0x3); // enable and reset
    ASSERT_EQ(svga.read(0x4), 0x0);
    ASSERT_EQ(svga.read(0x0), 0x0); // Should not be enabled, since it was a reset

    // Set HPOL, VPOL and BDSEL to high
    svga.write(0x0, (0x3 << 8 | 0x3 << 4));
    ASSERT_EQ(svga.read(0x0), (0x3 << 8 | 0x3 << 4));

    // reset
    svga.write(0x0, svga.read(0x0) | 0x2);
    ASSERT_EQ(svga.read(0x0), (0x3 << 8 | 0x3 << 4));
    
}
