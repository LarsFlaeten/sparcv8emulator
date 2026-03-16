// SPDX-License-Identifier: MIT
#include "../src/peripherals/APBPS2.h"
#include "../src/peripherals/IRQMP.h"
#include "../src/peripherals/MCTRL.h"
#include "../src/peripherals/Display.h"
#include "../src/sparcv8/CPU.h"

#include <gtest/gtest.h>
#include <vector>

// =========================================================================
// Helpers
// =========================================================================

// Build an IRQMP + CPU with IRQ line unmasked so we can observe trigger_irq.
struct IrqFixture {
    MCtrl  mctrl;
    IRQMP  intc{1};
    CPU    cpu{mctrl, intc, 0};

    IrqFixture() {
        intc.set_cpu_ptr(&cpu, 0);
        // Unmask IRQ 5 for CPU 0
        intc.write(0x40, 1u << 5);
    }

    bool irq5_pending() const {
        return (intc.read(0x04) >> 5) & 1;
    }

    void clear5() { intc.clear_irq(5, 0); }
};

// Build a minimal SDL_KeyboardEvent (no SDL runtime needed — just a struct)
static SDL_KeyboardEvent make_sdl_key(SDL_Scancode sc, Uint32 type) {
    SDL_KeyboardEvent e{};
    e.type          = type;
    e.keysym.scancode = sc;
    return e;
}

// =========================================================================
// APBPS2 register tests
// =========================================================================

class APBPS2Test : public ::testing::Test {
protected:
    IrqFixture fix;
    APBPS2 kbd{fix.intc, 5};
};

TEST_F(APBPS2Test, VendorDeviceID) {
    EXPECT_EQ(kbd.vendor_id(), VENDOR_GAISLER);
    EXPECT_EQ(kbd.device_id(), GAISLER_APBPS2);
}

TEST_F(APBPS2Test, EmptyQueue_DRZero_DataZero) {
    EXPECT_EQ(kbd.read(0x04) & 0x01, 0u);  // STATUS.DR == 0
    EXPECT_EQ(kbd.read(0x00), 0u);          // DATA == 0
}

TEST_F(APBPS2Test, PushOneByte_DRSet_DataCorrect_DrClearsAfterRead) {
    kbd.push_byte(0xAB);

    EXPECT_EQ(kbd.read(0x04) & 0x01, 1u);  // DR set
    EXPECT_EQ(kbd.read(0x00), 0xABu);      // correct byte returned
    EXPECT_EQ(kbd.read(0x04) & 0x01, 0u);  // DR cleared after read
}

TEST_F(APBPS2Test, RCNT_Field_ReflectsQueueDepth) {
    for (int i = 0; i < 5; i++)
        kbd.push_byte(0x10 + i);

    uint32_t st   = kbd.read(0x04);
    uint32_t rcnt = (st >> 27) & 0x1F;
    EXPECT_EQ(rcnt, 5u);
}

TEST_F(APBPS2Test, QueueOverflow_DropsAbove32) {
    for (int i = 0; i < 40; i++)
        kbd.push_byte(0x01);

    // RCNT must be capped at 31 (max representable), actual stored ≤ 32
    uint32_t rcnt = (kbd.read(0x04) >> 27) & 0x1F;
    EXPECT_LE(rcnt, 31u);

    // Drain: should get exactly 32 bytes, then empty
    int count = 0;
    while (kbd.read(0x04) & 0x01) {
        kbd.read(0x00);
        count++;
    }
    EXPECT_EQ(count, 32);
}

TEST_F(APBPS2Test, CtrlReadWrite) {
    kbd.write(0x08, 0x07);         // RE | TE | RI
    EXPECT_EQ(kbd.read(0x08), 0x07u);
    kbd.write(0x08, 0x00);
    EXPECT_EQ(kbd.read(0x08), 0x00u);
}

TEST_F(APBPS2Test, Reset_ClearsQueueAndCtrl) {
    kbd.push_byte(0x55);
    kbd.write(0x08, 0x05);
    kbd.reset();

    EXPECT_EQ(kbd.read(0x04) & 0x01, 0u);  // DR == 0
    EXPECT_EQ(kbd.read(0x08), 0x00u);      // CTRL == 0
}

TEST_F(APBPS2Test, DrainQueue_PreservesOrder) {
    const uint8_t bytes[] = {0x1C, 0xF0, 0x1C}; // A make, break
    for (auto b : bytes) kbd.push_byte(b);

    for (auto expected : bytes) {
        ASSERT_EQ(kbd.read(0x04) & 0x01, 1u);
        EXPECT_EQ(kbd.read(0x00), expected);
    }
    EXPECT_EQ(kbd.read(0x04) & 0x01, 0u);
}

// =========================================================================
// TX auto-ACK tests
// =========================================================================

TEST_F(APBPS2Test, TX_AnyByte_YieldsACK) {
    kbd.write(0x00, 0xF4);  // enable scanning command
    ASSERT_EQ(kbd.read(0x04) & 0x01, 1u);  // DR set
    EXPECT_EQ(kbd.read(0x00), 0xFAu);      // ACK byte
    EXPECT_EQ(kbd.read(0x04) & 0x01, 0u);  // queue empty after
}

TEST_F(APBPS2Test, TX_Reset_YieldsACK_Then_BAT) {
    kbd.write(0x00, 0xFF);  // reset command
    ASSERT_EQ(kbd.read(0x04) & 0x01, 1u);
    EXPECT_EQ(kbd.read(0x00), 0xFAu);      // ACK first
    ASSERT_EQ(kbd.read(0x04) & 0x01, 1u);
    EXPECT_EQ(kbd.read(0x00), 0xAAu);      // BAT completion second
    EXPECT_EQ(kbd.read(0x04) & 0x01, 0u);  // queue empty after
}

TEST_F(APBPS2Test, TX_IRQ_FiredOnACK_WhenRISet) {
    kbd.write(0x08, 0x05); // RE | RI
    kbd.write(0x00, 0xF4); // any TX command
    EXPECT_TRUE(fix.irq5_pending());
}

TEST_F(APBPS2Test, TX_NoIRQ_WhenRINotSet) {
    kbd.write(0x00, 0xF4);
    EXPECT_FALSE(fix.irq5_pending());
}

// =========================================================================
// IRQ delivery tests
// =========================================================================

TEST_F(APBPS2Test, NoIRQ_WhenRINotSet) {
    // RI bit (bit 2) not set in CTRL
    kbd.push_byte(0x1C);
    EXPECT_FALSE(fix.irq5_pending());
}

TEST_F(APBPS2Test, IRQ_Triggered_WhenRISet) {
    kbd.write(0x08, 0x05); // RE | RI
    kbd.push_byte(0x1C);
    EXPECT_TRUE(fix.irq5_pending());
}

TEST_F(APBPS2Test, IRQ_FiredForEachByte_WhenRISet) {
    kbd.write(0x08, 0x05); // RE | RI

    kbd.push_byte(0x1C);
    ASSERT_TRUE(fix.irq5_pending());
    // Drain and clear
    kbd.read(0x00);
    fix.clear5();

    kbd.push_byte(0x5A);
    EXPECT_TRUE(fix.irq5_pending());
}

// =========================================================================
// PS/2 Set 2 scan code tests (via Display::handle_key_event)
// =========================================================================

class PS2ScanCodeTest : public ::testing::Test {
protected:
    std::vector<uint8_t> bytes;
    std::function<void(uint8_t)> cb = [this](uint8_t b){ bytes.push_back(b); };
};

TEST_F(PS2ScanCodeTest, LetterA_Make) {
    auto e = make_sdl_key(SDL_SCANCODE_A, SDL_KEYDOWN);
    Display::handle_key_event(e, cb);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x1C);  // PS/2 Set 2 code for A
}

TEST_F(PS2ScanCodeTest, LetterA_Break) {
    auto e = make_sdl_key(SDL_SCANCODE_A, SDL_KEYUP);
    Display::handle_key_event(e, cb);
    ASSERT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[0], 0xF0);
    EXPECT_EQ(bytes[1], 0x1C);
}

TEST_F(PS2ScanCodeTest, Return_Make) {
    auto e = make_sdl_key(SDL_SCANCODE_RETURN, SDL_KEYDOWN);
    Display::handle_key_event(e, cb);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x5A);
}

TEST_F(PS2ScanCodeTest, Backspace_Make) {
    auto e = make_sdl_key(SDL_SCANCODE_BACKSPACE, SDL_KEYDOWN);
    Display::handle_key_event(e, cb);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x66);
}

TEST_F(PS2ScanCodeTest, ArrowUp_Make_Extended) {
    auto e = make_sdl_key(SDL_SCANCODE_UP, SDL_KEYDOWN);
    Display::handle_key_event(e, cb);
    ASSERT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[0], 0xE0);
    EXPECT_EQ(bytes[1], 0x75);
}

TEST_F(PS2ScanCodeTest, ArrowUp_Break_Extended) {
    auto e = make_sdl_key(SDL_SCANCODE_UP, SDL_KEYUP);
    Display::handle_key_event(e, cb);
    ASSERT_EQ(bytes.size(), 3u);
    EXPECT_EQ(bytes[0], 0xE0);
    EXPECT_EQ(bytes[1], 0xF0);
    EXPECT_EQ(bytes[2], 0x75);
}

TEST_F(PS2ScanCodeTest, RCtrl_Make_Extended) {
    auto e = make_sdl_key(SDL_SCANCODE_RCTRL, SDL_KEYDOWN);
    Display::handle_key_event(e, cb);
    ASSERT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[0], 0xE0);
    EXPECT_EQ(bytes[1], 0x14);
}

TEST_F(PS2ScanCodeTest, LShift_Make_NotExtended) {
    auto e = make_sdl_key(SDL_SCANCODE_LSHIFT, SDL_KEYDOWN);
    Display::handle_key_event(e, cb);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x12);
}

TEST_F(PS2ScanCodeTest, UnmappedKey_NoBytes) {
    // SDL_SCANCODE_UNKNOWN = 0 — not in our table
    auto e = make_sdl_key(SDL_SCANCODE_UNKNOWN, SDL_KEYDOWN);
    Display::handle_key_event(e, cb);
    EXPECT_EQ(bytes.size(), 0u);
}

TEST_F(PS2ScanCodeTest, F1_Make) {
    auto e = make_sdl_key(SDL_SCANCODE_F1, SDL_KEYDOWN);
    Display::handle_key_event(e, cb);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x05);
}

TEST_F(PS2ScanCodeTest, Delete_Make_Extended) {
    auto e = make_sdl_key(SDL_SCANCODE_DELETE, SDL_KEYDOWN);
    Display::handle_key_event(e, cb);
    ASSERT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[0], 0xE0);
    EXPECT_EQ(bytes[1], 0x71);
}
