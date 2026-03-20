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

// grvga_check_var in the Linux kernel driver accepts a mode change only if
// var->pixclock matches one of the four dclk registers exactly.
// The kernel boot path uses pixclock=40000 (dclk0, 25 MHz).
// SDL1 fbcon choose_vesa_mode substitutes VESA standard 640x480@60Hz
// pixclock=39683, which must match one of dclk1-3.
// If no match is found, grvga_check_var returns -EINVAL and SDL fails with
// "Couldn't set console screen info".
TEST_F(SVGATest, DclkCoversKernelAndVesaPixclock)
{
    SVGA svga(mctrl, /*enable=*/false);

    // Kernel boot pixclock (from grvga_modedb 640x480@60Hz)
    const u32 kernel_pixclock = 40000;
    // SDL1 VESA table pixclock for 640x480@60Hz (choose_vesa_mode)
    const u32 vesa_pixclock   = 39683;

    u32 dclk[4];
    dclk[0] = svga.read(0x18);
    dclk[1] = svga.read(0x1c);
    dclk[2] = svga.read(0x20);
    dclk[3] = svga.read(0x24);

    auto matches_any = [&](u32 pixclock) {
        for (auto v : dclk) if (v == pixclock) return true;
        return false;
    };

    EXPECT_TRUE(matches_any(kernel_pixclock))
        << "Kernel boot pixclock " << kernel_pixclock << " not in dclk registers";
    EXPECT_TRUE(matches_any(vesa_pixclock))
        << "VESA SDL pixclock " << vesa_pixclock << " not in dclk registers — "
           "FBIOPUT_VSCREENINFO will fail with -EINVAL";
}

TEST_F(SVGATest, ClutWriteStoresCorrectARGB)
{
    SVGA svga(mctrl, /*enable=*/false);

    // Kernel grvga_setcolreg writes: (regno<<24) | (red<<16) | (green<<8) | blue
    // We expect the emulator to store it as ARGB8888: 0xFF000000 | (r<<16) | (g<<8) | b
    // and expose it via get_palette_entry().
    svga.write(0x28, (5u << 24) | (0xAA << 16) | (0xBB << 8) | 0xCC);
    EXPECT_EQ(svga.get_palette_entry(5), 0xFF'AA'BB'CCu);

    svga.write(0x28, (0u << 24) | (0xFF << 16) | (0x00 << 8) | 0x00);
    EXPECT_EQ(svga.get_palette_entry(0), 0xFF'FF'00'00u); // red

    svga.write(0x28, (255u << 24) | (0x00 << 16) | (0xFF << 8) | 0x00);
    EXPECT_EQ(svga.get_palette_entry(255), 0xFF'00'FF'00u); // green
}

TEST_F(SVGATest, StatFuncSetsBpp)
{
    SVGA svga(mctrl, /*enable=*/false);

    // func=3 (bits[5:4]=11) → 32bpp
    svga.write(0x0, (3u << 4));
    EXPECT_EQ(svga.get_bpp(), 32);

    // func=1 (bits[5:4]=01) → 8bpp
    svga.write(0x0, (1u << 4));
    EXPECT_EQ(svga.get_bpp(), 8);

    // func=2 (bits[5:4]=10) → 16bpp treated as 32bpp (passthrough)
    svga.write(0x0, (2u << 4));
    EXPECT_EQ(svga.get_bpp(), 32);
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
