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


static constexpr uint32_t GS_CRDY_CODEC0 = 0x00000100;  // Codec-0 ready
static constexpr uint32_t GS_S0R  = (1u << 15);


class AC97NAMTest : public ::testing::Test {
protected:
    AC97NAMTest()
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


TEST_F(AC97NAMTest, ExtendedAudioRegistersCorrect)
{
    make_device();

    const uint32_t NAM = NAM_BASE;

    const uint32_t REG_28 = NAM + 0x28;
    const uint32_t REG_2A = NAM + 0x2A;

    // ---------------------------------------------------
    // 1) Check initial values
    // ---------------------------------------------------
    uint16_t ext_id = read16_le(REG_28);
    uint16_t ext_ctl = read16_le(REG_2A);

    EXPECT_EQ(ext_id, 0x0003)
        << "Extended Audio ID must report DAC + VRA";

    EXPECT_EQ(ext_ctl, 0x0003)
        << "Extended Audio Status must mirror initial VRA enabled";

    // ---------------------------------------------------
    // 2) Writes to 0x28 must be ignored (read-only)
    // ---------------------------------------------------
    write16_le(REG_28, 0xAAAA);
    uint16_t ext_id_after = read16_le(REG_28);

    EXPECT_EQ(ext_id_after, 0x0003)
        << "0x28 must remain read-only despite writes";

    uint16_t old_0x28 = ext_id;
    // ---------------------------------------------------
    // 3) Writes to 0x2A must be masked
    // ---------------------------------------------------
    // Try to disable VRA and set random reserved bits
    write16_le(REG_2A, 0x00FF);  // try to set VRA=0 + upper bits garbage

    uint16_t ext_ctl_after = read16_le(REG_2A);

    // VRA bit (bit1) must be forced ON because 0x28 says VRA supported
    EXPECT_TRUE(ext_ctl_after & 0x0002)
        << "VRA bit must not be allowed to clear if codec supports VRA";

    // Upper bits must NOT be writable
    EXPECT_EQ(ext_ctl_after & 0xFFF0, 0x0000)
        << "Reserved/unimplemented bits in 0x2A must remain 0";

    // Writable bits are 1–3, so masked result must be:
    // old = 0x0003
    // write = 0x00FF & 0x000E = 0x000E
    EXPECT_EQ(ext_ctl_after & 0x000E, 0x000E);    // writable bits applied
    EXPECT_EQ(ext_ctl_after & 0x0001, old_0x28 & 0x0001); // RO bit unchanged
}

TEST_F(AC97NAMTest, NAMResetResetsCodecRegisters)
{
    make_device();

    const uint32_t NAM = NAM_BASE;

    const uint32_t REG_RESET  = NAM + 0x00;
    const uint32_t REG_PCI_ID = NAM + 0x7C;   // vendor ID
    const uint32_t REG_EXT_ID = NAM + 0x28;
    const uint32_t REG_EXT_ST = NAM + 0x2A;
    const uint32_t REG_DACRT  = NAM + 0x2C;
    const uint32_t REG_ADCRT  = NAM + 0x32;
    const uint32_t REG_PCMVOL = NAM + 0x18;

    // --- Modify several registers before reset ---
    write16_le(REG_DACRT, 22050);     // sample rate
    write16_le(REG_ADCRT, 32000);
    write16_le(REG_PCMVOL, 0x1234);
    write16_le(REG_EXT_ST, 0x0000);   // disable VRA illegally

    // --- Trigger codec reset ---
    write16_le(REG_RESET, 0x0001);

    // --- Check that registers have been restored correctly ---

    // Extended ID must be DAC + VRA
    EXPECT_EQ(read16_le(REG_EXT_ID), 0x0003)
        << "0x28 Extended Audio ID must be restored to DAC+VRA.";

    // Extended Status must be restored to DAC + VRA enabled
    EXPECT_EQ(read16_le(REG_EXT_ST), 0x0003)
        << "0x2A Extended Audio Status must be reset to VRA enabled.";

    // Sample rates must reset to 48000 Hz
    EXPECT_EQ(read16_le(REG_DACRT), 48000)
        << "Front DAC rate must reset to 48000.";
    EXPECT_EQ(read16_le(REG_ADCRT), 48000)
        << "ADC rate must reset to 48000.";

    // Mixer state must reset (your init sets PCM out to 0x0808)
    EXPECT_EQ(read16_le(REG_PCMVOL), 0x0808)
        << "PCM out volume must reset to default 0x0808.";

    // Vendor ID must remain unchanged
    EXPECT_EQ(read16_le(REG_PCI_ID), 0x4144)
        << "Vendor ID must persist across codec reset.";
}

TEST_F(AC97NAMTest, PowerControlAffectsStatusCorrectly)
{
    make_device();

    const uint32_t NAM = NAM_BASE;

    const uint32_t REG_PWR  = NAM + 0x26;   // Power control/status
    const uint32_t REG_STAT = NAM + 0x28;   // Extended Audio ID (unchanged)
                                            // Or read REG_PWR itself if that's where you expose status

    // --- Case 1: Write 0x0000 => all blocks ON ---
    write16_le(REG_PWR, 0x0000);
    uint16_t st1 = read16_le(REG_PWR);

    EXPECT_EQ(st1 & 0x000F, 0x000F)
        << "Writing 0x0000 to power control must power ON all analog sections.";

    // --- Case 2: Disable DAC + ADC (bits 0 and 1) ---
    // Example: turn off DAC+ADC → status bits inverted for ready mask
    write16_le(REG_PWR, 0x0003);  // turn off blocks 0 and 1
    uint16_t st2 = read16_le(REG_PWR);

    EXPECT_EQ(st2 & 0x000F, (~0x0003) & 0x000F)
        << "Power status bits must reflect inverted mask of written value.";

    // --- Case 3: Random value, ensure only lower 4 bits affect status ---
    write16_le(REG_PWR, 0x00FF);  // Attempt to flip invalid bits
    uint16_t st3 = read16_le(REG_PWR);

    EXPECT_EQ(st3 & 0x000F, (~0x000F) & 0x000F)
        << "Only low 4 bits of power control should affect power status.";

    // Extended Audio ID must remain unchanged (sanity check)
    EXPECT_EQ(read16_le(NAM + 0x28), 0x0003)
        << "Extended Audio ID must not change due to power operations.";
}

TEST_F(AC97NAMTest, NAMResetAssertsCRDYandS0R)
{
    make_device();

    const uint32_t NAM  = NAM_BASE;
    const uint32_t NABM = NABM_BASE;

    const uint32_t REG_RESET   = NAM  + 0x00;
    const uint32_t REG_GLOBSTA = NABM + AC97Pci::BMOff::GLOB_STA; // usually 0x30

    // Pre-check: CRDY must become 1 after init
    uint32_t gs0 = read32_le(REG_GLOBSTA);
    EXPECT_NE(gs0 & 0x100, 0u)
        << "Codec should already be ready after init";

    // Write codec reset
    write16_le(REG_RESET, GS_CRDY_CODEC0);

    // After reset, controller must mark codec ready again
    uint32_t gs1 = read32_le(REG_GLOBSTA);

    EXPECT_NE(gs1 & GS_CRDY_CODEC0, 0u)
        << "CRDY must be asserted after NAM codec reset";

    // We look at the first value, since glob_sta will clear this on first read
    EXPECT_NE(gs0 & GS_S0R, 0u)
        << "Semaphore S0R must be set after NAM command completes";
}

TEST_F(AC97NAMTest, NAMWriteTogglesCodecSemaphore)
{
    make_device();

    const uint32_t NAM  = NAM_BASE;
    const uint32_t NABM = NABM_BASE;

    const uint32_t REG_PWR     = NAM + 0x26;   // arbitrary NAM command
    const uint32_t REG_GLOBSTA = NABM + AC97Pci::BMOff::GLOB_STA;

    // Ensure initial semaphore is set
    EXPECT_NE(read32_le(REG_GLOBSTA) & GS_S0R, 0u)
        << "Semaphore should start in ready state";

    // Perform write
    write16_le(REG_PWR, 0x0007);

    // After write handler → semaphore must be set again
    uint32_t gs = read32_le(REG_GLOBSTA);

    EXPECT_NE(gs & GS_S0R, 0u)
        << "S0R must be asserted after NAM write completes";
}

TEST_F(AC97NAMTest, MixerRegistersHaveCorrectMasking)
{
    make_device();

    const uint32_t NAM = NAM_BASE;

    const uint32_t REG_MASTER = NAM + 0x02;
    const uint32_t REG_HP     = NAM + 0x04;
    const uint32_t REG_PCM    = NAM + 0x18;
    const uint32_t REG_LINE   = NAM + 0x1A;

    auto test_mixer = [&](uint32_t reg)
    {
        // Write illegal bits
        write16_le(reg, 0xFFFF);
        uint16_t v = read16_le(reg);

        // Allowed bits: mute | 5-bit volume left | 5-bit volume right
        EXPECT_EQ(v & 0x1F1F, 0x1F1F)
            << "Volume bits must allow only 5 bits per channel.";

        EXPECT_EQ(v & 0x60E0, 0x0000)
            << "Reserved mixer bits must be zero.";
    };

    test_mixer(REG_MASTER);
    test_mixer(REG_HP);
    test_mixer(REG_PCM);
    test_mixer(REG_LINE);
}

TEST_F(AC97NAMTest, NAMWriteMustToggleCommandSemaphores)
{
    make_device();

    const uint32_t NAM = NAM_BASE;
    const uint32_t NABM = NABM_BASE;        // where your GLOB_STA lives

    const uint32_t REG_RECG = NAM + 0x1C;
    const uint32_t REG_GLOB_STA = NABM + 0x30;

    // Ensure codec ready bit initially set (reset state)
    uint32_t st0 = read32_le(REG_GLOB_STA);
    EXPECT_TRUE(st0 & 0x00000001)
        << "Codec should start ready after init.";

    // Write to a NAM register
    write16_le(REG_RECG, 0x8A05);

    // After write, BUSY should have been set during command_begin
    uint32_t st1 = read32_le(REG_GLOB_STA);
    EXPECT_TRUE(st1 & BUSY_BIT) 
        << "NAM write must assert BUSY bit in GLOB_STA.";

    // After write completes, ready should be asserted again
    uint32_t st2 = read32_le(REG_GLOB_STA);
    EXPECT_TRUE(st2 & READY_BIT)
        << "NAM write must assert READY after completion.";

    // READY must NOT assert instantly; it must follow command sequencing
}

