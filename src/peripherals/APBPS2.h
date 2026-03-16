// SPDX-License-Identifier: MIT
#pragma once

#include "apb_slave.h"
#include "gaisler/ambapp_ids.h"
#include "IRQMP.h"

#include <queue>
#include <mutex>
#include <functional>

// GRLIB APBPS2 PS/2 keyboard controller emulation.
// Register map (big-endian, 32-bit words):
//   0x00 DATA   [7:0] received byte
//   0x04 STATUS bit0=DR (data ready), bit1=PE, bit2=FE, bit3=KI, bits31:27=RCNT
//   0x08 CTRL   bit0=RE (rx enable), bit1=TE, bit2=RI (rx IRQ enable), bit3=TI
//   0x0C RELOAD clock prescaler (write-only in practice)

class APBPS2 : public apb_slave {
public:
    APBPS2(IRQMP& irqmp, uint8_t irq_line)
        : irqmp_(irqmp), irq_line_(irq_line), ctrl_(0) {}

    u32 vendor_id() const override { return VENDOR_GAISLER; }
    u32 device_id() const override { return GAISLER_APBPS2; }

    void reset() override {
        std::lock_guard<std::mutex> lock(mtx_);
        ctrl_ = 0;
        while (!rxq_.empty()) rxq_.pop();
    }

    // Called by Display render thread for each PS/2 byte to inject
    void push_byte(uint8_t byte) {
        bool trigger = false;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (rxq_.size() < 32)
                rxq_.push(byte);
            trigger = (ctrl_ & 0x4) != 0; // RI bit
        }
        if (trigger)
            irqmp_.trigger_irq(irq_line_);
    }

    u32 read(u32 offset) const override {
        std::lock_guard<std::mutex> lock(mtx_);
        switch (offset & 0x0c) {
            case 0x00: { // DATA — pop and return next byte
                if (rxq_.empty()) return 0;
                uint8_t b = rxq_.front();
                rxq_.pop();
                return b;
            }
            case 0x04: { // STATUS
                uint32_t rcnt = (uint32_t)rxq_.size();
                if (rcnt > 31) rcnt = 31;
                uint32_t st = 0;
                if (!rxq_.empty()) st |= 0x01; // DR
                st |= rcnt << 27;              // RCNT
                return st;
            }
            case 0x08: return ctrl_; // CTRL
            case 0x0c: return 0;     // RELOAD
        }
        return 0;
    }

    void write(u32 offset, u32 value) override {
        std::lock_guard<std::mutex> lock(mtx_);
        switch (offset & 0x0c) {
            case 0x04: // STATUS write clears error flags (ignored here)
                break;
            case 0x08: // CTRL
                ctrl_ = value & 0xf;
                break;
            case 0x0c: // RELOAD
                break;
            default: break;
        }
    }

private:
    IRQMP&              irqmp_;
    uint8_t             irq_line_;
    uint32_t            ctrl_;
    mutable std::queue<uint8_t> rxq_;
    mutable std::mutex  mtx_;
};
