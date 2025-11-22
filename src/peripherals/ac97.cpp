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

    po_status_  = 0x0000;   // RUN=0, BCIS=0, LVBCI=0
    TRACE_PO_SR_CHANGE();
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

    codec_regs_[0x28 >> 1] = 0x0101;   // Extended Audio Status:
                                   // bit0 = VRA supported
                                   // bit8 = Extended ID present

    codec_regs_[0x2A >> 1] = 0x0001;   // Extended Audio Control:
                                    // VRA enabled, nothing else

    codec_regs_[0x2C >> 1] = 48000;    // Front DAC Rate = 48kHz
    codec_regs_[0x32 >> 1] = 48000;    // ADC Rate = 48kHz
    

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

    power_status_ = 0x000F;                  // all analog subsections ready
}


void AC97Pci::tick()
{
    // ---- CRDY delayed ready handling ----
    if (reset_delay_counter_ > 0) {
        reset_delay_counter_--;
        if (reset_delay_counter_ == 0) {
            glob_sta_ |= (1 << 8);      // CRDY = ready
            printf("[AC97] CRDY READY after delay\n");
        }
    }

    // 0. DMA not running?
    if (!po_running_)
        return;

    // 1. BD BAR not programmed? Do not run DMA.
    if (bdbar_playback_ == 0)
        return;

    // 2. No valid BD loaded yet? Do not run DMA.
    if (po_cur_len_ == 0)
        return;

    // 3. No frames to consume? Do not run DMA.
    if (po_picb_ == 0)
        return;
        
    
    //
    // 2. Consume a limited number of frames this tick
    //
    // Tune this value to get smooth audio. 32 or 48 is usually good.
    const uint32_t FRAMES_PER_TICK = 1;

    uint32_t todo = std::min<uint32_t>(po_picb_, FRAMES_PER_TICK);

    //
    // 3. Fetch 'todo' stereo frames from guest memory
    //
    std::vector<int16_t> samples(todo * 2);   // stereo: 2 samples per frame

    uint32_t sample_ptr = po_cur_ptr_ + po_cur_bd_frame_offset_bytes_;

    for (uint32_t i = 0; i < todo * 2; ++i) {
        uint16_t v;
        mem_read_(sample_ptr + i * 2, &v, 2);
        samples[i] = (int16_t)v;
    }

    // Push audio to host
    if (host_audio_)
        host_audio_->pushSamples(samples);

    //
    // 4. Advance pointers
    //
    po_cur_bd_frame_offset_bytes_ += todo * 4;  // 4 bytes per stereo frame
    po_picb_ -= todo;

    //
    // 5. If buffer is not finished, return
    //
    if (po_picb_ > 0)
        return;

    //
    // 6. Buffer is COMPLETED
    //
    printf("[AC97 TICK] CIV=%u LVI=%u PICB=%u LEN=%u OFFS=%u CTL=%04x RUN=%u BD_PTR=%08x\n",
        po_civ_, po_lvi_, po_picb_, po_cur_len_, po_cur_bd_frame_offset_bytes_,
        po_cur_ctl_, po_running_, po_cur_ptr_);

    // CIV always increments modulo 32
    uint8_t old_civ = po_civ_;
    //po_civ_ = (old_civ + 1) & 0x1F;

    // Default increment (mod 32)
    uint8_t next_civ = (old_civ + 1) & 0x1F;

    // Wrap ring: if we passed LVI, restart at BD0
    if (next_civ > po_lvi_)
        next_civ = 0;

    po_civ_ = next_civ;

    // BCIS (buffer completion)
    if (po_cur_ctl_ & 0x8000) {
        po_status_ |= 0x08;
        TRACE_PO_SR_CHANGE();
    }

    // LVBCI: CIV == (LVI+1)
    uint8_t next_after_lvi = (po_lvi_ + 1) & 0x1F;
    if (po_civ_ == next_after_lvi) {
        po_status_ |= 0x04;
        TRACE_PO_SR_CHANGE();
    }

    // Raise interrupt if enabled
    if ((po_status_ & 0x0C) && (po_control_ & 0x10)) {
        if (raise_intx_) raise_intx_();
    }

    //
    // 7. Load the next BD for playback
    //
    uint32_t bd_addr = bdbar_playback_ + (po_civ_ * 8);

    uint32_t ptr  = mem_read32(bd_addr + 0);
    uint16_t len  = mem_read16(bd_addr + 4);
    uint16_t ctl  = mem_read16(bd_addr + 6);
    printf("[AC97 BD LOAD from tick] %08x: ptr=%08x len=%08x ctl=%08x",
        bd_addr, ptr, len, ctl);
    po_cur_ptr_ = ptr;
    po_cur_len_ = (uint32_t)len + 1;
    po_cur_ctl_ = ctl;

    po_cur_bd_frame_offset_bytes_ = 0;
    
    if (po_cur_len_ == 1) {
        // Empty BD; do not complete, do not fire interrupts,
        // do not advance CIV beyond this. Just idle.
        po_picb_ = 0;
        return;
    }

    po_picb_ = po_cur_len_ / 4; // frames
}


uint32_t AC97Pci::io_read32(uint32_t addr) {
    uint32_t ret;

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

    //std::cout << "[AC97] io_read32 addr=0x" << to_hex(addr) << " -> " << to_hex(ret) << "\n";
    
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

    //std::cout << "[AC97] io_read16 addr=0x" << to_hex(addr) << " -> " << to_hex(ret) << "\n";
    
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

    //std::cout << "[AC97] io_read8 addr=0x" << to_hex(addr) << " -> " << to_hex(ret) << "\n";
    //throw std::runtime_error("AC97: invalid io_read8 port: 0x" + to_hex(port));
    return ret;
}

void AC97Pci::io_write32(uint32_t addr, uint32_t v) {
    //std::cout << "[AC97] io_write32 addr=0x" << to_hex(addr) << " <- " << to_hex(v) << "\n";
    
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
    //std::cout << "[AC97] io_write32 addr=0x" << to_hex(addr) << ", val=" << to_hex(v) << " (ignored)\n";
    
    //throw std::runtime_error("AC97: invalid io_write32 port: 0x" + to_hex(addr) + ", v=0x" + to_hex(v));
    
}

void AC97Pci::io_write16(uint32_t addr, uint16_t v) {
    //std::cout << "[AC97] io_write16 addr=0x" << to_hex(addr) << " <- " << to_hex(v) << "\n";
    
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
    //std::cout << "[AC97] io_write16 addr=0x" << to_hex(addr) << ", val=" << to_hex(v) << " (ignored)\n";
    
    //throw std::runtime_error("AC97: invalid io_write32 port: 0x" + to_hex(port) + ", v=0x" + to_hex(v));
}

void AC97Pci::io_write8(uint32_t addr, uint8_t v) {
    //std::cout << "[AC97] io_write8 addr=0x" << to_hex(addr) << " <- " << to_hex(v) << "\n";
    
    /*   
    if (port >= nam_base_ && port < nam_base_ + 0x100) {
        uint32_t of = port - nam_base_;
        write_nam(of & 0xFF, v);
        return;
    }
    */
    if ((addr >= nabm_base_) && (addr < (nabm_base_ + 0x100))) {
        uint32_t of = addr - nabm_base_;
        write_nabm(of & 0xFF, v, 1);
        return;
    }

    // Silently ignore writes outside the BARS
    //std::cout << "[AC97] io_write8 addr=0x" << to_hex(addr) << ", val=" << to_hex(v) << " (ignored)\n";
    
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
    // Mask to only allow documented readable bits
    constexpr uint32_t allowed =
        (1u << 0)  |   // PCR
        (1u << 5)  |   // PCM in active
        (1u << 6)  |   // PCM out active
        (1u << 7)  |   // Mic ADC active
        (1u << 8)  |   // CRDY (codec ready)
        (1u << 15);    // S0R (sticky)

    uint32_t v = glob_sta_ & allowed;

    // S0R clears on read
    if (v & (1u << 15))
        glob_sta_ &= ~(1u << 15);

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
            {
                uint16_t s = po_status_;
                // Return correct 8-bit half
                val = (s >> ((offset & 1) * 8)) & 0xFF;

                // AC'97 semantics: BCIS is sticky until W1C
                // BUT do NOT auto-clear on read — Linux checks repeatedly
                break;
            }

            // --- PI_SR ---
            case BMOff::PI_BASE + BMOff::SR:
            case BMOff::PI_BASE + BMOff::SR + 1:
            {
                uint16_t masked = pi_status_ & 0x1F;   // keep only legal bits 0..4

                if ((offset & 1) == 0)
                    val = masked & 0xFF;               // low byte
                else
                    val = 0x00;                        // high byte always 0

                break;
            }

            // --- MC_SR ---
            case BMOff::MC_BASE + BMOff::SR:
            case BMOff::MC_BASE + BMOff::SR + 1:
            {
                uint16_t masked = mc_status_ & 0x1F;   // keep only legal bits 0..4

                if ((offset & 1) == 0)
                    val = masked & 0xFF;               // low byte
                else
                    val = 0x00;                        // high byte always 0

                break;
            }
            
            case (BMOff::PI_BASE + BMOff::CR):
                val = pi_control_;
                break;
            case BMOff::PO_BASE + BMOff::CR: {
                // CR is just the stored writable bits 0..4.
                // RESETREGS (bit1) always reads as 0 (self-clearing),
                // reserved bits 5–7 always read as zero.
                val = po_control_ & 0x1D;  // 0b00011101 (bit1 masked out)
                break;
            }
            case BMOff::MC_BASE + BMOff::CR:
                val = mc_control_;
                break;
            
            case BMOff::PI_BASE + BMOff::CIV:
                val = pi_civ_;
                break;
            case BMOff::PO_BASE + BMOff::CIV:
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
                printf("[AC97] READ LVI -> %02x\n", po_lvi_);
                break;
            case BMOff::MC_BASE + BMOff::LVI:
                val = mc_lvi_;
                break;
            case BMOff::CAS:
                val = 0U; // CAS not implemented
                break;
            default:
                throw std::runtime_error("[AC97 NABM] read8 not valid for register " + to_hex(offset));
        }
        printf("[AC97 NABM] read8  @%02x -> %02x\n", offset, val);
    
        return val;
    } else if(width==2) {
        switch (offset) { 
            case BMOff::PO_BASE + BMOff::PICB:
                val = po_picb_;
                break;
            default:
                throw std::runtime_error("[AC97 NABM] read16 not valid for register " + to_hex(offset));
            
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
                throw std::runtime_error("[AC97 NABM] read32 not valid for register " + to_hex(offset));
        
        }
        
        printf("[AC97 NABM] read32  @%02x -> %08x\n", offset, val);
        return val;
    }

    throw std::runtime_error("[AC97 NABM] read not valid for register " + to_hex(offset));
}

void AC97Pci::write_nabm(uint32_t offset, uint32_t value, uint8_t width)
{
    
    if(width == 1) {
        printf("[AC97 NABM] write8 @%02x <- %02x\n", offset, value);
        value = value & 0xFFU;
        switch (offset) {
            
            case BMOff::PO_BASE + BMOff::SR: {
                uint8_t clear_mask = value & 0x1E;   // bits 1..4 are W1C
                po_status_ &= ~clear_mask;
                TRACE_PO_SR_CHANGE();
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
                uint8_t ctl = value & 0x1F;

                bool req_reset = ctl & 0x02;   // RESETREGS
                bool req_start = ctl & 0x01;   // RUN

                // Update stored CR (bit1 always self-clears after we process it)
                po_control_ = ctl & ~0x02;

                // ------------------------------------------
                // 1. RESETREGS: reset internal playback state
                // ------------------------------------------
                if (req_reset) {

                    // Reset state
                    po_running_ = false;
                    po_civ_ = 0;
                    po_cur_ptr_ = 0;
                    po_cur_len_ = 0;
                    po_cur_ctl_ = 0;
                    po_picb_ = 0;
                    po_cur_bd_frame_offset_bytes_ = 0;

                    // Status according to ICH spec: RUN=0, BCIS=0, LVBCI=0, DCH=1
                    po_status_ = 0x01;

                    TRACE_PO_SR_CHANGE();
                    return;
                }

                // -------------------------------
                // 2. STOP request (RUN = 0)
                // -------------------------------
                if (!req_start) {
                    po_running_ = false;

                    // Clear BCIS+LVBCI, set DCH=1
                    po_status_ &= ~(0x0C);
                    po_status_ |= 0x01;

                    TRACE_PO_SR_CHANGE();
                    return;
                }

                // ------------------------------------------
                // 3. START request (RUN = 1)
                // ------------------------------------------

                // Already running?
                if (po_running_)
                    return;

                // BD BAR unprogrammed → cannot start yet
                if (bdbar_playback_ == 0 || !mctrl_.find_bank(bdbar_playback_)) {
                    po_running_ = false;
                    po_status_ |= 0x01;
                    return;
                }
                

                // Read BD0
                uint32_t bd_addr = bdbar_playback_ + (po_civ_ * 8);
                uint32_t ptr = mem_read32(bd_addr + 0);
                uint16_t len = mem_read16(bd_addr + 4);
                uint16_t ctl_field = mem_read16(bd_addr + 6);
                // 🔥 INSERT THIS HERE — BD is valid, Linux requested RUN=1
                printf("[BD_LOAD from PO CR] civ=%u ptr=%08x len=%u ctl=%04x\n",
                    po_civ_,
                    ptr,
                    (unsigned)(len + 1),
                    ctl_field);

                // ------------------------------------------
                // CORE FIX:
                // DMA MUST NOT START if BD0 is not VALID
                // (Linux writes BD BAR before BD contents)
                // ------------------------------------------
                // 1. If ptr==0 → invalid BD → DMA must not start, DCH stays set
                if (ptr == 0) {
                    po_running_ = false; 
                    po_status_ |= 0x01;  // DCH stays set
                    return;
                }

                // 2. If len==0 → empty BD → DMA must NOT run, but must NOT clear DCH
                // This matches hardware: empty BD stalls DMA without advancing CIV.
                if (len == 0) {
                    // Do not start DMA
                    po_running_ = false;
                    po_status_ |= 0x01;  // DCH must remain set
                    return;
                }

                // BD is valid → now it is safe to start
                po_running_ = true;

                // DMA running → clear DCH
                po_status_ &= ~0x01;
                TRACE_PO_SR_CHANGE();

                po_cur_ptr_ = ptr;
                po_cur_len_ = (uint32_t)len + 1;
                po_cur_ctl_ = ctl_field;
                po_cur_bd_frame_offset_bytes_ = 0;

                // Calculate frame count (stereo 16-bit = 4 bytes)
                po_picb_ = po_cur_len_ / 4;

                return;
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
                //printf("[AC97 DMA Setup] BD BAR=%08x LVI=%02x\n", bdbar_playback_, po_lvi_);
                break;
            case BMOff::MC_BASE + BMOff::LVI:
                mc_lvi_ = value & 0x1F;
                break;
            default:
                throw std::runtime_error("[AC97 NABM] Write not valid for register " + to_hex(offset));

        }
        return;
    } else if(width == 2) {
        value = value & 0xFFFFu;
        printf("[AC97 NABM] write16 @%02x <- %08x\n", offset, value);

        switch (offset) {
            // ----- PICB (playback PICB at least is used) -----
            case BMOff::PO_BASE + BMOff::PICB: { // 0x18
                po_picb_ = static_cast<uint16_t>(value);
                break;
            }
            
            default:
                throw std::runtime_error("[AC97 NABM] Write16 not valid for register " + to_hex(offset));

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

                bool old_cold = old & CNT_COLD;
                bool new_cold = value & CNT_COLD;

                bool old_warm = old & CNT_WARM;
                bool new_warm = value & CNT_WARM;

                // --- Cold reset: bit1 rises ---
                if (!old_cold && new_cold) {
                    printf("[AC97] Cold reset triggered\n");
                    cold_reset();
                    glob_cnt_ &= ~(CNT_COLD | CNT_WARM); // clear both COLD and WARM after done
                }

                // --- Warm reset: bit25 rises ---
                if (!old_warm && new_warm) {
                    std::cout << "[AC97] Warm reset triggered\n";

                    warm_reset();
                    glob_cnt_ &= ~(CNT_COLD | CNT_WARM); // clear both COLD and WARM after done

                    
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
                throw std::runtime_error("[AC97 NABM] Read16 not valid for register " + to_hex(offset));

        }
        return;    
    }

    throw std::runtime_error("[AC97 NABM] Read not valid for register " + to_hex(offset));
}

void AC97Pci::cold_reset()
{
    // Clear codec regs
    memset(codec_regs_, 0, sizeof(codec_regs_));
    init_codec();

    // DMA engine reset
    po_running_ = false;
    po_civ_ = 0;
    po_cur_ptr_ = 0;
    po_cur_len_ = 0;
    po_cur_ctl_ = 0;
    po_cur_bd_frame_offset_bytes_ = 0;
    po_picb_ = 0;

    bdbar_playback_ = 0;
    bdbar_capture_  = 0;

    // Status: RUN=0, BCIS=0, LVBCI=0, DCH=1
    po_status_ = 1;
    po_control_ = 0x02;

    // ----------- GLOB_STA initial state -----------
    // PCR + PCM IN + PCM OUT + (CRDY delayed)
    glob_sta_  = (1 << 0)      // PCR
               | (1 << 5)      // PCM In active
               | (1 << 6);     // PCM Out active

    // ----------- CRDY comes later ---------------
    glob_sta_ &= ~(1 << 8);    // make sure CRDY is off
    reset_delay_counter_ = 4;   // appear after 4 ticks

    // ----------- GLOB_CNT capabilities -----------
    glob_cnt_ = (1 << 18);      // VRA supported only

    printf("AFTER RESET: po_control_=0x%02x po_status_=0x%02x glob_sta_=0x%08x glob_cnt_=0x%08x\n",
           po_control_, po_status_, glob_sta_, glob_cnt_);
}

void AC97Pci::warm_reset()
{
    codec_regs_[0x26 >> 1] = 0x000F;

    codec_regs_[0x2A >> 1] &= 0x000F;

    po_status_ = 0x00;
    TRACE_PO_SR_CHANGE();
    mc_status_ = 0x00;

    glob_sta_ |= GS_CRDY_CODEC0;

    printf("AFTER WARM RESET: po_control_=0x%02x po_status_=0x%02x glob_sta_=0x%02x glob_cnt_=0x%02x\n", po_control_, po_status_, glob_sta_, glob_cnt_);
    
}




    