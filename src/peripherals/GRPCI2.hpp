// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <iostream>

#include "../common.h"
#include "gaisler/ambapp_ids.h"



#include "apb_slave.h"
#include "IRQMP.h"
#include "MCTRL.h"
#include "pcidevice.hpp"



class GRPCI2 : public apb_slave {

public:
    struct Map {
        static constexpr uint32_t PCI_IO_BASE   = 0xFFFA0000;
        static constexpr uint32_t PCI_CONF_BASE = 0xFFFB0000;
        static constexpr uint32_t PCI_MEM_BASE  = 0x24000000;
    };

    GRPCI2(IRQMP& irqmp);
    virtual ~GRPCI2();

    // I/O Amba registers at 0x80000...something
    virtual u32 read(u32 offset) const;
    virtual void write(u32 offset, u32 value);

    u32 config_read(u32 paddr, u8 size);
    u32 io_read(u32 paddr, u8 size);
    void config_write(u32 paddr, u32 val, u8 size);
    void io_write(u32 paddr, u32 val, u8 size);


    void signal_pci_cfg_access_complete() const;

    virtual void reset(){;}

    virtual u32 vendor_id() const {return VENDOR_GAISLER;}
    virtual u32 device_id() const {return GAISLER_GRPCI2;};

    void    attach_device(std::unique_ptr<PciDevice>&& dev) {
        device_ = std::move(dev);

        device_->set_intx_cb([this](){this->raise_pci_irq(6);});
    }

    void raise_pci_irq(uint8_t slot) {
        // For now, always route to a single IRQ line, e.g. #2
        irqmp_.trigger_irq(2);
        //printf("[GRPCI2] PCI IRQ #2 from slot %u -> IRQ2\n", slot);
    }

    const PciDevice& get_pci_device() const {
        if(!device_)
            throw std::runtime_error("Device not attached to PCI bridge.");
        return *device_;
    }

    PciDevice& get_pci_device() {
        if(!device_)
            throw std::runtime_error("Device not attached to PCI bridge.");
        return *device_;
    }

    void tick() {
        
        maybe_tick_device();
    }

    void maybe_tick_device()
    {
        using clock = std::chrono::steady_clock;
        using namespace std::chrono_literals;

        static auto last = clock::now();

        auto now = clock::now();

        // tick at ~1 kHz (every 1 ms)
        // Accumulated, handles long cpu cycles
        while (now - last >= 1ms) {
            device_->tick();
            last += 1ms;
        }
    }

protected:
    IRQMP& irqmp_;

    void pci_reset();
    void pci_master_reset();
    void pci_target_reset();

    void enable_dma();
    void disable_dma();

    u32  write_dma_ctrl(u32 orig_reg, u32 new_value);
    u32  write_sts_cap(u32 orig_reg, u32 new_value);

    u32 ctrl_ = {0};    // 0x0
    mutable u32 sts_cap_ = {0}; // 0x4
    
    u32 dma_ctrl_ = {0}; // 0x10
    u32 dma_base_ = {0}; // 0x14
    u32 io_map_ = {0}; // 0x0C
    u32 ahbm2pci_[16] = {}; // 0x40 - 0x7C

    //static constexpr u32 TICK_SCALER_RLD = 0x3FFU;
    static constexpr u32 TICK_SCALER_RLD = 0x0FFU;
    u32 tick_scaler = TICK_SCALER_RLD;

    std::unique_ptr<PciDevice> device_;

    static std::string to_hex(u32 val) {
        char buf[11];
        snprintf(buf, sizeof(buf), "%08X", val);
        return buf;
    }
};


class PCIIOCfgArea : public IMemoryBank {
public:                                 // PCI cfg is allways little endian
                                        // We use endian::Big here, but the underlying
                                        // pci device allready does the converions for us
                                        // and Linux will swab32 back
    PCIIOCfgArea(u32 base, GRPCI2& bridge)
        : IMemoryBank(Endian::Big), base(base), size(128 * 1024), bridge_(bridge) {}

    bool contains(u32 addr) const override {
        return addr >= base && addr < base + size;
    }

    u8 read8(u32 addr) const override {
        
        u8 data;
        if(addr>=GRPCI2::Map::PCI_CONF_BASE && addr<(GRPCI2::Map::PCI_CONF_BASE + 0x10000)) {
            data = bridge_.config_read(addr, 8);
            return data;
        }
        else if(addr>=GRPCI2::Map::PCI_IO_BASE && addr<(GRPCI2::Map::PCI_IO_BASE + 0x10000)) {
            data = bridge_.io_read(addr, 8);
            return data;
        }

        throw std::runtime_error("PCI read8 out of bounds, addr=" + to_hex(addr));
    }

    u16 read16(u32 addr, bool align = true) const override {
        if (align && (addr & 1))
            throw std::runtime_error("Unaligned 16-bit read at 0x" + to_hex(addr));
        
        u16 data;
        if(addr>=GRPCI2::Map::PCI_CONF_BASE && addr<(GRPCI2::Map::PCI_CONF_BASE + 0x10000)) {
            data = bridge_.config_read(addr, 16);
            return data;
        }
        else if(addr>=GRPCI2::Map::PCI_IO_BASE && addr<(GRPCI2::Map::PCI_IO_BASE + 0x10000)) {
            data = bridge_.io_read(addr, 16);
            return data;
        }

        
        throw std::runtime_error("PCI read16 out of bounds, addr=" + to_hex(addr));
    }

    u32 read32(u32 addr, bool align = true) const override {
        if (align && (addr & 3))
            throw std::runtime_error("Unaligned 32-bit read at 0x" + to_hex(addr));
        
        u32 data;
        if(addr>=GRPCI2::Map::PCI_CONF_BASE && addr<(GRPCI2::Map::PCI_CONF_BASE + 0x10000)) {
            data = bridge_.config_read(addr, 32);
            return data;
        }
        else if(addr>=GRPCI2::Map::PCI_IO_BASE && addr<(GRPCI2::Map::PCI_IO_BASE + 0x10000)) {
            data = bridge_.io_read(addr, 32);
            return data;
        }

        throw std::runtime_error("PCI read32 out of bounds, addr=" + to_hex(addr));
    }


    void write8(u32 addr, u8 val) override {
        
        if(addr>=GRPCI2::Map::PCI_CONF_BASE && addr<(GRPCI2::Map::PCI_CONF_BASE + 0x10000)) {
            bridge_.config_write(addr, (u32)val, 8);
            return;
        }
        else if(addr>=GRPCI2::Map::PCI_IO_BASE && addr<(GRPCI2::Map::PCI_IO_BASE + 0x10000)) {
            bridge_.io_write(addr, (u32)val, 8);
            return;
        }
        
        throw std::runtime_error("PCI write8 out of bounds, addr=" + to_hex(addr));
    }

    void write16(u32 addr, u16 val, bool align = true) override {
        
        if (align && (addr & 1))
            throw std::runtime_error("Unaligned 16-bit write at 0x" + to_hex(addr));
        
        if(addr>=GRPCI2::Map::PCI_CONF_BASE && addr<(GRPCI2::Map::PCI_CONF_BASE + 0x10000)) {
            bridge_.config_write(addr, (u32)val, 16);
            return;
        }
        else if(addr>=GRPCI2::Map::PCI_IO_BASE && addr<(GRPCI2::Map::PCI_IO_BASE + 0x10000)) {
            bridge_.io_write(addr, (u32)val, 16);
            return;
        }
        
        throw std::runtime_error("PCI write16 out of bounds, addr=" + to_hex(addr));
    }

    void write32(u32 addr, u32 val, bool align = true) override {
        if (align && (addr & 3))
            throw std::runtime_error("Unaligned 32-bit write at 0x" + to_hex(addr));
        
        if(addr>=GRPCI2::Map::PCI_CONF_BASE && addr<(GRPCI2::Map::PCI_CONF_BASE + 0x10000)) {
            bridge_.config_write(addr, val, 32);
            return;
        }
        else if(addr>=GRPCI2::Map::PCI_IO_BASE && addr<(GRPCI2::Map::PCI_IO_BASE + 0x10000)) {
            bridge_.io_write(addr, val, 32);
            return;
        }
        
        throw std::runtime_error("PCI write32 out of bounds, addr=" + to_hex(addr));
    }

    u32 get_base() const override { return base; }
    u64 get_end_exclusive() const override { return (u64)base + size; }
    u32 get_size() const override { return size; }

    u32* get_ptr() override { throw std::runtime_error("Cannot use get_ptr on pci cfg/io.");}

private:
    u32 base;
    size_t size;
    //std::vector<u8> data;
    GRPCI2& bridge_;

    void check_range(u32 addr) const {
        if (!contains(addr))
            throw std::out_of_range("PCI_CFG access out of range");
    }

    static std::string to_hex(u32 val) {
        char buf[11];
        snprintf(buf, sizeof(buf), "%08X", val);
        return buf;
    }
};
