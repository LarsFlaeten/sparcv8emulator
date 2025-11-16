#include "GRPCI2.hpp"

// std
#include <iostream>
#include <bit>

#define MST 0x1
#define MST_BIT 30
#define IRQ_MODE 0x1
#define IRQ_MODE_BIT 24
#define DMA_ENABLE 0x1
#define DMA_ENABLE_BIT 28

GRPCI2::GRPCI2(IRQMP& irqmp) : irqmp_(irqmp)
{
    sts_cap_ = (MST << MST_BIT) | (IRQ_MODE << IRQ_MODE_BIT) | (DMA_ENABLE << DMA_ENABLE_BIT);

    dma_ctrl_ = 1u << 31;

}

GRPCI2::~GRPCI2()
{

}

u32 GRPCI2::read(u32 offset) const
{
    switch(offset) {
        case(0x0):
            //std::cout << "[GRPCI2] Returning CTRL reg: " + to_hex(ctrl_) << "\n";
            return ctrl_;
        case(0x04):
            //std::cout << "[GRPCI2] Returning STS_CAP reg: " + to_hex(sts_cap_) << "\n";
            return sts_cap_;
        case(0x0C):
            //std::cout << "[GRPCI2] Returning IOMAP reg: " + to_hex(io_map_) << "\n";
            return io_map_;
        case(0x10):
            std::cout << "[GRPCI2] Returning DMACTRL reg: " + to_hex(dma_ctrl_) << "\n";
            return dma_ctrl_;
        case(0x14):
            std::cout << "[GRPCI2] Returning DMABASE reg: " + to_hex(dma_base_) << "\n";
            return dma_base_;
        default:
            if(offset >= 0x40 && offset <= 0x7c) {
                std::cout << "[GRPCI2] Returning AHBM2PCI reg[" + to_hex((offset - 0x40) / 4) + "]: " + to_hex(ahbm2pci_[(offset - 0x40) / 4]) << "\n";
                return ahbm2pci_[(offset - 0x40) / 4];
            } else {
                break;
            }
    }
    std::cerr << "WARN: GRPCI2::read(" << std::hex << offset << std::dec << ") outside implemented.\n";
    
    return 0;
}

void GRPCI2::write(u32 offset, u32 value  ) 
{
    u32 tmp = 0;
    switch(offset) {
        case(0x0): // CTRL
            // Bit 28 and 15-12 are RESERVED and not writeable
            tmp = value & ~(0x1u<<28 | 0xfu<<12);

            if(tmp & (1u << 31)) {
                pci_reset();
                // This bit is not self clearing
            }

            if(tmp & (1u << 30)) {
                pci_master_reset();
                tmp = tmp & ~(1u << 30); // Self clearing
            }

            if(tmp & (1u << 29)) {
                pci_target_reset();
                tmp = tmp & ~(1u << 29); // Self clearing
            }

            ctrl_ = tmp;
            return;
        case(0x4): // STS_CAP
            sts_cap_ = write_sts_cap(sts_cap_, value);
            
            return;
        case(0xC): // IO Map
            io_map_ = value & 0xffff0000; // bits 15-0 are reserved and allways zero
            
            return;

        case(0x10): // DMACTRL
            dma_ctrl_ = write_dma_ctrl(dma_ctrl_, value);
            
            return;
        case(0x14): // DMABASE
            dma_base_ = value;
            
            return;
        default:
            if(offset >= 0x40 && offset <= 0x7c) {
                ahbm2pci_[(offset - 0x40) / 4] = value;
                return;
            } else {
                break;
            }
            
    }   

    //std::cout << "GRPCI2::write(" << std::hex << offset << "), val= " << value << std::dec << "\n";
    return;
}

void GRPCI2::pci_reset(){
    std::cout << "[GRPCI2]: pci_reset() called.\n";
}
    
void GRPCI2::pci_master_reset(){
    std::cout << "[GRPCI2]: pci_master_reset() called.\n";
}

void GRPCI2::pci_target_reset(){
    std::cout << "[GRPCI2]: pci_target_reset() called.\n";
}

void GRPCI2::enable_dma() {
    std::cout << "[GRPCI2]: enable_dma() called.\n";
}

void GRPCI2::disable_dma() {
    std::cout << "[GRPCI2]: disable_dma() called.\n";
    
}

// Plain overwrite of the reg. Only to be used by the PCI cfg area?
void GRPCI2::signal_pci_cfg_access_complete() const {
    sts_cap_ = sts_cap_ & ~(0x1u << 19); // clear bit 19 (CFGER)
    sts_cap_ = sts_cap_ | (0x1u << 20); // Set bit 20 (CFGDO)
    return;
}
    

u32 GRPCI2::write_sts_cap(u32 old, u32 val) {
    // Bit masks
    constexpr uint32_t MASK_R  =
        (0x7FFu << 21) |    // bits 31–21
        (0xFFFu << 0);      // bits 11–0

    constexpr uint32_t MASK_WC =
        (0x1FFu << 12);     // bits 20–12 (9 bits)

    
    // Handle WC bits: clear those that were 1 and are written as 1
    uint32_t wc_bits = old & MASK_WC;
    wc_bits &= ~(val & MASK_WC);

    // Combine preserved R bits and updated WC bits
    return (old & MASK_R) | wc_bits;
}

            
u32 GRPCI2::write_dma_ctrl(u32 old, u32 val) {
    u32 sg_mask;
    if((1u << 2) & val)
        disable_dma();

    if((1u << 0) & val)
        enable_dma();

    // remove the safe guarded bits:
    if(((val >> 31) & 1u) == 0) {
        // We dont allow writes to 1, 4:6
        sg_mask = ~(1u << 1 | 7u << 4);
        val = val & sg_mask;
    }

    constexpr uint32_t MASK_RW =
        (1u << 31) |        // bit 31
        (7u << 4) |         // bits 6-4
        (7u << 0);          // bits 2-0

    constexpr uint32_t MASK_R =
        (0x7FFu << 20) |    // bits 30-20
        (1u << 3);          // bit 3

    constexpr uint32_t MASK_WC =
        (0x1FFFu << 7);     // bits 19-7

    
    // --- Handle WC bits ---
    // Clear bits where (old=1 and write=1)
    uint32_t wc_bits = old & MASK_WC;                  // isolate wc bits
    uint32_t wc_write = val & MASK_WC;                 // what was written
    wc_bits &= ~wc_write;                              // clear bits that were written as 1

    // --- Handle RW bits ---
    uint32_t rw_bits = (val & MASK_RW);

    // --- Handle R bits ---
    uint32_t r_bits = (old & MASK_R);

    // --- Combine all ---
    return r_bits | rw_bits | wc_bits;
}

u32 GRPCI2::config_read(u32 paddr, u8 size){
    //std::cout << "GRPCI2::config_read, addr=" << std::hex << paddr << ", size=" << size << std::dec << "\n";
        
    u32 ofs   = paddr - Map::PCI_CONF_BASE;
    u32  devfn = (ofs >> 8) & 0xff;    // device+function
    u32  reg   = ofs & 0xfc;           // register offset within config space

    u32  slot  = devfn >> 3;
    
    // (bus number comes from GRPCI2 ctrl reg; for now we can assume bus 0)
    //u32  bus   = 0;

    // for now, we only have our device at slot 6
    if(slot!=6) {
        //u32  func  = devfn & 0x7;

        //std::cout << "PCI read config, off=" << std::hex << ofs << ", devfn=" << devfn << ", slot=" << slot << ", func=" << func << ", reg=" << reg << std::dec << "\n";
        //std::cout << "  -> returning empty (0xffffffff)\n";
        signal_pci_cfg_access_complete();   
        return 0xffffffffu;
    }

    //std::cout << "PCI read config, off=" << std::hex << ofs << ", devfn=" << devfn << ", slot=" << slot << ", func=" << func << ", reg=" << reg << std::dec << "\n";
        
    u32 data = 0;
    u32 tmp = 0;
    switch (size) {
        
        case 8:
        case 16: 
            // 1- and 2-byte sizes normally aren't used directly by GRPCI2;
            // it always reads 32 bits then shifts. We implement them
            // later if needed.
            throw("Config reads for 8 and 16 bits not implemented.");
        case 32: 
            // 1) get dword in *bus little-endian* from the device
            data = device_->config_read32(reg);

            // 2) emulate big-endian CPU load: bytes [b0,b1,b2,b3] -> 0xb0b1b2b3
            tmp =
              (data << 24)
            | ((data & 0x0000FF00u) << 8)
            | ((data & 0x00FF0000u) >> 8)
            | (data >> 24);
            data = tmp;
            break;
        default: 
            data = 0xFFFFFFFFu;
            break;
    }

    signal_pci_cfg_access_complete();
    //std::cout << "  -> returning data from our device (data = " << std::hex << data << ", size=" << size << std::dec << ")\n";   
    
    return data;
}

u32 GRPCI2::io_read(u32 paddr, u8 size) {

    auto offs = paddr - GRPCI2::Map::PCI_IO_BASE;

    if(offs>0x10000) {
        throw std::runtime_error("PCI io read, addr=" + to_hex(paddr) + ", sz=" + to_hex(size));  
    }

    switch (size) {
        case 8: {
            uint8_t v = device_->io_read8(offs);
            return v;  // 8-bit endian neutral
        }
        case 16: {
            uint16_t v = device_->io_read16(offs);
            return std::byteswap(v);  // swap to host order
        }
        case 32: {
            uint32_t v = device_->io_read32(offs);
            return std::byteswap(v);  // swap to host order
        }
        default:
            throw std::runtime_error("IO read: unsupported size " + std::to_string(size));
    }
    
}

void GRPCI2::config_write(u32 paddr, u32 val, u8 size){
    
    //std::cout << "GRPCI2::config_write, addr=" << std::hex << paddr << ", size=" << size << std::dec << "\n";
        
    u32 ofs   = paddr - Map::PCI_CONF_BASE;
    u32 devfn = (ofs >> 8) & 0xff;    // device+function
    
    u32 slot  = devfn >> 3;
    u32 reg   = ofs & 0xfc;           // register offset within config space

    // (bus number comes from GRPCI2 ctrl reg; for now we can assume bus 0)
    //u32 bus   = 0;
    
    // for now, we only have our device at slot 6
    if(slot!=6) {
        //u32 func  = devfn & 0x7;
        
        //std::cout << "PCI write config, off=" << std::hex << ofs << ", devfn=" << devfn << ", slot=" << slot << ", func=" << func << ", reg=" << reg << ", val=" << val << std::dec << "\n";
        //std::cout << "  -> ignoring\n";
        signal_pci_cfg_access_complete();   
        return;
    }

    //std::cout << "PCI write config, off=" << std::hex << ofs << ", devfn=" << devfn << ", slot=" << slot << ", func=" << func << ", reg=" << reg << ", val=" << val << std::dec << "\n";
    //std::cout << "  -> writing to device (size=" << size << ")\n";   
    u32 tmp = 0;
    switch (size) {
        
        case 8:
        case 16: 
            // 1- and 2-byte sizes normally aren't used directly by GRPCI2;
            // it always reads 32 bits then shifts. We implement them
            // later if needed
            throw("Config reads for 8 and 16 bits not implemented.");

        case 32:
            // 2) emulate big-endian CPU store: bytes [b0,b1,b2,b3] -> 0xb0b1b2b3
            tmp =
              (val << 24)
            | ((val & 0x0000FF00u) << 8)
            | ((val & 0x00FF0000u) >> 8)
            | (val >> 24);
            val = tmp;
            device_->config_write32(reg, val);
            break;
        default: 
            break;
    }
    signal_pci_cfg_access_complete();
    return;
        
}

void GRPCI2::io_write(u32 paddr, u32 value, u8 size){
    auto offs = paddr - GRPCI2::Map::PCI_IO_BASE;

    if(offs>0x10000) {
        throw std::runtime_error("PCI io write, addr=" + to_hex(paddr) + ", val=" + to_hex(value) + ", sz=" + to_hex(size));
    }

    switch (size) {
        case 8:
            device_->io_write8(offs, value & 0xFF);
            break;
        case 16:
            device_->io_write16(offs, std::byteswap<uint16_t>(value & 0xFFFF));
            break;
        case 32:
            device_->io_write32(offs, std::byteswap<uint32_t>(value));
            break;
        default:
            throw std::runtime_error("IO write: unsupported size " + std::to_string(size));
    }
    
    
}
