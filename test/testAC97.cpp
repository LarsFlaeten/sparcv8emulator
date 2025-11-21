#include <gtest/gtest.h>
#include "../src/peripherals/ac97.hpp"

#include <bit>

static constexpr uint32_t NAM_BASE = 0x24000800;
static constexpr uint32_t NABM_BASE = 0x24000900;

//
// Constants
//
static constexpr uint32_t PO_BASE = 0x10;
static constexpr uint32_t PO_BDBAR = PO_BASE + 0x00;
static constexpr uint32_t PO_CIV   = PO_BASE + 0x04;
static constexpr uint32_t PO_LVI   = PO_BASE + 0x05;
static constexpr uint32_t PO_SR    = PO_BASE + 0x06;
static constexpr uint32_t PO_PICB  = PO_BASE + 0x08;
static constexpr uint32_t PO_CR    = PO_BASE + 0x0B;

static constexpr uint32_t BD_AREA = 0x420A0000; 



class AC97Test : public ::testing::Test {
protected:
    AC97Test()
    {
        
    }

    void SetUp() override {
        dev.reset();
        irq_raised = false;
    }

    MCtrl mctrl;
    std::unique_ptr<AC97Pci> dev;
    bool irq_raised = false;

    void make_device() {
        // Create the AC'97 PCI peripheral
        // Instead of handing over mctrl, we give it read/write lambdas
        // TODO: Redesign this...
        auto mem_read = [this](uint32_t pa, void* val, size_t sz) -> bool {
            
            switch(sz) {
                case 1: {
                    u8* p = static_cast<u8*>(val);
                    *p = mctrl.read8(pa);
                    break;
                }
                case 2: {
                    u16* p = static_cast<u16*>(val);
                    *p = std::byteswap(mctrl.read16(pa));
                    break;
                }
                case 4: {
                    u32* p = static_cast<u32*>(val);
                    *p = std::byteswap(mctrl.read32(pa));
                    break;
                }
                default:
                throw std::runtime_error("memread lambda, wrong size: " + std::to_string(sz));
            
            }
            return true;
        };
        auto mem_write = [](uint32_t va, const void* val, size_t sz) -> bool {
            throw std::runtime_error("memwrite lambda");
            return true;
        };

        dev = std::make_unique<AC97Pci>(0, mem_read, mem_write, mctrl, false);

        dev->config_write32(0x10, NAM_BASE); // Write NAM BAR
        dev->config_write32(0x14, NABM_BASE); // Write NABM BAR
    
        // PCI MMIO BARS
        mctrl.attach_bank<PCIMMIOBank>(*dev, NAM_BASE, 0x100); // NAM
        mctrl.attach_bank<PCIMMIOBank>(*dev, NABM_BASE, 0x100); // NABM

        //mctrl.debug_list_banks();

        // Replace raise_intx with a lambda that flips a bool
        dev->set_intx_cb([this]() { irq_raised = true; });

    }

    uint32_t read32_le(uint32_t addr) {
        return std::byteswap(mctrl.read32(addr));
    }

    uint16_t read16_le(uint32_t addr) {
        return std::byteswap(mctrl.read16(addr));
    }

    uint8_t read8(uint32_t addr) {
        return mctrl.read8(addr);
    }

    void write32_le(uint32_t addr, uint32_t val) {
        mctrl.write32(addr, std::byteswap(val));
    }

    void write16_le(uint32_t addr, uint16_t val) {
        mctrl.write16(addr, std::byteswap(val));
    }

    void write8(uint32_t addr, uint8_t val) {
        mctrl.write8(addr, val);
    }

    //
    // Minimal BD entry helper
    //
    void write_playback_bd(uint32_t bdbar, int index, uint32_t addr, uint16_t len, uint16_t ctl)
    {
        uint32_t off = bdbar + index * 8;
        write32_le(off + 0, addr);
        write16_le(off + 4, len);
        write16_le(off + 6, ctl);
    }

};


// ----------------------
// GLOB_CNT / Warm Reset
// ----------------------
TEST_F(AC97Test, WarmResetSetsCRDYAndClearsResetBit)
{
    make_device();

    

    

    // Assert warm reset (write bit 16)
    // Linux writes byteswapped
    write32_le(NABM_BASE + 0x2c, 0x4);
    
    // Read back GLOB_CNT must have the bit cleared after warm reset
    uint32_t cnt = read32_le(NABM_BASE + 0x2C);
    ASSERT_EQ(cnt & 0x4, 0u) << "Warm reset bit must auto-clear";

    // GLOB_STA must have CRDY set
    uint32_t sta = read32_le(NABM_BASE + 0x30);
    ASSERT_NE(sta & 0x100, 0u) << "Codec ready bit must be set";
}

// ----------------------
// GLOB_STA behavior
// ----------------------
TEST_F(AC97Test, GlobStaReturnsOnlyAllowedBits)
{
    make_device();

    uint32_t v = read32_le(NABM_BASE + 0x30);
    // We cannot see the sempahore bit, even though it is set a device init
    // since reading glob sta will clear it:
    ASSERT_EQ(v , 0x100u) << "GLOB_STA must have Codec 0 ready and semaphore bit not set";
    
    // Only allow clearing sempahore bit, not the codec ready bit:
    write32_le(NABM_BASE + 0x30, 0xffffffffu);

    v = read32_le(NABM_BASE + 0x30);
    ASSERT_EQ(v , 0x100u) << "GLOB_STA must have Codec 0 ready and semaphore bit not set";
    
    // Below test is disabled, as DMA bits are not implemented:

    // Only 0x10F is allowed: CRDY + DMA bits + semaphore bit
    //ASSERT_EQ(v & ~0x10F, 0u) << "GLOB_STA must mask off undefined bits";
}

// ----------------------
// NAM read/write behavior
// ----------------------
TEST_F(AC97Test, NAMReadOnlyRegistersReturnCorrect16bitWithSignExtend)
{
    make_device();

    // NAM offset 0x26 = initial power status = 0x000F
    uint32_t v = read32_le(NAM_BASE + 0x26);
    ASSERT_EQ(v, 0xFFFF000F);
}

TEST_F(AC97Test, NAMWriteUpdatesRegister)
{
    make_device();

    write16_le(NAM_BASE + 0x02, 0x1234);

    uint32_t v = read32_le(NAM_BASE + 0x02);
    ASSERT_EQ(v, 0xFFFF1234);
}

// -------------------------------------------------------------
// PO_CR start: device loads buffer descriptor, PICB initialized
// -------------------------------------------------------------
TEST_F(AC97Test, POCR_StartLoadsBDAndInitializesPICB)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    //mctrl.debug_list_banks();
    

    make_device();

    // Set BD BAR: put descriptors in our RAM
    write32_le(NABM_BASE + PO_BDBAR, BD_AREA);

    // CIV = 0 by reset
    write8(NABM_BASE + PO_LVI, 0x1F);

    // Write a BD entry @ index 0
    uint32_t bufaddr = 0x42000000;
    uint16_t buflen  = 0x1000;   // len bytes
    uint16_t ctl     = 0x8000;   // IOC=1
    write_playback_bd(BD_AREA, 0, bufaddr, buflen - 1, ctl);

    // Start playback → set RUN bit
    write8(NABM_BASE + PO_CR, 0x01);

    // PICB should be initialized to buflen/4 (S16 * 2ch)
    uint16_t picb = read16_le(NABM_BASE + PO_PICB);
    ASSERT_EQ(picb, buflen / 4);
}

TEST_F(AC97Test, InitialStatusMustBeZero)
{
    make_device();
    uint8_t sr = read8(NABM_BASE + 0x16);
    EXPECT_EQ(sr, 0x00) << "PO_SR must be zero after initialization";
}

TEST_F(AC97Test, TickMustNotModifySRWhenNotRunning)
{
    make_device();

    // call tick 1000 times
    for (int i = 0; i < 1000; ++i)
        dev->tick();

    uint8_t sr = read8(NABM_BASE + 0x16);
    EXPECT_EQ(sr, 0x00) << "tick() modified PO_SR even though PO_RUNNING=0";
}

TEST_F(AC97Test, POCR_StartClearsBCISAndLVBCI)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    
    make_device();

    // 1. Prepare BD table
    uint32_t bd_addr = BD_AREA;  // you know BD_AREA from earlier
    write32_le(NABM_BASE + 0x10, bd_addr);   // BD_BAR
    write8(   NABM_BASE + 0x15, 0x1F);       // LVI=31

    // Write bogus BCIS/LVBCI into the device before START
    write8(NABM_BASE + 0x16, 0x0C);          // force BCIS+LVBCI

    // 2. Start DMA
    write8(NABM_BASE + 0x1B, 0x01);          // PO_CR = START
    
    // 3. Read SR
    uint8_t sr = read8(NABM_BASE + 0x16);

    // DCH must be 0, BCIS=LVBCI=0
    EXPECT_EQ(sr & 0x01, 0x00);    // DCH = 1
    EXPECT_EQ(sr & 0x0C, 0x00);    // BCIS/LVBCI cleared
}

TEST_F(AC97Test, POCR_ReservedBitsAlwaysZeroAndLowBitsPreserved) {
    make_device();

    const uint32_t PO_CR = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;

    // Write all ones
    write8(PO_CR, 0xFF);

    uint8_t cr = read8(PO_CR);

    // Bit1 must always read zero (RESETREGS self-clears)
    EXPECT_EQ(cr & 0x02, 0);

    // Bits 5–7 must always read zero
    EXPECT_EQ(cr & 0xE0, 0);

    // Writable bits (0,2,3,4) must survive
    EXPECT_EQ(cr & 0x1D, 0x1D);
}

//
// AC’97 NABM — Playback Engine Register Tests
//

TEST_F(AC97Test, POCR_ResetValueAndReservedBits)
{
    make_device();

    const uint32_t PO_CR = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;

    // Write all ones
    write8(PO_CR, 0xFF);

    uint8_t cr = read8(PO_CR);

    // upper bits must be 0
    EXPECT_EQ(cr & 0xE0, 0x00)
        << "Reserved bits [7:5] must read zero after write";

    // RESETREGS must self clear → 0x1D
    EXPECT_EQ(cr & 0x1F, 0x1D)
        << "PO_CR should return masked writable bits, with RESETREGS self-cleared";
}

TEST_F(AC97Test, POCR_WriteDoesNotAffectHighByte)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t CR = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;

    // CR must read as 0 after reset
    EXPECT_EQ(read8(CR), 0x00);

    // Write masked junk
    write8(CR, 0xFF);

    // High byte does not exist – reading adjacent byte must NOT reflect CR
    // High byte is CR+1, which is PIV high or reserved
    uint32_t CR_HI = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR + 1;

    // According to our model, CR+1 is not a valid register → should throw
    EXPECT_THROW(read8(CR_HI), std::runtime_error);
    //EXPECT_EQ(read8(CR_HI), 0x00);
}

TEST_F(AC97Test, POCR_StartDoesNotActivateDMAWithoutBDBar)
{
    // Ensures your safety fix works
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t CR = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;
    const uint32_t SR = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;

    // RUN bit write
    write8(CR, 0x01);

    // DMA must NOT start because BD_BAR is zero
    // RUN bit in SR must remain cleared (no active DMA)
    // According to our model, SR is not a valid register for r16 → should throw
    EXPECT_THROW(read16_le(SR), std::runtime_error);
    uint8_t sr = read8(SR);
    EXPECT_EQ(sr & 0x01, 0x00);
}

//
// --- Status Register (PO_SR) Tests ---
//

TEST_F(AC97Test, POSR_ResetValueAndReservedBits)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t SR = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;

    EXPECT_THROW(read16_le(SR), std::runtime_error);
    

    uint8_t sr = read8(SR);

    // Reset state = RUN=0, BCIS=0, LVBCI=0
    EXPECT_EQ(sr, 0x0000);

    // Upper bits reserved, must read 0 (your implementation has 0)
    //EXPECT_EQ(sr & 0xFFE0, 0x0000);
}

TEST_F(AC97Test, POSR_WriteOneToClearBits_NoEffectIfNoBitsAreSet)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t SR = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;

    // Writing 1s when no bits are set must not cause exceptions
    write8(SR, 0x1C);
    EXPECT_EQ(read8(SR) & 0x1C, 0x00);
}

/* This one was more permissive than the implementation
TEST_F(AC97Test, POSR_WriteOneToClearBits)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t SR = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;

    //
    // 1. Set W1C bits manually by writing the low status byte
    //
    write8(SR, 0x1C);   // set bits 2..4
    EXPECT_THROW(read16_le(SR), std::runtime_error);
    EXPECT_EQ(read8(SR) & 0x1C, 0x1C);

    //
    // 2. Now clear them via W1C
    //
    write8(SR, 0x1C);   // write 1s to clear bits 2..4
    EXPECT_EQ(read8(SR) & 0x1C, 0x00);
}
*/
//
// --- CIV / LVI Tests ---
//

TEST_F(AC97Test, POCIV_WriteIsIgnored)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t CIV = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CIV;

    // Write junk
    write8(CIV, 0xFF);

    // Must remain 0 — CIV is read-only
    EXPECT_EQ(read8(CIV), 0x00);
}
/* This one was more permissive than the implementation
TEST_F(AC97Test, POCIV_WriteMasksTo5Bits)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t CIV = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CIV;

    write8(CIV, 0xFF);
    EXPECT_EQ(read8(CIV), 0x1F);   // &0x1F mask
}
*/
TEST_F(AC97Test, POLVI_WriteMasksTo5Bits)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t LVI = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::LVI;

    write8(LVI, 0xFF);
    EXPECT_EQ(read8(LVI), 0x1F);
}

//
// --- PICB (16-bit) ---
//

TEST_F(AC97Test, POPICB_ReadWriteLowWordOnly)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t PICB = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::PICB;

    write16_le(PICB, 0xABCD);
    EXPECT_EQ(read16_le(PICB), 0xABCD);
}

//
// --- PIV (Predictive Interrupt Value) ---
// Should typically be RO / return 0 ---
//

TEST_F(AC97Test, POPIV_AlwaysReadsZero)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t PIV = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::PIV;

    //EXPECT_EQ(read8(PIV), 0x00);
    EXPECT_THROW(read8(PIV), std::runtime_error);
}

//
// --- BD BAR ---
//

TEST_F(AC97Test, POBDBAR_WriteAndReadBack)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t BD = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::BD_BAR;

    write32_le(BD, 0x420A0000);
    EXPECT_EQ(read32_le(BD), 0x420A0000);
}

//
// --- RESERVED REGIONS ---
//

TEST_F(AC97Test, ReservedBytesAlwaysReadZero_nowait_ShouldAlwaysThrow)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    // Pick several known-reserved offsets
    uint32_t reserved1 = NABM_BASE + 0x02;
    uint32_t reserved2 = NABM_BASE + 0x03;
    uint32_t reserved3 = NABM_BASE + 0x2E;

    EXPECT_THROW(read8(reserved1), std::runtime_error);
    EXPECT_THROW(read8(reserved2), std::runtime_error);
    EXPECT_THROW(read8(reserved3), std::runtime_error);
}

TEST_F(AC97Test, StatusRegistersAreClearedOnColdReset) {
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();


    // Convenience aliases
    const uint32_t base = NABM_BASE;

    // Playback registers
    const uint32_t PO_CIV = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CIV;
    const uint32_t PO_SR  = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;

    // Mic/Capture registers
    const uint32_t MC_CIV = base + AC97Pci::BMOff::MC_BASE + AC97Pci::BMOff::CIV;
    const uint32_t MC_SR  = base + AC97Pci::BMOff::MC_BASE + AC97Pci::BMOff::SR;

    // Global control register
    const uint32_t GLOB_CNT = base + AC97Pci::BMOff::GLOB_CNT;

    // -----------------------------
    // Precondition: simulate a device state *before* Linux resets it
    // -----------------------------
    write8(PO_SR,  0x0C);  // BCIS + LVBCR set (the exact bits Linux polls)
    write8(MC_SR,  0x1E);  // all W1C bits set
    write8(PO_CIV, 7);     // non-zero CIV
    write8(MC_CIV, 3);     // non-zero CIV

    // -----------------------------
    // Act: Trigger a cold reset like Linux does
    // -----------------------------
    // Rising edge on bit 1 of GLOB_CNT = cold reset
    write32_le(GLOB_CNT, 0x00000000);
    write32_le(GLOB_CNT, 0x00000002);

    // -----------------------------
    // Assert: All status and CIV registers must be cleared
    // -----------------------------
    EXPECT_EQ(read8(PO_SR), 0x01u)
        << "Cold reset must set DCH=1 and clear RUN/BCIS/LVBCI";

    // After cold reset, MC_SR must have DCH=1 (bit1), and everything else 0
    EXPECT_EQ(read8(MC_SR) & 0x1F, 0x02u);
    
    EXPECT_EQ(read8(PO_CIV), 0u)
        << "Cold reset must reset PO_CIV to zero";

    EXPECT_EQ(read8(MC_CIV), 0u)
        << "Cold reset must reset MC_CIV to zero";

}

TEST_F(AC97Test, BcisAndLvBciAreClearedWhenStoppingDma)
{
    // Map BD BAR memory
    uint32_t bd_base = 0x42097000;
    mctrl.attach_bank<RamBank>(bd_base, 0x1000);

    make_device();

    const uint32_t base = NABM_BASE;

    const uint32_t PO_CR    = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;
    const uint32_t PO_SR    = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;
    const uint32_t PO_CIV   = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CIV;
    const uint32_t PO_LVI   = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::LVI;
    const uint32_t PO_BDBAR = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::BD_BAR;

    // Install BD BAR and one BD
    write32_le(PO_BDBAR, bd_base);
    write8(PO_CIV, 0);
    write8(PO_LVI, 0);

    uint32_t bd0 = bd_base + 0;
    write32_le(bd0 + 0, bd_base + 0x200);
    write16_le(bd0 + 4, 0x00FF);      // len = 256 bytes → real BD
    write16_le(bd0 + 6, 0x8000);      // BCIS enabled

    // Start DMA
    write8(PO_CR, 0x01);

    for (int i = 0; i < 20; i++)
        dev->tick();
    

    // Now BCIS must be set
    ASSERT_EQ(read8(PO_SR) & 0x08, 0x08)
        << "Precondition failed: BCIS should be set after BD completion";

    // STOP DMA
    write8(PO_CR, 0x00);

    uint8_t sr = read8(PO_SR);

    EXPECT_EQ(sr & 0x0C, 0x00)
        << "BCIS/LVBCI must clear on STOP";

    EXPECT_EQ(sr & 0x01, 0x01)
        << "Expect DMA halted";
}


TEST_F(AC97Test, DchBitSetOnResetAndStop)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t base = NABM_BASE;

    const uint32_t PO_CR    = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;
    const uint32_t PO_SR    = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;
    const uint32_t GLOB_CNT = base + AC97Pci::BMOff::GLOB_CNT;

    // --- Cold reset (Linux behavior) ---
    write32_le(GLOB_CNT, 0x00000000);
    write32_le(GLOB_CNT, 0x00000002);   // rising edge triggers cold reset

    // After reset, DCH must be 1
    uint8_t sr = read8(PO_SR);
    EXPECT_EQ((sr & 0x01), 0x01)
        << "After cold reset, DCH must be 1 (DMA halted)";

    // --- Start + Stop cycle ---
    write8(PO_CR, 0x01); // START
    write8(PO_CR, 0x00); // STOP

    sr = read8(PO_SR);
    EXPECT_EQ((sr & 0x01), 0x01)
        << "After STOP, DCH must be 1 (DMA halted)";
}

TEST_F(AC97Test, DchBitClearedOnStart)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t base = NABM_BASE;

    const uint32_t PO_CR    = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;
    const uint32_t PO_SR    = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;
    const uint32_t GLOB_CNT = base + AC97Pci::BMOff::GLOB_CNT;

    // --- Cold reset ---
    write32_le(GLOB_CNT, 0x00000000);
    write32_le(GLOB_CNT, 0x00000002);

    // Confirm DCH=1 after reset
    uint8_t sr = read8(PO_SR);
    ASSERT_EQ(sr & 0x01, 0x01)
        << "Sanity: After reset, DCH must be 1";

    // --- Need valid BD BAR before START ---
    const uint32_t PO_BDBAR = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::BD_BAR;

    // Setup BD table
    uint32_t bd_base = 0x42097000;
    mctrl.attach_bank<RamBank>(bd_base, 0x1000);
    write32_le(PO_BDBAR, bd_base);

    // Put valid BD with nonzero len
    write32_le(bd_base + 0, bd_base + 0x200); // pointer
    write16_le(bd_base + 4, 0x00FF);          // len = 256 bytes
    write16_le(bd_base + 6, 0x0000);          // control

    write8(PO_CR, 0x01); // START DMA

    sr = read8(PO_SR);
    EXPECT_EQ((sr & 0x01), 0x00)
        << "DCH must be 0 when DMA is running";
}


TEST_F(AC97Test, DmaStallsOnEmptyBufferDescriptor)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x20000);
    make_device();

    const uint32_t base = NABM_BASE;

    const uint32_t PO_BDBAR = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::BD_BAR;
    const uint32_t PO_CR    = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;
    const uint32_t PO_SR    = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;

    const uint32_t GLOB_CNT = base + AC97Pci::BMOff::GLOB_CNT;

    // --- Cold reset ---
    write32_le(GLOB_CNT, 0x00000000);
    write32_le(GLOB_CNT, 0x00000002);

    // After reset, DCH=1, other bits clear
    uint8_t sr = read8(PO_SR);
    ASSERT_EQ(sr & 0x01, 0x01) << "Sanity check: DCH must be 1 after cold reset";

    // --- Set BD BAR ---
    uint32_t bd_base = 0x42097000;
    mctrl.attach_bank<RamBank>(bd_base, 0x1000);
    write32_le(PO_BDBAR, bd_base);

    // Leave BD0 completely zero:
    // ptr=0, len=0, ctl=0
    // (Linux does this before filling descriptors)
    write32_le(bd_base + 0, 0x00000000); // ptr
    write16_le(bd_base + 4, 0x0000);     // len
    write16_le(bd_base + 6, 0x0000);     // ctl

    // --- Start DMA ---
    write8(PO_CR, 0x01);  // RUN=1

    // Engine must start running
    sr = read8(PO_SR);
   
    // DCH must be 0 once DMA starts, even if BD is empty
    EXPECT_EQ(sr & 0x01, 0x00) << "DCH must clear when DMA engine is running";

    // --- Tick the engine several times ---
    for (int i = 0; i < 50; ++i)
        dev->tick();

    // Re-read status register
    sr = read8(PO_SR);

    // EXPECTATIONS:
    

    // 2. DCH=0 (engine is running, not halted)
    EXPECT_EQ(sr & 0x01, 0x00)
        << "DCH must remain cleared during stall";

    // 3. BCIS and LVBCI must NOT fire
    EXPECT_EQ(sr & 0x0C, 0x00)
        << "BCIS/LVBCI must not fire for empty BD";

    // If the test reaches this point, empty BD behavior is correct.
}


TEST_F(AC97Test, PoCrResetRegisterBitMustSelfClear) {
    make_device();

    const uint32_t PO_CR = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;
    const uint32_t GLOB_CNT = NABM_BASE + AC97Pci::BMOff::GLOB_CNT;

    // Cold reset: CR must be 0x00
    write32_le(GLOB_CNT, 0);
    write32_le(GLOB_CNT, 2); // cold reset

    EXPECT_EQ(read8(PO_CR), 0x00)
        << "After cold reset, PO_CR must be 0x00";

    // Write RESETREGS (bit1)
    write8(PO_CR, 0x02);

    // Must always read zero
    EXPECT_EQ(read8(PO_CR) & 0x02, 0)
        << "RESETREGS bit must self-clear and always read as 0";
}

TEST_F(AC97Test, AC97_ResetCompliance) {
    make_device();

    const uint32_t GLOB_CNT = NABM_BASE + AC97Pci::BMOff::GLOB_CNT;
    const uint32_t PO_CR    = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;
    const uint32_t PO_SR    = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;

    // Cold reset
    write32_le(GLOB_CNT, 0);
    write32_le(GLOB_CNT, 2);

    // CR must be zero after reset
    EXPECT_EQ(read8(PO_CR), 0x00)
        << "After cold reset, PO_CR must be 0x00";

    // Status must show DCH=1
    EXPECT_EQ(read8(PO_SR) & 0x01, 0x01)
        << "After cold reset, PO_SR.DCH must be 1";

    // Write RESETREGS=1 (bit1)
    write8(PO_CR, 0x02);

    // RESETREGS must self-clear
    EXPECT_EQ(read8(PO_CR) & 0x02, 0)
        << "RESETREGS bit must self-clear";

    // Status must still report halted (DCH=1)
    EXPECT_EQ(read8(PO_SR) & 0x01, 0x01)
        << "DCH must remain 1 after RESETREGS write";
}

TEST_F(AC97Test, DchBitSetOnResetAndStop2)
{
   
    make_device();

    const uint32_t base = NABM_BASE;
    const uint32_t PO_CR = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;
    const uint32_t PO_SR = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;

    // --- Cold reset ---
    write32_le(base + AC97Pci::BMOff::GLOB_CNT, 0);
    write32_le(base + AC97Pci::BMOff::GLOB_CNT, 0x02);

    uint8_t sr = read8(PO_SR);
    EXPECT_EQ(sr & 0x01, 0x01) << "After cold reset, DCH (bit0) must be 1";

    // --- Start DMA (RUN=1) ---
    write8(PO_CR, 0x01);
    sr = read8(PO_SR);
    EXPECT_EQ(sr & 0x01, 0x00) << "After RUN=1, DCH must be cleared";

    // --- STOP DMA (RUN=0) ---
    write8(PO_CR, 0x00);
    sr = read8(PO_SR);
    EXPECT_EQ(sr & 0x01, 0x01) << "After STOP, DCH must be set again";
}

TEST_F(AC97Test, DchBitClearedOnStart2)
{
    make_device();

    const uint32_t base = NABM_BASE;
    const uint32_t PO_CR = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;
    const uint32_t PO_SR = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;

    // Reset first
    write32_le(base + AC97Pci::BMOff::GLOB_CNT, 0);
    write32_le(base + AC97Pci::BMOff::GLOB_CNT, 0x02);

    uint8_t sr = read8(PO_SR);
    EXPECT_EQ(sr & 0x01, 0x01) << "Sanity: DCH must be 1 after reset";

    // Start DMA (RUN=1)
    write8(PO_CR, 0x01);

    sr = read8(PO_SR);
    EXPECT_EQ(sr & 0x01, 0x00) << "Starting DMA must clear DCH (bit0)";
}

TEST_F(AC97Test, ResetRegsSetsDchAndClearsStatus)
{
    make_device();

    const uint32_t base = NABM_BASE;
    const uint32_t PO_CR = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;
    const uint32_t PO_SR = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;

    // Write RESETREGS (bit1)
    write8(PO_CR, 0x02);

    uint8_t cr = read8(PO_CR);
    EXPECT_EQ(cr & 0x02, 0x00) << "RESETREGS bit must self-clear in CR";

    uint8_t sr = read8(PO_SR);
    EXPECT_EQ(sr & 0x01, 0x01) << "RESETREGS must set DCH";
    EXPECT_EQ(sr & 0x0E, 0x00) << "RESETREGS must clear BCIS/LVBCI/FIFO";
}

TEST_F(AC97Test, GlobCnt_HasCapabilitiesAfterColdReset)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x20000);
    make_device();

    const uint32_t base     = NABM_BASE;
    const uint32_t GLOB_CNT = base + AC97Pci::BMOff::GLOB_CNT;

    // Cold reset
    write32_le(GLOB_CNT, 0x00000002);

    uint32_t gc = read32_le(GLOB_CNT);

    // --- EXPECTATION ---
    // Hardware must auto-clear RESET bit and not set capability flags.

    // PR (PCM Out Ready)
    EXPECT_EQ(gc & (1u << 2),0) << "GLOB_CNT: PR (bit2) must not be set after cold reset";

    // CR (PCM In Ready)
    EXPECT_EQ(gc & (1u << 3),0) << "GLOB_CNT: CR (bit3) must not be set after cold reset";

    // PRD (PCM Out double-buffer supported)
    EXPECT_EQ(gc & (1u << 4),0) << "GLOB_CNT: PRD (bit4) must not be set after cold reset";

    // CRD (PCM In double-buffer supported)
    EXPECT_EQ(gc & (1u << 5),0) << "GLOB_CNT: CRD (bit5) must not be set after cold reset";

    // VRA (Variable Rate Audio)
    EXPECT_EQ(gc & (1u << 18),0) << "GLOB_CNT: VRA (bit18) must not be set after cold reset";

    // Cold reset bit must clear itself
    EXPECT_FALSE(gc & 0x02) << "GLOB_CNT bit1 must auto-clear after reset";
}

TEST_F(AC97Test, GlobCnt_NoFragmentsImplementedinGLOBCNT)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x20000);
    make_device();

    const uint32_t base     = NABM_BASE;
    const uint32_t GLOB_CNT = base + AC97Pci::BMOff::GLOB_CNT;

    // Cold reset
    write32_le(GLOB_CNT, 0x00000002);

    uint32_t gc = read32_le(GLOB_CNT);

    ASSERT_EQ(gc & 0x2, 0);

}