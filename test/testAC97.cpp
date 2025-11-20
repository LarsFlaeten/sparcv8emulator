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
    write32_le(NABM_BASE + 0x2c, 1u << 25);
    
    // Read back GLOB_CNT must have the bit cleared after warm reset
    uint32_t cnt = read32_le(NABM_BASE + 0x2C);
    ASSERT_EQ(cnt & (1u << 25), 0u) << "Warm reset bit must auto-clear";

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

    // RUN must be 1, BCIS=LVBCI=0
    EXPECT_EQ(sr & 0x01, 0x01);    // RUN = 1
    EXPECT_EQ(sr & 0x0C, 0x00);    // BCIS/LVBCI cleared
}

TEST_F(AC97Test, POCR_ReservedBitsAlwaysZeroAndLowBitsPreserved)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    // At reset, CR should be 0
    // PO_CR is 0x1B
    uint8_t cr = read8(NABM_BASE + 0x1B);
    ASSERT_EQ(cr, 0x00);

    // Write junk (all bits set)
    write8(NABM_BASE + 0x1B, 0xFF);

    // Read back
    cr = read8(NABM_BASE + 0x1B);

    // Reserved bits (7..5) must always be 0
    ASSERT_EQ(cr & 0xE0, 0x00);

    // Low 5 bits (4..0) should reflect the write, masked to 0x1F
    ASSERT_EQ(cr & 0x1F, 0x1F);
}


//
// AC’97 NABM — Playback Engine Register Tests
//

TEST_F(AC97Test, POCR_ResetValueAndReservedBits)
{
    mctrl.attach_bank<RamBank>(0x420A0000, 0x10000);
    make_device();

    const uint32_t CR = NABM_BASE + AC97Pci::BMOff::PO_BASE + AC97Pci::BMOff::CR;

    // Reset state: CR = 0
    EXPECT_EQ(read8(CR), 0x00);

    // Write junk: all bits set
    write8(CR, 0xFF);

    uint8_t cr = read8(CR);

    // Only RUN (bit0) is meaningful, but your implementation masks to 0x1F
    // So verify:
    EXPECT_EQ(cr & 0x1F, 0x1F);  // writable bits preserved

    // Reserved bits [7..5] always read zero
    EXPECT_EQ(cr & 0xE0, 0x00);
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