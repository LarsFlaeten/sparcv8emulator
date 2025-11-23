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
    // enforce VRA=1 → bit1 forced on → 0x000E already has bit1
    // final = 0x000E
    EXPECT_EQ(ext_ctl_after, 0x000E)
        << "0x2A masking logic incorrect; expected masked write with VRA forced on";
}
