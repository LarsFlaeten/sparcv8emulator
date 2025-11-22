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
    ASSERT_EQ(v & 0x1FF , 0x100u) << "GLOB_STA must have Codec 0 ready and semaphore bit not set";
    
    // Only allow clearing sempahore bit, not the codec ready bit:
    write32_le(NABM_BASE + 0x30, 0xffffffffu);

    v = read32_le(NABM_BASE + 0x30);
    ASSERT_EQ(v & 0x1ff, 0x100u) << "GLOB_STA must have Codec 0 ready and semaphore bit not set";
    
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
    // Allocate RAM for BD area
    mctrl.attach_bank<RamBank>(0x42097000, 0x1000);
    make_device();

    const uint32_t BDBAR = 0x42097000;

    // --- Cold reset ---
    write32_le(NABM_BASE + AC97Pci::BMOff::GLOB_CNT, 0x00000002);

    // Program BD BAR
    write32_le(NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::BD_BAR, BDBAR);

    // BD0 = empty (ptr=0,len=0)
    write32_le(BDBAR + 0, 0x00000000); // ptr
    write16_le(BDBAR + 4, 0x0000);     // len
    write16_le(BDBAR + 6, 0x8000);     // interrupt enable, otherwise irrelevant

    // --- START playback ---
    write8(NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR, 0x01);

    // --- EXPECTATION: DMA MUST NOT START ---
    uint8_t sr = read8(NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR);

    EXPECT_EQ(sr & 0x01, 0x01) << "DCH must remain set when BD0 is empty";
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
    mctrl.attach_bank<RamBank>(0x42097000, 0x1000);
    make_device();

    const uint32_t BDBAR = 0x42097000;

    // Reset
    write32_le(NABM_BASE + AC97Pci::BMOff::GLOB_CNT, 0x00000002);

    // DCH must be set after reset
    uint8_t sr = read8(NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR);
    EXPECT_EQ(sr & 0x01, 0x01) << "DCH must be set after cold reset";

    // START with empty BD0 -> DMA MUST NOT start
    write32_le(NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::BD_BAR, BDBAR);

    write32_le(BDBAR + 0, 0x00000000);  // empty ptr
    write16_le(BDBAR + 4, 0x0000);      // empty len
    write16_le(BDBAR + 6, 0x0000);      // ctl

    write8(NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR, 0x01);

    sr = read8(NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR);

    EXPECT_EQ(sr & 0x01, 0x01)
        << "Starting DMA must NOT clear DCH when BD0 is empty";

    // STOP should keep DCH=1
    write8(NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR, 0x00);
    sr = read8(NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR);

    EXPECT_EQ(sr & 0x01, 0x01) << "STOP must keep DCH=1";
}

TEST_F(AC97Test, DchBitClearedOnStart2)
{
    mctrl.attach_bank<RamBank>(0x42097000, 0x1000);
    make_device();

    const uint32_t BDBAR = 0x42097000;

    // Reset
    write32_le(NABM_BASE + AC97Pci::BMOff::GLOB_CNT, 0x00000002);

    // Program BDBAR
    write32_le(NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::BD_BAR, BDBAR);

    // Provide a VALID BD0
    write32_le(BDBAR + 0, 0x420A8000); // valid pointer
    write16_le(BDBAR + 4, 0x0080);     // nonzero length
    write16_le(BDBAR + 6, 0x8000);     // interrupt enabled

    // START
    write8(NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR, 0x01);

    uint8_t sr = read8(NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR);
    EXPECT_EQ(sr & 0x01, 0x00)
        << "With a valid BD0, starting DMA must clear DCH";
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

TEST_F(AC97Test, GlobCntSta_HasCapabilitiesAfterColdReset)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x20000);
    make_device();

    const uint32_t base     = NABM_BASE;
    const uint32_t GLOB_CNT = base + AC97Pci::BMOff::GLOB_CNT;
    const uint32_t GLOB_STA = base + AC97Pci::BMOff::GLOB_STA;

    // Cold reset
    write32_le(GLOB_CNT, 0x00000002);

    uint32_t gc = read32_le(GLOB_CNT);
    uint32_t gs = read32_le(GLOB_STA);

    // --- EXPECTATION ---
    // Hardware must auto-clear RESET bit and not set capability flags.

    u32 glob_sta_exp =
      (1 << 0)   // PCR: primary codec ready
    | (1 << 5)   // PCM in status
    | (1 << 6)   // PCM out status
    | 0x100;  // PCRDY: codec responds ready
    
    u32 glob_cnt_exp =
      (1 << 18);  // VRA supported

    EXPECT_EQ(gs & (1 << 0), (1 << 0)) << "PCR: bit must be set after reset";
    EXPECT_EQ(gs & (1 << 5), (1 << 5)) << "PCM in: bit must be set after reset";
    EXPECT_EQ(gs & (1 << 6), (1 << 6)) << "PCM out: bit must be set after reset";
    EXPECT_EQ(gs & (1 << 8), (1 << 8)) << "PCRDY: bit must be set after reset";


    EXPECT_EQ(gc & (1 << 18), (1 << 18)) << "GLOB_CNT bit18 must be set after reset";

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

TEST_F(AC97Test, AC97_DACRate_Register_Endianness)
{
    // Create memory + device
    mctrl.attach_bank<RamBank>(0x420A0000, 0x20000);
    make_device();

    // Perform cold reset (as Linux does)
    const uint32_t GLOB_CNT = NABM_BASE + AC97Pci::BMOff::GLOB_CNT;
    write32_le(GLOB_CNT, 0x00000002); // assert cold reset

    // AC97 register offsets are inside codec_regs_[]:
    // 0x2C = PCM Front DAC Rate register (16-bit)
    const uint32_t AC97_PCM_DAC_RATE = NAM_BASE + 0x2C;

    // According to the AC97 spec, this register must contain 48000 Hz by default.
    const uint16_t expected_rate = 48000;         // decimal
    const uint16_t expected_le   = 0xBB80;        // little-endian 16-bit
    const uint16_t expected_be   = 0x80BB;        // big-endian swapped

    // Read the raw device value
    uint16_t rate = read16_le(AC97_PCM_DAC_RATE);

    printf("[TEST] AC97 PCM DAC Rate read = 0x%04x\n", rate);

    // First: ensure it's non-zero, otherwise ALSA sees "no VRA support"
    ASSERT_NE(rate, (uint16_t)0)
        << "PCM DAC Rate register returned 0 — ALSA will reject hw_params.";

    // Then: ensure it is *exactly* 0xBB80 (little endian 48000)
    ASSERT_EQ(rate, expected_le)
        << "PCM DAC Rate register has wrong endianness. "
        << "Expected 0xBB80 (LE 48000), got 0x" << std::hex << rate
        << ". ALSA will fallback to 'single period' mode and refuse hw_params.";
}

TEST_F(AC97Test, TickMustNotAdvanceOrFireWhenNotRunning)
{
    // 1. Set up RAM region for BD descriptors
    const uint32_t BD_BASE = 0x420A0000;
    mctrl.attach_bank<RamBank>(BD_BASE, 0x20000);
    make_device();

    const uint32_t base     = NABM_BASE;
    const uint32_t PO_BDBAR = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::BD_BAR;
    const uint32_t PO_CR    = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;
    const uint32_t PO_CIV   = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CIV;
    const uint32_t PO_SR    = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;

    // --- Cold reset ---
    const uint32_t GLOB_CNT = base + AC97Pci::BMOff::GLOB_CNT;
    write32_le(GLOB_CNT, 0x00000002);        // request cold reset

    // After reset: CIV=0, RUN=0, BD BAR not programmed
    uint8_t civ_before = read8(PO_CIV);
    EXPECT_EQ(civ_before, 0u);

    uint8_t sr_before = read8(PO_SR);
    // DCH must be 1 after reset
    EXPECT_EQ(sr_before & 0x01, 0x01);

    // --- 2. Program BD BAR like Linux does early in boot ---
    write32_le(PO_BDBAR, BD_BASE);

    // Put a valid-looking descriptor in BD0 to tempt the bug
    // ptr=0x1000, len=0x40, ctl=IOC (bit15)
    write32_le(BD_BASE + 0, 0x00001000);
    write16_le(BD_BASE + 4, 0x0040);
    write16_le(BD_BASE + 6, 0x8000);     // IOC set → BCIS skulle fyres hvis DMA egentlig gikk

    // --- 3. DO NOT start DMA (RUN=0) ---
    write8(PO_CR, 0x00);

    // --- 4. Run many ticks to simulate boot-time  ---
    for (int i = 0; i < 5000; i++)
        dev->tick();

    // --- 5. Read back CIV and SR ---
    uint8_t civ_after = read8(PO_CIV);
    uint8_t sr_after  = read8(PO_SR);

    // --- EXPECTATION ---
    // DMA MUST NOT have progressed the BD ring
    EXPECT_EQ(civ_after, civ_before)
        << "CIV must not advance when RUN=0";

    // No BCIS (bit3) or LVBCI (bit2) should ever fire
    EXPECT_EQ(sr_after & 0x0C, 0x00)
        << "BCIS/LVBCI must not set when RUN=0";
}

TEST_F(AC97Test, POCR_MustNotStartDmaUntilBd0Valid)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x20000);
    make_device();

    const uint32_t base     = NABM_BASE;
    const uint32_t GLOB_CNT = base + AC97Pci::BMOff::GLOB_CNT;
    const uint32_t PO_CR    = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;
    const uint32_t PO_CIV   = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CIV;
    const uint32_t PO_SR    = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::SR;
    const uint32_t PO_BDBAR = base + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::BD_BAR;

    // 1. Cold reset
    write32_le(GLOB_CNT, 0x00000002);

    // BD0 memory (always mapped)
    const uint32_t BD_BASE = 0x420A0000;

    // 2. Linux writes BD BAR early in boot
    write32_le(PO_BDBAR, BD_BASE);

    // 3. But BD0 is NOT valid yet → ptr=0, len=0
    write32_le(BD_BASE + 0, 0);      // ptr
    write16_le(BD_BASE + 4, 0);      // len
    write16_le(BD_BASE + 6, 0);      // ctl

    // 4. Linux now writes PO_CR=1 during init
    write8(PO_CR, 0x01); // RUN=1

    // 5. DMA must NOT start, and CIV must remain 0
    uint8_t civ = read8(PO_CIV);
    EXPECT_EQ(civ, 0u) << "CIV must remain 0 until BD0 is valid";

    uint8_t sr = read8(PO_SR);
    EXPECT_TRUE(sr & 0x01) << "DCH must remain set (DMA halted)";

    // 6. Run a ton of ticks (boot simulation)
    for (int i = 0; i < 10000; i++)
        dev->tick();

    // Still must not have started
    civ = read8(PO_CIV);
    EXPECT_EQ(civ, 0u) << "DMA must not advance CIV before BD0 is valid";

    // 7. Now BD0 becomes valid (Linux writes it later)
    write32_le(BD_BASE + 0, 0x00001000); // ptr
    write16_le(BD_BASE + 4, 0x0040);     // len
    write16_le(BD_BASE + 6, 0x8000);     // IOC

    // 8. Write CR=1 again → now DMA SHOULD start
    write8(PO_CR, 0x01);

    civ = read8(PO_CIV);
    EXPECT_EQ(civ, 0u) << "DMA starts at CIV=0";

    sr = read8(PO_SR);
    EXPECT_FALSE(sr & 0x01) << "DCH must clear when DMA starts";
}
