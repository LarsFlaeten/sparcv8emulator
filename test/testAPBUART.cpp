// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>
#include "../src/peripherals/APBUART.h"

#include <queue>

class FakeConsole : public Console {
public:
    std::string out;
    std::queue<uint8_t> inq;

    void Putc(unsigned c) override {
        out.push_back((char)c);
    }

    bool Hit() override {
        return !inq.empty();
    }

    uint8_t Getc() override {
        uint8_t v = inq.front();
        inq.pop();
        return v;
    }
};


class APBUARTTest : public ::testing::Test {
protected:
    FakeConsole c;
    APBUART u;
    //FakeConsole c;
    APBUARTTest() : u(c) {;}

    void SetUp() override {
        //u.console = c;   // requires console to be public or via setter
    }
};

// ------------------------
// TX interrupt edge test
// ------------------------

TEST_F(APBUARTTest, TXInterruptEdgeTriggeredOnWrite)
{
    u.write(0x8, 0x8); // TI bit

    ASSERT_FALSE(u.check_irq());

    u.write(0x0, 'A');
    ASSERT_TRUE(u.check_irq());
    ASSERT_FALSE(u.check_irq());

    u.write(0x0, 'B');
    ASSERT_TRUE(u.check_irq());
    ASSERT_FALSE(u.check_irq());
}

// ------------------------
// RX FIFO test
// ------------------------

TEST_F(APBUARTTest, RXFIFO_OrderAndStatus)
{
    c.inq.push('H');
    c.inq.push('i');

    u.input_scheduled();
    u.input_scheduled();

    u32 s = u.read(0x4);
    ASSERT_TRUE(s & 1);              // DREADY
    ASSERT_EQ((s >> 26) & 0x3F, 2);  // RCNT=2

    ASSERT_EQ((char)u.read(0x0), 'H');
    ASSERT_EQ((char)u.read(0x0), 'i');

    ASSERT_EQ(((u.read(0x4) >> 26) & 0x3F), 0);
}

// ------------------------
// TX periodic interrupt test
// ------------------------

TEST_F(APBUARTTest, TXInterruptPeriodicWhenEmpty)
{
    u.write(0x8, 0x8); // TI enable

    /*
    auto tick = [&]() {
        if (true) { // TX FIFO always empty after Putc()
            u.tx_emptied_int_pending = true;
        }
    };*/

    u.tick_scheduled();
    ASSERT_TRUE(u.check_irq());

    u.tick_scheduled();
    ASSERT_TRUE(u.check_irq());
    ASSERT_FALSE(u.check_irq());

    u.tick_scheduled();
    ASSERT_TRUE(u.check_irq());
}
