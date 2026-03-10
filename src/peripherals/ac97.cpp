// SPDX-License-Identifier: MIT
#include "ac97.hpp"

#include <bit>
#include <chrono>

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

    
    init_codec_cold();

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

void AC97Pci::init_codec_cold() {
    // Zero register file
    memset(codec_regs_, 0, sizeof(codec_regs_));

    // Vendor ID
    codec_regs_[0x7C >> 1] = 0x4144;  // 'AD'
    codec_regs_[0x7E >> 1] = 0x5348;  // 'SH' for AD1881A

    // Power Status: all analog blocks ON
    codec_regs_[0x26 >> 1] = 0x000F;

    // Extended Audio ID 
    codec_regs_[0x28 >> 1] = 0x0003;  // VRA + DAC

    // Extended Audio Status 
    codec_regs_[0x2A >> 1] = 0x0003;  // VRA enabled

    // Sample Rates reset to 48k
    codec_regs_[0x2C >> 1] = 48000;
    codec_regs_[0x32 >> 1] = 48000;

    // Mixer defaults
    codec_regs_[0x02 >> 1] = 0x0000;  
    codec_regs_[0x04 >> 1] = 0x0000;
    codec_regs_[0x18 >> 1] = 0x0808;
    codec_regs_[0x1A >> 1] = 0x0808;

    // General Purpose Register
    codec_regs_[0x20 >> 1] = 0x0008;

    // Hardware READY state
    glob_sta_ |= GS_PR;
    glob_sta_ &= ~GS_BUSY;

    power_status_ = 0x000F;
    
}

void AC97Pci::init_codec_warm() {

    // Mixer register defaults only
    codec_regs_[0x02 >> 1] = 0x0000;
    codec_regs_[0x04 >> 1] = 0x0000;
    codec_regs_[0x18 >> 1] = 0x0808;
    codec_regs_[0x1A >> 1] = 0x0808;

    // Keep GP (0x20)
    // Keep Extended Audio ID (0x28)
    // Keep Extended Audio Status bits
    // Keep DAC/ADC rates

    // Power status unchanged
    // Vendor ID unchanged

    // READY state
    glob_sta_ |= GS_PR;
    glob_sta_ &= ~GS_BUSY;
}


void AC97Pci::tick()
{
    // 0. DMA not running or BD BAR not set?
    if (!po_running_ || bdbar_playback_ == 0)
        return;

    // 1. Load current BD if not yet loaded (initial start or resume after LVBCI halt).
    if (po_cur_len_ == 0) {
        uint32_t bd_addr = bdbar_playback_ + (po_civ_ * 8);
        uint32_t ptr = mem_read32(bd_addr + 0);
        uint16_t len = mem_read16(bd_addr + 4);
        uint16_t ctl = mem_read16(bd_addr + 6);
#ifdef AC97_DEBUG
        printf("[AC97 BD LOAD] civ=%u ptr=%08x len=%u ctl=%04x\n", po_civ_, ptr, len, ctl);
#endif
        if (ptr == 0 || len == 0) {
            po_running_ = false;
            po_status_  = (po_status_ & ~0x02u) | 0x01u; // DCH=1, CIP=0
            TRACE_PO_SR_CHANGE();
            return;
        }
        po_cur_ptr_               = ptr;
        po_cur_len_               = (uint32_t)len;
        po_cur_ctl_               = ctl;
        po_cur_bd_frame_offset_bytes_ = 0;
        po_picb_                  = len / 2;  // len in 16-bit samples → frames
    }

    // 2. Determine how many frames to process this tick.
    uint32_t todo;
    if (use_wall_clock_) {
        // Production mode: produce audio at exactly 48 kHz wall-clock rate regardless
        // of how fast or slow the emulator runs.
        using clock = std::chrono::steady_clock;
        auto now = clock::now();
        if (!po_wall_clock_initialized_) {
            po_last_tick_wall_         = now;
            po_frame_credit_           = 0.0;
            po_wall_clock_initialized_ = true;
            return;  // seed the clock; produce frames on next tick
        }
        double elapsed_s = std::chrono::duration<double>(now - po_last_tick_wall_).count();
        po_last_tick_wall_ = now;
        po_frame_credit_ += elapsed_s * 48000.0;
        // Cap to one period to prevent latency burst after stalls
        if (po_frame_credit_ > 4096.0) po_frame_credit_ = 4096.0;
        todo = std::min<uint32_t>(static_cast<uint32_t>(po_frame_credit_), po_picb_);
        if (todo == 0)
            return;
    } else {
        // Test mode: fixed frames per tick (set by force_frames_per_tick()).
        todo = std::min<uint32_t>(frames_per_tick_dynamic_, po_picb_);
    }

#ifdef AC97_DEBUG
    printf("[TICK] picb=%u todo=%u offset=%u\n", po_picb_, todo, po_cur_bd_frame_offset_bytes_);
#endif

    // 3. Fetch 'todo' stereo frames from guest RAM and push to host audio.
    std::vector<int16_t> samples(todo * 2);
    uint32_t sample_ptr = po_cur_ptr_ + po_cur_bd_frame_offset_bytes_;
    for (uint32_t i = 0; i < todo * 2; ++i)
        samples[i] = static_cast<int16_t>(mem_read16(sample_ptr + i * 2));
    if (host_audio_)
        host_audio_->pushSamples(samples);

    // 4. Advance pointers.
    if (use_wall_clock_) po_frame_credit_ -= todo;
    po_cur_bd_frame_offset_bytes_ += todo * 4;  // 4 bytes per stereo S16 frame
    po_picb_                      -= todo;

    if (po_picb_ > 0)
        return;

    // 5. BD complete.
#ifdef AC97_DEBUG
    printf("[AC97 TICK] CIV=%u LVI=%u BD done, CTL=%04x\n", po_civ_, po_lvi_, po_cur_ctl_);
#endif

    // BCIS: signal buffer completion if IOC bit set in BD control word.
    if (po_cur_ctl_ & 0x8000) {
        po_status_ |= 0x08;
        TRACE_PO_SR_CHANGE();
    }

    // Advance CIV.
    po_civ_ = (po_civ_ + 1) & 0x1Fu;

    // LVBCI: last valid BD consumed — halt per AC'97 spec.
    // Linux must update LVI to resume; hardware auto-resumes when LVI > CIV.
    if (po_civ_ == ((po_lvi_ + 1) & 0x1Fu)) {
        po_status_ |= 0x04;                          // LVBCI
        po_status_  = (po_status_ & ~0x02u) | 0x01u; // DCH=1, CIP=0
        po_running_ = false;
        po_lvbci_halted_ = true;   // remember: halted by LVBCI (not explicit stop)
        po_cur_len_ = 0;  // signal: no BD loaded (tick() top will reload on resume)
        TRACE_PO_SR_CHANGE();
        printf("[AC97 LVBCI] Halted: civ=%u lvi=%u status=%02x ctrl=%02x\n",
               po_civ_, po_lvi_, po_status_, po_control_);

        // Raise interrupt and return — do NOT load next BD.
        if ((po_status_ & 0x0Cu) && (po_control_ & 0x10u)) {
            glob_sta_ |= GS_POINT;
            if (raise_intx_) raise_intx_();
        }
        return;
    }

    // Raise interrupt if BCIS is set and IOCE (bit 4 of CR) is enabled.
    if ((po_status_ & 0x0Cu) && (po_control_ & 0x10u)) {
        glob_sta_ |= GS_POINT;
        if (raise_intx_) {
            raise_intx_();
#ifdef AC97_DEBUG
            printf("RAISE IRQ! STATUS=%04x GLOB_STA=%08x\n", po_status_, glob_sta_);
#endif
        }
    }

    // Load the next BD from the new po_civ_ so PICB is immediately visible.
    {
        uint32_t bd_addr = bdbar_playback_ + (po_civ_ * 8);
        uint32_t ptr = mem_read32(bd_addr + 0);
        uint16_t len = mem_read16(bd_addr + 4);
        uint16_t ctl = mem_read16(bd_addr + 6);
#ifdef AC97_DEBUG
        printf("[AC97 BD LOAD from tick] civ=%u ptr=%08x len=%u ctl=%04x\n", po_civ_, ptr, len, ctl);
#endif
        if (ptr == 0 || len == 0) {
            po_running_ = false;
            po_cur_len_ = 0;
            po_status_  = (po_status_ & ~0x02u) | 0x01u; // DCH=1
            TRACE_PO_SR_CHANGE();
            return;
        }
        po_cur_ptr_               = ptr;
        po_cur_len_               = (uint32_t)len;
        po_cur_ctl_               = ctl;
        po_cur_bd_frame_offset_bytes_ = 0;
        po_picb_                  = len / 2;
    }
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
    // BUSY is a software model only; GS_PR (PCR) must never be cleared —
    // it is a hardware state bit (codec alive) checked by the ICH semaphore path.
    glob_sta_ |= GS_BUSY;
}

void AC97Pci::codec_command_complete()
{
    glob_sta_ &= ~GS_BUSY;
}

uint16_t AC97Pci::read_nam(uint32_t offset)
{
    
    if (offset >= 0x80)
        return 0xFFFF;
    
    codec_command_begin();

    uint16_t val = 0xFFFF; // default for invalid registers

    
    if (!nam_valid(offset)) {
        //printf("[AC97 NAM]  read INVALID @%02x -> FFFF\n", offset);
        codec_command_complete();
        return 0xFFFF;
    }
    
    
    switch (offset)
    {
        case 0x00:
            val = 0x0A00; // Dummy, return something != 0
            break;
        case 0x26: // Power-status
            val = power_status_;
            break;
        
        default:
            val = (codec_regs_[offset >> 1]);
            break;
    }
    

    //printf("[AC97 NAM]  read  @%02x -> %04x\n", offset, val);
    
    codec_command_complete();
    return val;
}

void AC97Pci::handle_0x26_power(uint16_t value) {
    power_ctrl_ = value;
    // if all bits clear -> start power-up sequence
    if (value == 0x0000) {
        // instant-ready for now
        power_status_ = 0x000F;  // all analog sections ready
    } else {
        // some bits set -> those blocks off
        power_status_ = (~value) & 0x000F;
    }
}

void AC97Pci::handle_0x2A_ext_audio_status(uint16_t value)
{
    uint16_t eaid = codec_regs_[0x28 >> 1];

    // EAST mirrors EAID in upper bits
    uint16_t newv = (eaid & 0xFFFE);

    // Only bit0 is writable (VRA enable)
    newv |= (value & 0x0001);

    codec_regs_[0x2A >> 1] = newv;
}

static uint16_t mask_volume_reg(uint16_t value)
{
    // Mute = bit 15
    // Volume L = bits 4..0
    // Volume R = bits 12..8
    // Reserved = must be zero
    uint16_t mute = value & 0x8000;
    uint16_t volL = value & 0x001F;        // lower 5 bits
    uint16_t volR = (value & 0x1F00);      // right 5 bits shifted left

    return mute | volR | volL;
}

void AC97Pci::write_nam(uint32_t offset, uint16_t value)
{
    if (offset >= 0x80)
        return;

    // take semaphore:
    codec_command_begin();

    if (!nam_valid(offset)) {
        //printf("[AC97 NAM]  write INVALID @%02x <- %04x (ignored)\n",
        //       offset, value);
        codec_command_complete();
        return;
    }
    
    //printf("[AC97 NAM]  write @%02x <- %04x\n", offset, value);

    // Typical reactions
    switch (offset) {
        case 0x00:
            init_codec_warm();
            break;

        case 0x26:
            handle_0x26_power(value);
            break;

        case 0x28:
            // ignore (RO)
            break;

        case 0x2A:
            handle_0x2A_ext_audio_status(value);
            break;
        
        case 0x02: // Master Volume
        case 0x04: // Headphone Volume
        case 0x18: // PCM Out Volume
        case 0x1A: // Line Out Volume
        {
            codec_regs_[offset >> 1] = mask_volume_reg(value);
            break;
        }

        default:
            //throw std::runtime_error("Default NAM not implemented");
            codec_regs_[offset >> 1] = value;
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
        (1u << 0)  |   // GSCI  - Global Status Change Interrupt (W1C)
        (1u << 1)  |   // MOINT - Modem Out Interrupt (W1C)
        (1u << 2)  |   // MINT  - Modem In Interrupt (W1C)
        (1u << 3)  |   // PIINT - PCM In Interrupt (W1C)
        (1u << 4)  |   // POINT - PCM Out Interrupt (W1C)
        (1u << 5)  |   // MINT  - Mic In Interrupt (W1C)
        (1u << 6)  |   // RCS   - Read Completion Status (W1C)
        (1u << 8)  |   // PCR   - Primary Codec Ready (R/O in hardware; emulator allows W1C)
        (1u << 15);    // S0R   - Sticky (clears on read)

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
                //printf("[AC97] READ LVI -> %02x\n", po_lvi_);
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
        //printf("[AC97 NABM] read8  @%02x -> %02x\n", offset, val);
    
        return val;
    } else if(width==2) {
        switch (offset) { 
            case BMOff::PO_BASE + BMOff::PICB:
                // Linux expects PICB in 16-bit samples; po_picb_ is in frames
                // stereo S16: 1 frame = 2 samples
                val = po_picb_ * 2;
                break;
            default:
                throw std::runtime_error("[AC97 NABM] read16 not valid for register " + to_hex(offset));
            
        }
        //printf("[AC97 NABM] read16  @%02x -> %02x\n", offset, val);
    
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
        
        //printf("[AC97 NABM] read32  @%02x -> %08x\n", offset, val);
        return val;
    }

    throw std::runtime_error("[AC97 NABM] read not valid for register " + to_hex(offset));
}

void AC97Pci::write_nabm(uint32_t offset, uint32_t value, uint8_t width)
{
    
    if(width == 1) {
        //printf("[AC97 NABM] write8 @%02x <- %02x\n", offset, value);
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
                    po_lvbci_halted_ = false;
                    po_civ_ = 0;
                    po_cur_ptr_ = 0;
                    po_cur_len_ = 0;
                    po_cur_ctl_ = 0;
                    po_picb_ = 0;
                    po_cur_bd_frame_offset_bytes_ = 0;
                    po_wall_clock_initialized_ = false;
                    po_frame_credit_ = 0.0;

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
                    po_lvbci_halted_ = false;  // explicit stop cancels LVBCI auto-resume

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
                if (bdbar_playback_ == 0 || !mctrl_.find_bank_or_null(bdbar_playback_)) {
                    po_running_ = false;
                    po_status_ |= 0x01;
                    return;
                }
                

                // Read BD0
                uint32_t bd_addr = bdbar_playback_ + (po_civ_ * 8);
                uint32_t ptr = mem_read32(bd_addr + 0);
                uint16_t len = mem_read16(bd_addr + 4);
                uint16_t ctl_field = mem_read16(bd_addr + 6);
#ifdef AC97_DEBUG
                printf("BD0 RAW len read = %04x\n", len);
                printf("[BD_LOAD from PO CR] civ=%u ptr=%08x len=%u ctl=%04x\n",
                    po_civ_, ptr, (unsigned)(len + 1), ctl_field);
#endif

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
                po_wall_clock_initialized_ = false;  // re-seed wall clock on fresh start
                po_frame_credit_ = 0.0;

                // DMA running → clear DCH
                po_status_ &= ~0x01;
                TRACE_PO_SR_CHANGE();

                po_cur_ptr_ = ptr;
                po_cur_len_ = (uint32_t)len;   // len is in 16-bit samples
                po_cur_ctl_ = ctl_field;
                po_cur_bd_frame_offset_bytes_ = 0;

                // len is in 16-bit samples; stereo S16 = 2 samples/frame
                po_picb_ = len / 2;
#ifdef AC97_DEBUG
                printf("[BD_INIT PO CR] frames=%u len=%u ptr=%08x\n",
                    po_picb_, po_cur_len_, po_cur_ptr_);
#endif

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
            {
                uint8_t old_lvi = po_lvi_;
                po_lvi_ = value & 0x1F;
                // Auto-resume: real ICH hardware resumes DMA automatically when LVI
                // is extended past the stall point.
                // NOTE: Linux clears the LVBCI status bit (W1C) BEFORE writing the
                // new LVI, so we cannot use LVBCI bit as the condition here.
                // Instead, use DCH=1 (bit 0), which remains set until we resume.
                // Auto-resume: only if this halt was caused by LVBCI (not explicit stop).
                // Using po_lvbci_halted_ instead of DCH bit avoids premature resume
                // during clock-measurement setup where Linux stops/restarts DMA
                // explicitly and writes LVI for the next measurement.
                bool should_resume = !po_running_ && po_lvbci_halted_;
                bool lvi_extended = (po_lvi_ != ((po_civ_ - 1 + 32u) & 0x1Fu));
                printf("[AC97 LVI write] old_lvi=%u new_lvi=%u civ=%u running=%d lvbci_halt=%d should_resume=%d lvi_extended=%d\n",
                       old_lvi, po_lvi_, po_civ_, (int)po_running_, (int)po_lvbci_halted_, (int)should_resume, (int)lvi_extended);
                if (should_resume && lvi_extended) {
                    po_running_ = true;
                    po_lvbci_halted_ = false;  // consumed the halt
                    po_wall_clock_initialized_ = false;  // re-seed; avoid latency burst
                    po_frame_credit_ = 0.0;
                    po_status_  = (po_status_ & ~0x01u) | 0x02u; // DCH=0, CIP=1
                    // po_cur_len_ == 0, so tick() will load BD from po_civ_
                    TRACE_PO_SR_CHANGE();
                    printf("[AC97 LVI write] AUTO-RESUME triggered, civ=%u lvi=%u\n", po_civ_, po_lvi_);
                }
                break;
            }
            case BMOff::MC_BASE + BMOff::LVI:
                mc_lvi_ = value & 0x1F;
                break;
            default:
                throw std::runtime_error("[AC97 NABM] Write not valid for register " + to_hex(offset));

        }
        return;
    } else if(width == 2) {
        value = value & 0xFFFFu;
        //printf("[AC97 NABM] write16 @%02x <- %08x\n", offset, value);

        switch (offset) {
            // ----- PICB (playback PICB at least is used) -----
            case BMOff::PO_BASE + BMOff::PICB: { // 0x18
                // PICB is read only!!!! Ignore
                //po_picb_ = static_cast<uint16_t>(value);
                break;
            }
            
            default:
                throw std::runtime_error("[AC97 NABM] Write16 not valid for register " + to_hex(offset));

        }
        return;

    } else {

        //printf("[AC97 NABM] write32 @%02x <- %08x\n", offset, value);

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
                    glob_cnt_ &= ~(CNT_COLD); // clear COLD after done
                }

                // --- Warm reset: bit2 rises ---
                if (!old_warm && new_warm) {
                    std::cout << "[AC97] Warm reset triggered\n";

                    warm_reset();
                    glob_cnt_ &= ~(CNT_WARM); // clear WARM after done

                    
                }
                break;
            }
            case BMOff::GLOB_STA: {
                // AC'97 NABM Global Status is write-one-to-clear
                
                // Only allow clearing bits that are W1C-capable
                uint32_t masked = value & GS_W1C_MASK;

                // Clear only those bits
                glob_sta_ &= ~masked;

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
    //
    // --- Correct AC'97 Cold Reset Semantics ---
    // Reset ALL codec registers (NAM space) to defaults.
    // Do NOT touch codec_regs_[] here directly — init_codec_cold()
    // owns the entire NAM-visible reset state.
    //
    init_codec_cold();   // <-- this does all codec register reset

    //
    // --- DMA Engine Reset (correct for cold reset) ---
    //
    po_running_ = false;
    po_lvbci_halted_ = false;
    po_civ_ = 0;
    po_cur_ptr_ = 0;
    po_cur_len_ = 0;
    po_cur_ctl_ = 0;
    po_cur_bd_frame_offset_bytes_ = 0;
    po_picb_ = 0;
    po_wall_clock_initialized_ = false;
    po_frame_credit_ = 0.0;

    bdbar_playback_ = 0;
    bdbar_capture_  = 0;

    // SR: only DCH bit must be 1, others zero
    po_status_  = 0x0001;  
    po_control_ = 0x0000;  // RUN=0, RESET bit auto-clears

    //
    // --- GLOB_STA Hardware Reset State ---
    //
    glob_sta_  = GS_PR;       // CRDY: primary codec ready (bit 8); all interrupt bits clear

    //
    // --- GLOB_CNT Codec Feature Bits ---
    //
    glob_cnt_ = (1 << 18);      // VRA only

    printf("AFTER COLD RESET: po_control_=0x%02x po_status_=0x%02x glob_sta_=0x%08x glob_cnt_=0x%08x\n",
           po_control_, po_status_, glob_sta_, glob_cnt_);
}

void AC97Pci::warm_reset()
{
    //
    // --- Correct AC'97 Warm Reset Semantics ---
    // Only mixer defaults must be reset.
    //
    init_codec_warm();   // <-- this resets ONLY mixer regs

    //
    // --- DMA & MC state reset is allowed ---
    // Warm reset synchronizes AC’97 link but does not reset codec features.
    //
    po_status_ = 0x0000;     // DCH=0 (temp), RUN=0, BCIS/LVBCI cleared
    TRACE_PO_SR_CHANGE();

    mc_status_ = 0x0000;

    //
    // READY state restored after warm reset
    //
    glob_sta_ |= GS_PR;      // Codec ready
    glob_sta_ &= ~GS_BUSY;   // Clear busy

    printf("AFTER WARM RESET: po_control_=0x%02x po_status_=0x%02x glob_sta_=0x%08x glob_cnt_=0x%08x\n",
           po_control_, po_status_, glob_sta_, glob_cnt_);
}



    