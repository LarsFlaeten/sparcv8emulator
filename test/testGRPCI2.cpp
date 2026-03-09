// SPDX-License-Identifier: MIT
#include "../src/sparcv8/CPU.h"
#include "../src/sparcv8/MMU.h"
#include "../src/peripherals/GRPCI2.hpp"
#include "../src/peripherals/ac97.hpp"
#include "../src/peripherals/APBCTRL.h"


#include <gtest/gtest.h>

class GRPCI2Testable : public GRPCI2 {
    public:
        GRPCI2Testable(IRQMP& irq) : GRPCI2(irq) {}
        using GRPCI2::sts_cap_;
        using GRPCI2::ctrl_;
        using GRPCI2::dma_ctrl_;
        using GRPCI2::io_map_;
        using GRPCI2::ahbm2pci_;

};


class GRPCI2Test : public ::testing::Test {

protected:
    GRPCI2Test();

    virtual ~GRPCI2Test();

    // Code here will be called immediately after the constructor (right
    // before each test).
    virtual void SetUp();

    // Code here will be called immediately after each test (right
    // before the destructor).
    virtual void TearDown();

    IRQMP irqmp;
    MCtrl mctrl;
    MMU mmu;
    
    GRPCI2Testable testable_;
    GRPCI2& grpci2;
};



GRPCI2Test::GRPCI2Test() : irqmp(1), mmu(mctrl), testable_(irqmp), grpci2(testable_) {

}

GRPCI2Test::~GRPCI2Test() {

}

void GRPCI2Test::SetUp() {

}

void GRPCI2Test::TearDown() {

}

TEST_F(GRPCI2Test, CheckControlRegOnStartup)
{
    auto ctrl = testable_.read(0x0);

    u32 cmp = 0;
    ASSERT_EQ(ctrl, cmp);
}

TEST_F(GRPCI2Test, CheckControlRegWrite)
{
    auto ctrl = testable_.read(0x0);
    ASSERT_EQ(ctrl, 0);

    // Writes to bit 28 and bits 15-12 should be ignored (reserved bits)
    u32 value = 0x1 << 28 | 0xf << 12;
    testable_.write(0x0, value);

    ctrl = testable_.read(0x0);
    ASSERT_EQ(ctrl, 0);

    // Writes to all other bits should be retained, except bits 29 and 30,
    // which are selv-clearing
    testable_.write(0x0, (u32)~0);
    ctrl = testable_.read(0x0);
    ASSERT_EQ(ctrl, ~value & ~(3u << 29));  // all bits set except the reserved ones
                                            // and the master/target reset




}

TEST_F(GRPCI2Test, CheckSTSCAPRegOnStartup)
{
    auto sts_cap = testable_.read(0x4);

    // PCI should have master bit enabled, and IRQ mode 1 set
    u32 cmp = 0x1 << 30 | 0x1 << 24 | 0x1 << 28;
    ASSERT_EQ(sts_cap, cmp);
}

TEST_F(GRPCI2Test, WriteSTSCAPReg)
{
    auto sts_cap = testable_.read(0x4);

    // PCI should have master bit enabled, and IRQ mode 1 set
    u32 cmp = 0x1 << 30 | 0x1 << 24 | 0x1 << 28;
    ASSERT_EQ(sts_cap, cmp);

    // bits 12-20 are write-clear, all other are read only

    // Set all to one, check that only 12-20 are cleared:
    testable_.sts_cap_ = (u32)~0;
    testable_.write(0x4, (u32)~0); // Write all ones
    sts_cap = testable_.read(0x4);
    ASSERT_EQ(sts_cap, (u32)~(0x1ff << 12)); // Expect all ones except bits 12-20

    u32 mask_alt = 0xaaaaaaaa; // Alternating 1 and 0
    u32 mask_wc = 0x1ff << 12;
    u32 mask_ones = (u32)~0;
    testable_.sts_cap_ = mask_alt; // Alternating 1 and 0
    testable_.write(0x4, mask_ones); // Write all ones
    sts_cap = testable_.read(0x4);
    ASSERT_EQ(sts_cap, 0xaaaaaaaa & (u32)~(0x1ff << 12)); // Expect alternating ones except bits 12-20

    // Set all to one, check that only 12-20 are cleared:
    testable_.sts_cap_ = (u32)~0;
    testable_.write(0x4, mask_alt); // Write alternating bits, will clear half of 12-20
    sts_cap = testable_.read(0x4);
    
    ASSERT_EQ((sts_cap >> 12) & 0x1ff, 0x155); // Expect all ones except bits 12-20, where half of the bits hsould be cleared

    ASSERT_EQ(sts_cap, ~mask_wc | 0x155 << 12);
    
    

}

#define REGSTORE(x, y) testable_.write(x,y)
#define REGLOAD(x) testable_.read(x)
    
TEST_F(GRPCI2Test, STSCAPRegLinuxBoot)
{
    struct regs_ {
        u32 sts_cap;
    };
    regs_ r;
    r.sts_cap = 0x04;
    regs_* regs = &r;

    // Emulate the linux boot
    testable_.sts_cap_ = 0xCAFEBABE; // Reg is something

    // clear status, use the actual code line
    // arch/sparc/kernel/leon_pci_grpci2.c, line 580
    REGSTORE(regs->sts_cap, ~0); /* Clear Status */
	
    auto ret = testable_.read(0x4);
    ASSERT_EQ(ret & (0x1FFu << 12), 0); // any nonzero WC bits should be cleared

}
 

#define STS_CFGERRVALID_BIT 20
#define STS_CFGERR_BIT	19
#define STS_CFGERRVALID	(1<<STS_CFGERRVALID_BIT)
#define STS_CFGERR	(1<<STS_CFGERR_BIT)


TEST_F(GRPCI2Test, STSCAP_signal_cfg_access_done)
{
    auto sts_cap = testable_.read(0x4);

    // PCI should have master bit enabled, and IRQ mode 1 set
    u32 cmp = 0x1 << 30 | 0x1 << 24 | 0x1 << 28;
    ASSERT_EQ(sts_cap, cmp);

    testable_.signal_pci_cfg_access_complete();
    sts_cap = testable_.read(0x4);
    ASSERT_EQ((sts_cap >> 19) & 0x1u, 0); //No error
    ASSERT_EQ((sts_cap >> 20) & 0x1u, 1); // cfg done


    struct regs_ {
        u32 sts_cap;
    };
    regs_ r;
    r.sts_cap = 0x04;
    struct priv_ {
        regs_* regs;
    };
    priv_ p;
    priv_* priv = &p;
    priv->regs = &r;

    // Linux:
    // arch/sparc/kernel/leon_pci_grpci2.c, line 268
    while ((REGLOAD(priv->regs->sts_cap) & STS_CFGERRVALID) == 0)
		; // This should not hang...

    // Clear old status:
    /* clear old status */
	REGSTORE(priv->regs->sts_cap, (STS_CFGERR | STS_CFGERRVALID));
    sts_cap = testable_.read(0x4);
    ASSERT_EQ((sts_cap >> 19) & 0x1u, 0); //No error
    ASSERT_EQ((sts_cap >> 20) & 0x1u, 0); // No cfg done


    
    // Do it again with original reg all ones:
    testable_.sts_cap_ = ~0u;

    testable_.signal_pci_cfg_access_complete();
    sts_cap = testable_.read(0x4);
    ASSERT_EQ((sts_cap >> 19) & 0x1u, 0); //No error
    ASSERT_EQ((sts_cap >> 20) & 0x1u, 1); // cfg done

        // arch/sparc/kernel/leon_pci_grpci2.c, line 268
    while ((REGLOAD(priv->regs->sts_cap) & STS_CFGERRVALID) == 0)
		; // This should not hang...


}
TEST_F(GRPCI2Test, CheckDMACTRLRegOnStartup)
{
    auto dma_ctrl = testable_.read(0x10);

    u32 cmp = 0x1u << 31;
    ASSERT_EQ(dma_ctrl, cmp);
}

TEST_F(GRPCI2Test, CheckDMACTRLRegWriteReadOnly)
{
    auto dma_ctrl = testable_.read(0x10);

    u32 cmp = 0x1u << 31;
    ASSERT_EQ(dma_ctrl, cmp);

    // Check RO area:
    testable_.write(0x10, 0x1u << 3); // Active, bit 3 is ro
    testable_.write(0x10, 0x7ff << 20); // reserved (20-30) are ro
    testable_.write(0x10, 0x1u << 31); // keep safe bit
    
    dma_ctrl = testable_.read(0x10);

    // Writes to these areas shold not change the reg:
    ASSERT_EQ(dma_ctrl, cmp);

    // Set all ones
    testable_.dma_ctrl_ = ~0u;
    testable_.write(0x10, 0x1u << 31);
    
    dma_ctrl = testable_.read(0x10);
    cmp = (0x1u << 3) | (0x7ffu << 20) | (0x1fffu << 7) | (0x1u << 31); 
    // Writes to these areas shold not change the reg:
    ASSERT_EQ(dma_ctrl, cmp);



}


TEST_F(GRPCI2Test, CheckDMACTRLRegWriteClear)
{
    auto dma_ctrl = testable_.read(0x10);

    u32 cmp = 0x1u << 31;
    ASSERT_EQ(dma_ctrl, cmp);

    // bits 7-19 are write-clear, all other are read only

    // Set all to one, check that only 7-19 are cleared:
    testable_.dma_ctrl_ = (u32)~0;
    testable_.write(0x10, (u32)~0); // Write all ones
    dma_ctrl = testable_.read(0x10);
    ASSERT_EQ(dma_ctrl, (u32)~(0x1fffu << 7)); // Expect all ones except bits 7-19

    // Set all to zer, except safety guard:
    testable_.dma_ctrl_ = 0x1u << 31;

    // Check that we can modify the safe guarded bits
    testable_.write(0x10, 0x1u << 31 | 0x3u << 4 | 0x1 << 1);
    dma_ctrl = testable_.read(0x10);
    ASSERT_EQ(dma_ctrl, 0x3u << 4 | 0x1 << 1 | 0x1u << 31);
    // Safe guard:
    testable_.write(0x10, 0u);
    testable_.write(0x10, 0x3u << 4 | 0x1 << 1); // Should not be written
    dma_ctrl = testable_.read(0x10);
    ASSERT_EQ(dma_ctrl, 0);
    
    // Clear-write on a reg that is zero shuold produce zero
    testable_.write(0x10, 0x1u << 7 | 0x1u << 19);
    dma_ctrl = testable_.read(0x10);
    ASSERT_EQ(dma_ctrl, 0);
    // Set wc registers to 1:
    testable_.dma_ctrl_ = 0x1u << 7 | 0x1u << 19 | 0x1u << 12;
    dma_ctrl = testable_.read(0x10);
    ASSERT_EQ(dma_ctrl, 0x1u << 7 | 0x1u << 19 | 0x1u << 12);
    testable_.write(0x10, 0x1u << 7 | 0x1u << 19); // Write-clear 7 and 19
    dma_ctrl = testable_.read(0x10);
    ASSERT_EQ(dma_ctrl, 0x1u << 12); // 12 shuould remain

    // Check RO regs
    u32 MASK_R = (0x7FFu << 20) | (1u << 3);    
    u32 MASK_WC = (0x1FFFu << 7); 
    testable_.dma_ctrl_ = 0;
    testable_.write(0x10, ~0u); 
    dma_ctrl = testable_.read(0x10);
    ASSERT_EQ(dma_ctrl, ~(MASK_R | MASK_WC)); // r-regs and wc shuold still be zero

    testable_.dma_ctrl_ = ~0u;
    testable_.write(0x10, 0); 
    dma_ctrl = testable_.read(0x10);
    ASSERT_EQ(dma_ctrl, MASK_R | MASK_WC); // r-regs and wc shuold still be one    
}

TEST_F(GRPCI2Test, CheckDMABASEreadwrite)
{
    auto dma_base = testable_.read(0x14);
    ASSERT_EQ(dma_base, 0);

    testable_.write(0x14, 0xcafebabe);
    dma_base = testable_.read(0x14);
    ASSERT_EQ(dma_base, 0xcafebabe);

}

TEST_F(GRPCI2Test, iomap_readwrite)
{
    auto iomap = testable_.read(0x0C);
    ASSERT_EQ(iomap, 0);

    // Check that only bits 31-16 are writeable
    testable_.write(0x0C, 0xCAFEBABE);
    iomap = testable_.read(0x0C);
    ASSERT_EQ(iomap, 0xCAFE0000);

    testable_.write(0x0C, ~0x0);
    iomap = testable_.read(0x0C);
    ASSERT_EQ(iomap, 0xFFFF0000);

}

TEST_F(GRPCI2Test, ahbm2pci_readwrite)
{
    // Check zero init
    for(int i = 0; i < 16; ++i) {
        auto p = testable_.read(0x40 + 4*i);
        ASSERT_EQ(p, 0);
    }

    // populate with values:
    for(int i = 0; i < 16; ++i) {
        testable_.ahbm2pci_[i] = i+1;
    }

    u32 x = 1;
    for(u32 offset = 0x40; offset <= 0x7c; offset += 4) {
        auto p = testable_.read(offset);
        ASSERT_EQ(p, x);
        x++;
    }

    // test write
    for(u32 offset = 0x40; offset <= 0x7c; offset += 4) {
        testable_.write(offset, offset+7);
        auto p = testable_.read(offset);
        ASSERT_EQ(p, offset+7);
    }
}

TEST_F(GRPCI2Test, PciIrqRoutedToIRQ6)
{
    // The Linux GRPCI2 driver computes PCI INTA IRQ as:
    //   amba_pnp_irq + 4 = 2 + 4 = 6
    // Verified via /proc/interrupts: "6: grpci2 -pcilvl snd_intel8x0"
    // raise_pci_irq must trigger IRQMP IRQ 6, not IRQ 2.
    testable_.raise_pci_irq(0);  // slot 0, INTA

    u32 ipend = irqmp.read(IRQMP_IPEND_OS);
    ASSERT_NE(ipend & (1u << 6), 0u) << "IRQ 6 must be pending in IRQMP after PCI INTA";
    ASSERT_EQ(ipend & (1u << 2), 0u) << "IRQ 2 must NOT be triggered (that is GRPCI2_JUMP, not device IRQ)";
}

TEST_F(GRPCI2Test, ac97_init)
{
    auto ac97pci = std::make_unique<AC97Pci>(0, mctrl, false); // false = no host audio
    

    // Set up what we need of memory and peripherals to test all this:
    mctrl.attach_bank<APBCTRL>(0x80000000, mctrl, irqmp);
    auto& apbctrl= reinterpret_cast<APBCTRL&>(*mctrl.find_bank(0x80000000));
    GRPCI2& grpci2 = apbctrl.get_grpci2();
    grpci2.attach_device(std::move(ac97pci));
    mctrl.attach_bank<PCIIOCfgArea>(0xfffa0000, grpci2); // PCI CFG
    

    // Read from NABM 0x30:
    auto glob_sta = mctrl.read32(0xfffa1130u);
    ASSERT_EQ(glob_sta, 0x00000100);



}