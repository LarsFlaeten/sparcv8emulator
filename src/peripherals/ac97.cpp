#include "ac97.hpp"

#include <bit>

void AC97Pci::init_pci_config(){
    write16(0x00,kVendorId); 
    write16(0x02,kDeviceId);
    config_[0x08]=0x01; 
    config_[0x09]=kProgIf; 
    config_[0x0A]=kClassSub; 
    config_[0x0B]=kClassBase; 
    config_[0x0E]=kHeaderType;
    // DO NOT advertise any PCI capabilities (avoids MMIO BARs)
    write16(0x06, read16(0x06) & ~kStatusCapsListBit);
    //write16(0x06,read16(0x06)|kStatusCapsListBit);
    
    //write32(BAR0,0x00000001); 
    //write32(BAR1,0x00000001);
    //write32(BAR0, 0x24000000);   // memory BAR
    //write32(BAR1, 0x24000100);   // memory BAR
    
    write32(0x2C, 0x0000000C); // stereo + 16-bit
    write32(0x30, 0x00000000); // No expansion ROM
    config_[0x3D]=0x01; 
    config_[0x3C]=0xFF;
    config_[kCapPtrOffset]=0x50;
    
    // Initialize GRPCI2 capability at 0x50
    init_grpci2_cap(0x50, 0x00);

    
    init_codec();
}

void AC97Pci::init_grpci2_cap(uint8_t off, uint8_t next) {
    /*
    // Vendor-specific GRPCI2 capability
    config_[off + 0x00] = 0x80; // capability ID (custom)
    config_[off + 0x01] = next; // next pointer = none

    // PCI->AMBA mapping (optional mock values)
    write32(off + 0x04, 0xFA000000); // I/O mapping
    write32(off + 0x08, 0x24000000); // MEM mapping

    // Endianness config (the GRPCI2 endian register)
    // bit 0 = 1 => little endian I/O
    // bit 1 = 1 => little endian MEM
    write32(off + 0x20, 0x00000001); // I/O little-endian, MEM big-endian
    */
}

void AC97Pci::init_codec() {
    for(u_int8_t i = 0; i <64; ++i)
        codec_regs_[i] = 0x0U;

    // Codec Vendor ID (Analog Devices AD1881A)
    codec_regs_[0x7C >> 1] = 0x4144;  // 'AD'
    codec_regs_[0x7E >> 1] = 0x5348; // "AD1881A"

    codec_regs_[0x26 >> 1] = 0x80F0;  // many real codecs report this

    // Extended Audio ID / Control
    //codec_regs_[0x28 >> 1] = 0x0039; // VRA + VRM + PCM DAC + PCM ADC
    //codec_regs_[0x2A >> 1] = 0x0001;
    codec_regs_[0x28 >> 1] = 0x0001;   // Extended Audio ID = PCM only
    codec_regs_[0x2A >> 1] = 0x0007;   // Extended Audio Status:
                                   // bit0 = VRA supported? no (0)
                                   // bit1 = VRM? no (0)
                                   // bit2 = PCM DAC supported (1)
                                   // bit3 = PCM ADC supported (1)
                                   // bit2|bit3|bit0 maybe -> 0x0007
    // Rates
    codec_regs_[0x2C >> 1] = 0xBB80; // PCM Front DAC Rate  = 48000
    codec_regs_[0x32 >> 1] = 0xBB80; // PCM ADC Rate        = 48000
    codec_regs_[0x30 >> 1] = 0xBB80; // LFE DAC Rate        = 48000
    codec_regs_[0x2E >> 1] = 0xBB80; // Surround DAC Rate   = 48000
    

    // Master volume (0dB)
    codec_regs_[0x02 >> 1] = 0x0000;
    codec_regs_[0x04 >> 1] = 0x0000;

    // Mixer defaults
    codec_regs_[0x18 >> 1] = 0x0808;  // PCM out volume
    codec_regs_[0x1A >> 1] = 0x0808;  // Line out

    //glob_sta_ |= GS_CRDY_CODEC0;
    //glob_sta_ &= ~GS_S0R;              // start with semaphore = 0 (busy)
    //semaphore_state_ = false;
    //semaphore_pulse_pending_ = false;

    glob_sta_ |= GS_CRDY_CODEC0;   // Codec is present & ready
    glob_sta_ |= GS_S0R;           // No pending codec command (idle)
}


void AC97Pci::tick() {

    static uint64_t tick_count = 0;
    static uint64_t dma_tick_counter = 0;
    tick_count++;
    //if ((tick_count % 1000) == 0)
    //    printf("[AC97] ticked 1000 times (CIV=%02x)\n", po_civ_);

    if (!(running_)) return; 

    

    if (++dma_tick_counter < dma_ticks_per_buffer_)
        return;

    
    dma_tick_counter = 0;

    // Locate current buffer descriptor
    uint32_t bd_addr = bdbar_playback_ + (po_civ_ * 8);
    
    uint32_t buf_paddr = 0;
    uint16_t buf_len = 0;
    uint16_t control   = 0;
    if(!mem_read_(bd_addr + 0, &buf_paddr, 4)) return;    
    if(!mem_read_(bd_addr + 4, &buf_len, 2)) return;
    if(!mem_read_(bd_addr + 6, &control, 2)) return;
    printf("[AC97 DMA BD] po_civ=%02d: addr=%08x len=%04x ctl=%04x\n",
       po_civ_, buf_paddr, buf_len, control);
    

    // Read samples from guest memory
    std::vector<int16_t> samples(buf_len / 2);
    // Update PICB so ALSA sees progress and correct period size
    po_picb_ = buf_len / 2;

    for (size_t i = 0; i < samples.size(); ++i) {
        uint16_t buf;
        mem_read_(buf_paddr + i * 2, &buf, 2);
        samples[i] = static_cast<int16_t>(buf);
    }

    // Push to host
    host_audio_->pushSamples(samples);

    // Mark buffer complete
    po_civ_ = (po_civ_ + 1) & 0x1F;

    // Interrupt if IOC flag set in BD entry
    if (control & 0x8000)
        po_status_ |= 0x0008; // BCIS

    // If CIV reached LVI+1, wrap to 0
    if (po_civ_ == ((po_lvi_ + 1) & 0x1F)) {
        po_civ_ = 0;
        po_status_ |= 0x4000; // maybe set INT flag when wrapping
        if ((po_control_ & 0x10) && raise_intx_)
            raise_intx_();
    }

/*
    // One buffer "completed" -> advance CIV, raise IRQ, etc.
    po_civ_ = (po_civ_ + 1) & 0x1F;

    if (po_civ_ == po_lvi_) {
        po_status_ |= 0x4008; // INT + BCIS
        if ((po_control_ & 0x10) && raise_intx_)
            raise_intx_();
        po_civ_ = 0;
    }
*/
    // here you can also fetch the next BD and push samples to host_audio_
    
}

uint32_t AC97Pci::io_read32(uint32_t addr) {
    uint32_t ret;
    if (addr < nam_base_ || addr >= nam_base_+0x100)
        printf("NOT NAM read: %08x\n", addr);

    if (addr < nabm_base_ || addr >= nabm_base_+0x100)
        printf("NOT NABM read: %08x\n", addr);

    if ((addr >= nam_base_) && (addr < (nam_base_ + 0x100))) {
        uint32_t of = addr - nam_base_;
        uint32_t val = read_nam(of & 0xFF);
        ret = 0xFFFF0000u | val;     // REQUIRED BY AC'97
    } else if ((addr >= nabm_base_) && (addr < (nabm_base_ + 0x100))) {
        uint32_t of = addr - nabm_base_;
        ret = read_nabm(of & 0xFF, 4);
    } else {
        ret = 0xFFFFFFFF;
    }

    std::cout << "[AC97] io_read32 addr=0x" << to_hex(addr) << " -> " << to_hex(ret) << "\n";
    
    //throw std::runtime_error("AC97: invalid io_read32 port: 0x" + to_hex(port));
    return ret;
}


uint16_t AC97Pci::io_read16(uint32_t addr) {
    uint16_t ret;
    if ((addr >= nam_base_) && (addr < (nam_base_ + 0x100))) {
        uint32_t of = addr - nam_base_;
        ret = read_nam(of & 0xFF);
    } else if ((addr >= nabm_base_) && (addr < (nabm_base_ + 0x100))) {
        uint32_t of = addr - nabm_base_;
        ret = read_nabm(of & 0xFF, 2);
    } else {
        ret = 0xFFFF;
    }

    std::cout << "[AC97] io_read16 addr=0x" << to_hex(addr) << " -> " << to_hex(ret) << "\n";
    
    //throw std::runtime_error("AC97: invalid io_read16 port: 0x" + to_hex(port));
    return ret;
    
}

uint8_t AC97Pci::io_read8(uint32_t addr) {
    uint8_t ret;
    /*
    if (port >= nam_base_ && port < nam_base_ + 0x100) {
        uint32_t of = port - nam_base_;
        return read_nam(of & 0xFF);
    }
    */

    if ((addr >= nabm_base_) && (addr < (nabm_base_ + 0x100))) {
        uint32_t of = addr - nabm_base_;
        ret = read_nabm(of & 0xFF, 1);
    } else {
        ret = 0xFF;
    }

    std::cout << "[AC97] io_read8 addr=0x" << to_hex(addr) << " -> " << to_hex(ret) << "\n";
    //throw std::runtime_error("AC97: invalid io_read8 port: 0x" + to_hex(port));
    return ret;
}

void AC97Pci::io_write32(uint32_t addr, uint32_t v) {
    std::cout << "[AC97] io_write32 addr=0x" << to_hex(addr) << " <- " << to_hex(v) << "\n";
    
    if ((addr >= nam_base_) && (addr < (nam_base_ + 0x100))) {
        uint32_t of = addr - nam_base_;
        write_nam(of & 0xFF, v);
        return;
    }

    if ((addr >= nabm_base_) && (addr < (nabm_base_ + 0x100))) {
        uint32_t of = addr - nabm_base_;
        write_nabm(of & 0xFF, v, 4);
        return;
    }

    // Silently ignore writes outside the BARS
    std::cout << "[AC97] io_write32 addr=0x" << to_hex(addr) << ", val=" << to_hex(v) << " (ignored)\n";
    
    //throw std::runtime_error("AC97: invalid io_write32 port: 0x" + to_hex(addr) + ", v=0x" + to_hex(v));
    
}

void AC97Pci::io_write16(uint32_t addr, uint16_t v) {
    std::cout << "[AC97] io_write16 addr=0x" << to_hex(addr) << " <- " << to_hex(v) << "\n";
    
    if ((addr >= nam_base_) && (addr < (nam_base_ + 0x100))) {
        uint32_t of = addr - nam_base_;
        write_nam(of & 0xFF, v);
        return;
    }

    if ((addr >= nabm_base_) && (addr < (nabm_base_ + 0x100))) {
        uint32_t of = addr - nabm_base_;
        write_nabm(of & 0xFF, v, 2);
        return;
    }

    // Silently ignore writes outside the BARS
    std::cout << "[AC97] io_write16 addr=0x" << to_hex(addr) << ", val=" << to_hex(v) << " (ignored)\n";
    
    //throw std::runtime_error("AC97: invalid io_write32 port: 0x" + to_hex(port) + ", v=0x" + to_hex(v));
}

void AC97Pci::io_write8(uint32_t addr, uint8_t v) {
    std::cout << "[AC97] io_write8 addr=0x" << to_hex(addr) << " <- " << to_hex(v) << "\n";
    
    /*   
    if (port >= nam_base_ && port < nam_base_ + 0x100) {
        uint32_t of = port - nam_base_;
        write_nam(of & 0xFF, v);
        return;
    }
    */
    if ((addr >= nabm_base_) && (addr < (nabm_base_ + 0x100))) {
        uint32_t of = addr - nabm_base_;
        write_nabm(of & 0xFF, v, 2);
        return;
    }

    // Silently ignore writes outside the BARS
    std::cout << "[AC97] io_write8 addr=0x" << to_hex(addr) << ", val=" << to_hex(v) << " (ignored)\n";
    
    //throw std::runtime_error("AC97: invalid io_write32 port: 0x" + to_hex(port) + ", v=0x" + to_hex(v));

}


    
//----------------------------------------------------------------------
// NAM (Native Audio Mixer) accessors
//----------------------------------------------------------------------


void AC97Pci::codec_command_begin()
{
    glob_sta_ &= ~GS_S0R;
}

void AC97Pci::codec_command_complete()
{
    // nothing special; the read handler will lift the bit
    glob_sta_ |= GS_S0R;                  // make sure internal copy high
}

uint16_t AC97Pci::read_nam(uint32_t offset)
{
    
    if (offset >= 0x80)
        return 0xFFFF;
    
    codec_command_begin();

    uint16_t val = 0;
    switch (offset)
    {
        case 0x26: // Power-status
            val = power_status_;
            break;
        
        default:
            val = (codec_regs_[offset >> 1]);
            break;
    }
    

    printf("[AC97 NAM]  read  @%02x -> %04x\n", offset, val);
    
    codec_command_complete();
    return val;
}

void AC97Pci::write_nam(uint32_t offset, uint16_t value)
{
    if (offset >= 0x80)
        return;

    printf("[AC97 NAM]  write @%02x <- %04x\n", offset, value);

    // take semaphore:
    codec_command_begin();

    // Typical reactions
    switch (offset) {
        case 0x00: // Reset
            glob_sta_ |= GS_CRDY_CODEC0;// codec ready again
            
            init_codec();
            break;
        case 0x26: // Powerdown control/status
            power_ctrl_ = value;
            // if all bits clear -> start power-up sequence
            if (value == 0x0000) {
                // instant-ready for now
                power_status_ = 0x000F;  // all analog sections ready
            } else {
                // some bits set -> those blocks off
                power_status_ = (~value) & 0x000F;
            }

            
            break;
        default:
            codec_regs_[offset >> 1] = (value);
            break;
    }

    // command complete → release semaphore: ready = 1
    codec_command_complete();
}

//----------------------------------------------------------------------
// NABM (Native Audio Bus Master) accessors
//----------------------------------------------------------------------
uint32_t AC97Pci::read_glob_sta()
{
    constexpr uint32_t allowed_mask =
        (1u << 8) |   // CRDY
        0x0F;         // DMA req + semaphore bits

    // Mask off irrelevant bits
    uint32_t v = glob_sta_ & allowed_mask;

    // If S0R is set, reading GLOB_STA *auto-clears* it
    if (v & GS_S0R) {
        glob_sta_ &= ~GS_S0R;
    }

    return v;
}

uint32_t AC97Pci::read_nabm(uint32_t offset, uint8_t width)
{
    uint32_t val = 0;
    if(width == 1) {
        switch (offset) {
            // --- PO_SR (16-bit status register, readable as 8-bit low/high) ---
            case BMOff::PO_BASE + BMOff::SR:
            case BMOff::PO_BASE + BMOff::SR + 1:
                val = (po_status_ >> ((offset & 1) * 8)) & 0xFF;
                break;

            // --- PI_SR ---
            case BMOff::PI_BASE + BMOff::SR:
            case BMOff::PI_BASE + BMOff::SR + 1:
                val = (pi_status_ >> ((offset & 1) * 8)) & 0xFF;
                break;

            // --- MC_SR ---
            case BMOff::MC_BASE + BMOff::SR:
            case BMOff::MC_BASE + BMOff::SR + 1:
                val = (mc_status_ >> ((offset & 1) * 8)) & 0xFF;
                break;

            case (BMOff::PI_BASE + BMOff::CR):
                val = pi_control_;
                break;
            case BMOff::PO_BASE + BMOff::CR:
                val = po_control_;
                break;
            case BMOff::MC_BASE + BMOff::CR:
                val = mc_control_;
                break;
            case BMOff::PI_BASE + BMOff::CIV:
                val = pi_civ_;
                break;
            case BMOff::PO_BASE + BMOff::CIV:
                // Simulate CIV advance..
                // TO BE REMOVED!!!!!!!!!
                //po_civ_ = (po_civ_ + 1) & 0x1F;
                // po_civ increment is moved to tick()

                val = po_civ_;
                break;
            case BMOff::MC_BASE + BMOff::CIV:
                val = mc_civ_;
                break;
            case BMOff::PI_BASE + BMOff::LVI:
                val = pi_lvi_;
                break;
            case BMOff::PO_BASE + BMOff::LVI:
                val = po_lvi_;
                break;
            case BMOff::MC_BASE + BMOff::LVI:
                val = mc_lvi_;
                break;
            case BMOff::PO_BASE + BMOff::PICB:
            case BMOff::PO_BASE + BMOff::PICB + 1:
                val = (po_picb_ >> ((offset & 1) * 8)) & 0xFF;
                break;
            case BMOff::CAS:
                val = 0U; // CAS not implemented now
                // TODO: IMplement sempahore here when doing NAM access
                break;
            default:
                throw("[AC97 NABM] read8 not valid for register " + to_hex(offset));
        }
        printf("[AC97 NABM] read8  @%02x -> %02x\n", offset, val);
    
        return val;
    } else if(width==2) {
        switch (offset) {
            case BMOff::PI_BASE + BMOff::SR:
                val = pi_status_;
                break;
            case BMOff::PO_BASE + BMOff::SR:
                val = po_status_;
                break;
            case BMOff::MC_BASE + BMOff::SR:
                val = mc_status_;
                break;
            case BMOff::PO_BASE + BMOff::PICB:
                val = po_picb_;
                break;
            default:
                throw("[AC97 NABM] read16 not valid for register " + to_hex(offset));
            
        }
        printf("[AC97 NABM] read16  @%02x -> %02x\n", offset, val);
    
        return val;
    } else { // width = 4
        switch (offset) {
            case BMOff::PO_BASE + BMOff::BD_BAR:
                val = bdbar_playback_;
                break;
            case BMOff::PI_BASE + BMOff::BD_BAR:
                val = bdbar_capture_;
                break;
            case BMOff::MC_BASE + BMOff::BD_BAR:
                val = 0;
                break;
            case BMOff::GLOB_CNT:
                val = glob_cnt_;
                break;
            case BMOff::GLOB_STA:
                val = read_glob_sta();
                //val = glob_sta_;
                break;
            default:
                throw("[AC97 NABM] read32 not valid for register " + to_hex(offset));
        
        }
        
        printf("[AC97 NABM] read32  @%02x -> %08x\n", offset, val);
        return val;
    }

    throw("[AC97 NABM] read not valid for register " + to_hex(offset));
}

void AC97Pci::write_nabm(uint32_t offset, uint32_t value, uint8_t width)
{
    
    if(width == 1) {
        printf("[AC97 NABM] write8 @%02x <- %02x\n", offset, value);
        value = value & 0xFFU;
        switch (offset) {

            case BMOff::PO_BASE + BMOff::SR: {
                // Low byte write — handle W1C for bits 4..2
                po_status_ = value & 0x1Fu;
                uint8_t clear_mask = value & 0x1C;     // bits 4–2
                po_status_ &= ~clear_mask;             // clear only those bits
                break;
            }

            case BMOff::PO_BASE + BMOff::SR + 1: 
                break; // Reserved, RO
            

            case BMOff::PI_BASE + BMOff::SR: {
                // Low byte write — handle W1C for bits 4..2
                pi_status_ = value & 0x1Fu;
                uint8_t clear_mask = value & 0x1C;     // bits 4–2
                pi_status_ &= ~clear_mask;             // clear only those bits
                break;
            }
            case BMOff::PI_BASE + BMOff::SR + 1:
                break;   
            

            case BMOff::MC_BASE + BMOff::SR:  {
                // Low byte write — handle W1C for bits 4..2
                mc_status_ = value & 0x1Fu;
                uint8_t clear_mask = value & 0x1C;     // bits 4–2
                mc_status_ &= ~clear_mask;             // clear only those bits
                break;
            }
            case BMOff::MC_BASE + BMOff::SR + 1: 
                break;


            case BMOff::PI_BASE + BMOff::CR:  // PI_CONTROL
                pi_control_ = value & 0x1F;
                if (pi_control_ & 0x02) {
                    // Reset requested
                    pi_control_ &= ~0x02;
                }
                break;
            case BMOff::PO_BASE + BMOff::CR: {
                uint8_t old = po_control_;
                po_control_ = value & 0x1F;

                // Handle reset bit
                if (po_control_ & 0x02) {
                    if (old & 0x01)
                        throw std::runtime_error("Reset called while RUN=1.");
                    po_control_ &= ~0x02; // auto-clear reset bit

                    running_ = false;
                    po_civ_ = 0;
                    po_status_ = 0x0001; // DCH=1
                    po_picb_ = 0;
                    break;
                }

                bool old_run = old & 0x01;
                bool new_run = po_control_ & 0x01;

                if (new_run && !old_run) {
                    // RUN just transitioned 0 → 1

                    set_run(true);

                    // --- IMPORTANT: initialize PICB from BD ---
                    uint32_t bd_addr = bdbar_playback_ + (po_civ_ * 8);

                    uint16_t buf_len = 0;
                    mem_read_(bd_addr + 4, &buf_len, 2);

                    // PICB = #samples remaining
                    po_picb_ = buf_len / 2;
                }
                else if (!new_run && old_run) {
                    set_run(false);
                }

                break;
            }
            case BMOff::MC_BASE + BMOff::CR:  // MC_CONTROL (also 8-bit)
                mc_control_ = value & 0x1F;
                if(mc_control_ & 0x02) {
                    mc_control_ &= ~0x02; // immediately clear reset bit
                }
                break;

            case BMOff::PO_BASE + BMOff::CIV:
            case BMOff::PI_BASE + BMOff::CIV:
            case BMOff::MC_BASE + BMOff::CIV:
                // Read-only in spec; Linux may write 0 once after reset
                if (value != 0)
                    printf("[AC97] Unexpected write to CIV = %02x (ignored)\n", value);
                break;
            case BMOff::PI_BASE + BMOff::LVI:
                pi_lvi_ = value & 0x1F;
                break;
            case BMOff::PO_BASE + BMOff::LVI:
                po_lvi_ = value & 0x1F;
                printf("[AC97 DMA Setup] BD BAR=%08x LVI=%02x\n", bdbar_playback_, po_lvi_);
                break;
            case BMOff::MC_BASE + BMOff::LVI:
                mc_lvi_ = value & 0x1F;
                break;
            default:
                throw("[AC97 NABM] Write not valid for register " + to_hex(offset));

        }
        return;
    } else if(width == 2) {
        value = value & 0xFFFFu;
        printf("[AC97 NABM] write16 @%02x <- %08x\n", offset, value);

        switch (offset) {
            case BMOff::PI_BASE + BMOff::SR: {
                pi_status_ = value & 0x1F;  // only lower bits valid
                uint8_t clear_mask = value & 0x1C; // bits 4–2 W1C
                pi_status_ &= ~clear_mask;
                break;
            }
            case BMOff::PO_BASE + BMOff::SR: {
                po_status_ = value & 0x1F;
                uint8_t clear_mask = value & 0x1C;
                po_status_ &= ~clear_mask;
                break;
            }
            case BMOff::MC_BASE + BMOff::SR: {
                mc_status_ = value & 0x1F;
                uint8_t clear_mask = value & 0x1C;
                mc_status_ &= ~clear_mask;
                break;
            }

            // ----- CONTROL REGISTERS (16-bit writes, low byte is CR) -----
            case BMOff::PI_BASE + BMOff::CR: { // 0x0B
                uint8_t v = static_cast<uint8_t>(value & 0x1F);
                pi_control_ = v;
                if (pi_control_ & 0x02) {
                    // reset capture DMA
                    pi_control_ &= ~0x02;
                    pi_status_ = 0x0001; // DCH=1 (stopped)
                    pi_civ_ = 0;
                }
                // run bit (bit0) is not used much for capture in your current code,
                // but we could add set_run_capture() later if needed.
                break;
            }

            case BMOff::PO_BASE + BMOff::CR: { // 0x1B
                uint8_t old = po_control_;
                uint8_t v   = static_cast<uint8_t>(value & 0x1F);
                po_control_ = v;

                if (po_control_ & 0x02) {
                    if (old & 0x01)
                        throw std::runtime_error("Reset called when running bit is 1.");
                    // Reset requested
                    po_control_ &= ~0x02;

                    running_   = false;
                    po_civ_    = 0;
                    po_status_ = 0x0001; // DCH=1
                    po_picb_   = 0;
                }

                bool old_run = old & 0x01;
                bool new_run = v   & 0x01;
                if (new_run != old_run)
                    set_run(new_run);

                break;
            }

            case BMOff::MC_BASE + BMOff::CR: { // 0x2B
                uint8_t v = static_cast<uint8_t>(value & 0x1F);
                mc_control_ = v;
                if (mc_control_ & 0x02) {
                    mc_control_ &= ~0x02;
                    mc_status_ = 0x0001; // DCH=1
                    mc_civ_ = 0;
                }
                break;
            }

            // ----- PICB (playback PICB at least is used) -----
            case BMOff::PO_BASE + BMOff::PICB: { // 0x18
                po_picb_ = static_cast<uint16_t>(value);
                break;
            }

            default:
                throw("[AC97 NABM] Write16 not valid for register " + to_hex(offset));

        }
        return;

    } else {

        printf("[AC97 NABM] write32 @%02x <- %08x\n", offset, value);

        switch (offset) {
            case BMOff::PO_BASE + BMOff::BD_BAR:
                bdbar_playback_ = value;
                printf("[AC97 DMA Setup] BD BAR=%08x LVI=%02x\n", bdbar_playback_, po_lvi_);
                break;
            case BMOff::PI_BASE + BMOff::BD_BAR:
                bdbar_capture_ = value;
                break;
            case BMOff::MC_BASE + BMOff::BD_BAR:
                // MC not implemented
                break;
            case BMOff::GLOB_CNT: {
                uint32_t old = glob_cnt_;
                glob_cnt_ = value;

                bool old_cold = old & 0x02;
                bool new_cold = value & 0x02;

                bool old_warm = old & 0x02000000;
                bool new_warm = value & 0x02000000;

                // --- Cold reset: bit1 rises ---
                if (!old_cold && new_cold) {
                    printf("[AC97] Cold reset triggered\n");
                    cold_reset();
                }

                // --- Warm reset: bit25 rises ---
                if (!old_warm && new_warm) {
                    std::cout << "[AC97] Warm reset triggered\n";

                    warm_reset();

                    
                }
                break;
            }
            case BMOff::GLOB_STA: {
                // Only bits 4,5,6,7,15,23.. etc are W1C on real hardware
                // For now: allow clearing ONLY GS_S0R
                if (value & GS_S0R)
                    glob_sta_ &= ~GS_S0R;

                break;
            }
            default:
                throw("[AC97 NABM] Read16 not valid for register " + to_hex(offset));

        }
        return;    
    }

    throw("[AC97 NABM] Read not valid for register " + to_hex(offset));
}

void AC97Pci::cold_reset() {
    
    memset(codec_regs_, 0, sizeof(codec_regs_));
    bdbar_playback_ = 0;
    bdbar_capture_  = 0;

    glob_sta_ = GS_CRDY_CODEC0; // bit 8 set
}

void AC97Pci::warm_reset()
{
    printf("[AC97] Warm reset triggered\n");

    // 1. Restore analog subsections ready
    codec_regs_[0x26 >> 1] = 0x000F;

    // 2. Extended Audio Status → bits 0–3 are writable
    codec_regs_[0x2A >> 1] &= 0x000F;

    // 3. Codec ready (bit 8)
    glob_sta_ |= GS_CRDY_CODEC0;

    // 4. Clear warm-reset trigger bit (GLOB_CNT bit 16)
    glob_cnt_ &= ~(1u << 16);
}




    