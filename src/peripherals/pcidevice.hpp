// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <functional>


class PciDevice {
public:
    
    virtual ~PciDevice() = default;
    
    virtual uint8_t  config_read8 (uint16_t off) = 0;
    virtual uint16_t config_read16(uint16_t off) = 0;
    virtual uint32_t config_read32(uint16_t off) = 0;
    virtual void     config_write8 (uint16_t off, uint8_t  val) = 0;
    virtual void     config_write16(uint16_t off, uint16_t val) = 0;
    virtual void     config_write32(uint16_t off, uint32_t val) = 0;

    virtual uint8_t  io_read8 (uint32_t port)  = 0;
    virtual uint16_t io_read16(uint32_t port)  = 0;
    virtual uint32_t io_read32(uint32_t port)  = 0;
    virtual void     io_write8 (uint32_t port, uint8_t  val) = 0;
    virtual void     io_write16(uint32_t port, uint16_t val) = 0;
    virtual void     io_write32(uint32_t port, uint32_t val) = 0;

    //virtual std::optional<uint8_t> irq_pin() const = 0;

    virtual void tick() {;} // empty tick if not implemented in derived

    virtual void set_intx_cb(std::function<void()> cb) = 0;

    // Returns true if the device is currently asserting PCI INTA# (level-sensitive)
    virtual bool inta_asserted() const { return false; }
};
