#pragma once
// AC97_Generic_PCI_Device.cpp (GRPCI2-integrated, Intel-compatible IDs)
// Generic AC'97 PCI audio controller emulating an Intel ICH AC'97 device (0x8086:0x2415)
// with GRPCI2-style AHB mappings:
//   - PCI I/O space:   0xFFFA0000  <-- No....
//   - PCI Config space:0xFFFB0000
//   - PCI Memory space:0x24000000
//
// Linux will recognize this as an Intel ICH AC'97 controller and automatically
// bind the snd-intel8x0 ALSA driver if available.

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <memory>

#include <pthread.h>
static void set_audio_thread_name() {
    pthread_setname_np(pthread_self(), "host audio");
}

#define TRACE_PO_SR_CHANGE() \
    printf("[AC97 Debug] PO_SR changed to %02x (file %s:%d)\n", po_status_, __FILE__, __LINE__);



#include <portaudio.h>
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
#include <iostream>

class HostAudioOut {
public:
    HostAudioOut(int sample_rate = 44100, int frames = 2048)
        : frames_(frames), sample_rate_(sample_rate)
    {
        PaError err = Pa_Initialize();
        if (err != paNoError)
            throw std::runtime_error("PortAudio init failed");

        err = Pa_OpenDefaultStream(
            &stream_,
            0,          // no input channels
            2,          // stereo output
            paInt16,    // sample format
            sample_rate_,
            frames_,
            nullptr,    // no callback (we’ll feed manually)
            nullptr);
        if (err != paNoError)
            throw std::runtime_error("PortAudio open failed");

        err = Pa_StartStream(stream_);
        if (err != paNoError)
            throw std::runtime_error("PortAudio start failed");

        startThread();
    }

    ~HostAudioOut() {
        stopThread();
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
        Pa_Terminate();
    }

    void pushSamples(const std::vector<int16_t>& samples) {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.insert(queue_.end(), samples.begin(), samples.end());
    }

private:
    void startThread() {
        running_ = true;
        thread_ = std::thread([this] {
            set_audio_thread_name();
            while (running_) {
                feedAudio();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    void stopThread() {
        running_ = false;
        if (thread_.joinable())
            thread_.join();
    }

    void feedAudio() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.size() >= static_cast<size_t>(frames_ * 2)) { // stereo → *2
            Pa_WriteStream(stream_, queue_.data(), frames_);
            queue_.erase(queue_.begin(), queue_.begin() + frames_ * 2);
        }
    }

private:
    PaStream* stream_ = nullptr;
    std::vector<int16_t> queue_;
    std::mutex mtx_;
    std::thread thread_;
    bool running_ = false;
    int frames_;
    int sample_rate_;
};

#include "MCTRL.h"
#include "pcidevice.hpp"

class PCIMMIOBank : public IMemoryBank {
public:
    PciDevice& dev;
    uint32_t base_addr_;
    uint32_t size_;

    PCIMMIOBank(PciDevice& d, uint32_t base_addr, uint32_t size) : IMemoryBank(Endian::Little), dev(d), base_addr_(base_addr), size_(size) {}

    virtual uint32_t read32(uint32_t addr, bool) const override {
        //uint32_t off = addr - base_addr_;
        return std::byteswap(dev.io_read32(addr));
    }

    virtual uint16_t read16(uint32_t addr, bool) const override {
        //uint32_t off = addr - base_addr_;
        return std::byteswap(dev.io_read16(addr));
    }

    virtual uint8_t read8(uint32_t addr) const override {
        //uint32_t off = addr - base_addr_;
        return dev.io_read8(addr);
    }

    virtual void write32(uint32_t addr, uint32_t val, bool) override {
        //uint32_t off = addr - base_addr_;
        dev.io_write32(addr, std::byteswap(val));
    }

    virtual void write16(uint32_t addr, uint16_t val, bool) override {
        //uint32_t off = addr - base_addr_;
        dev.io_write16(addr, std::byteswap(val));
    }

    virtual void write8(uint32_t addr, uint8_t val) override {
        //uint32_t off = addr - base_addr_;
        dev.io_write8(addr, val);
    }

    virtual bool contains(uint32_t paddr) const 
    {
        return (paddr >= base_addr_) && (paddr < (base_addr_ + size_));
    }

    // Gets a pointer to the host data buffer
    virtual u32* get_ptr() {throw std::runtime_error("get_ptr not implemented for PCIMMIO.");};

    u32 get_base() const override { return base_addr_; }
    u32 get_limit() const override { return base_addr_ + size_; }

};

#include "pcidevice.hpp"

class AC97Pci final : public PciDevice {
public:
    

    // Intel ICH AC'97 Audio Controller (compatible with snd-intel8x0)
    static constexpr uint16_t kVendorId = 0x8086; // Intel
    static constexpr uint16_t kDeviceId = 0x2415; // ICH AC'97 Audio Controller
    static constexpr uint8_t  kClassBase = 0x04;  // Multimedia
    static constexpr uint8_t  kClassSub  = 0x01;  // Audio
    static constexpr uint8_t  kProgIf    = 0x00;  // AC'97 controller
    static constexpr uint8_t  kHeaderType= 0x00;  // normal endpoint

    static constexpr uint8_t  kStatusCapsListBit = 0x10;
    static constexpr uint8_t  kCapPtrOffset      = 0x34;
    static constexpr uint8_t  kCapIdPM           = 0x01;

    static constexpr uint8_t kNumBars = 6;
    enum : uint8_t { BAR0 = 0x10, BAR1 = 0x14 };
    static constexpr uint32_t kNamSize  = 256;
    static constexpr uint32_t kNabmSize = 256;

    
    struct BMOff {
        static constexpr uint32_t PI_BASE = 0x00;
        static constexpr uint32_t PO_BASE = 0x10;
        static constexpr uint32_t MC_BASE = 0x20;

        static constexpr uint32_t BD_BAR  = 0x00;
        static constexpr uint32_t CIV     = 0x04;
        static constexpr uint32_t LVI     = 0x05;
        static constexpr uint32_t SR      = 0x06;
        static constexpr uint32_t PICB    = 0x08;
        static constexpr uint32_t PIV     = 0x0A;
        static constexpr uint32_t CR      = 0x0B;

        static constexpr uint32_t GLOB_CNT = 0x2C;
        static constexpr uint32_t GLOB_STA = 0x30;
        static constexpr uint32_t CAS      = 0x34;
    };
    
    
    explicit AC97Pci(uint8_t device_number,
                     std::function<bool(uint32_t, void*, size_t)> mem_read,
                     std::function<bool(uint32_t, const void*, size_t)> mem_write,
                     MCtrl& mctrl,
                     bool start_host_audio = true)
        : dev_num_(device_number), mem_read_(mem_read), mem_write_(mem_write), mctrl_(mctrl) {

        if(start_host_audio)
            host_audio_ = std::make_unique<HostAudioOut>(48000, 2);

        init_pci_config();
    }

    ~AC97Pci() = default;



    // ---------------- PCI and IO interface ----------------
    uint8_t  config_read8 (uint16_t off) override { return config_[off]; }
    uint16_t config_read16(uint16_t off) override { return read16(off); }
    uint32_t config_read32(uint16_t off) override { return read32(off); }
    void config_write8 (uint16_t off, uint8_t v) override { config_[off]=v; }
    void config_write16(uint16_t off, uint16_t v) override { write16(off,v); }
    void config_write32(uint16_t off, uint32_t v) override { write32(off,v); }

    uint32_t io_read32(uint32_t port) override;
    uint16_t io_read16(uint32_t port) override;
    uint8_t  io_read8 (uint32_t port) override;
    
    void io_write32(uint32_t port, uint32_t v) override;
    void io_write16(uint32_t port, uint16_t v) override;
    void io_write8 (uint32_t port, uint8_t v) override;
    
    void tick();

    void set_intx_cb(std::function<void()> cb) {
        raise_intx_ = std::move(cb);
    }

    void reset() {
        cold_reset();
    }

private:
    uint8_t dev_num_{0};
    std::array<uint8_t,256> config_{};
    std::array<uint8_t,256> nam_{};
    std::function<bool(uint32_t, void*, size_t)> mem_read_;
    std::function<bool(uint32_t, const void*, size_t)> mem_write_;
    MCtrl& mctrl_;
    
    std::function<void()> raise_intx_;
    std::unique_ptr<HostAudioOut> host_audio_;

    //uint32_t dma_ticks_per_buffer_ = 417; // TODO: Guess for now, Dynamic update
    //uint32_t dma_ticks_per_buffer_ = 1; // TODO: Guess for now, Dynamic update
    uint32_t dma_ticks_per_buffer_ = 10; // TODO: Guess for now, Dynamic update
    
    uint32_t bar_values_[kNumBars]{};  // store assigned base addresses
    bool probing_bar_[kNumBars]{};     // optional flags for 0xFFFFFFFF probe
    bool running_ = false;
    uint32_t nam_base_ = 0;
    uint32_t nabm_base_ = 0;
    
    // NAM = codec register file (0x00–0x7F)
    uint16_t codec_regs_[0x80/2] = {};

    //static constexpr uint32_t GS_CRDY_CODEC0 = 0x00000100;  // Codec-0 ready
    //static constexpr uint32_t GS_S0R  = (1u << 15);
    static constexpr uint32_t GS_PR   = (1u << 0);  // Primary Codec Ready
    static constexpr uint32_t GS_BUSY = (1u << 2);  // Command Busy
    static constexpr uint32_t GS_W1C_MASK = (GS_PR | GS_BUSY);  

    static constexpr uint32_t CNT_COLD     = 0x00000002;
    static constexpr uint32_t CNT_WARM     = 0x00000004;
    static constexpr uint32_t CNT_PCM_INO  = 0x00000008;
    static constexpr uint32_t CNT_PCM_OUTO = 0x00000010;
    static constexpr uint32_t CNT_VRA      = 1u << 18;
    
    // semaphore state variables
    //bool semaphore_pulse_pending_ = false;
    //bool semaphore_state_ = false; // false = 0 (busy), true = 1 (ready)

    // NABM = bus-master / DMA control area
    uint32_t bdbar_playback_ = 0;
    uint32_t bdbar_capture_  = 0;
    uint32_t glob_cnt_       = 0;
    uint32_t glob_sta_       = GS_PR;  // Codec0 Ready

    uint8_t pi_control_ = 0;
    uint8_t po_control_ = 0;
    uint8_t mc_control_ = 0;

    uint16_t pi_status_ = 0;
    uint16_t po_status_  = 0x0000;   // RUN=0, BCIS=0, LVBCI=0
    uint16_t mc_status_ = 0;

    uint8_t pi_civ_ = 0;
    uint8_t po_civ_ = 0;
    uint8_t mc_civ_ = 0;

    uint8_t pi_lvi_ = 0;
    uint8_t po_lvi_ = 0;
    uint8_t mc_lvi_ = 0;

    uint16_t po_picb_ = 0x0;

    uint16_t power_ctrl_ = 0;
    uint16_t power_status_ = 0;
    
    // PO state variables
    uint32_t po_cur_ptr_ = 0;
    uint32_t po_cur_len_ = 0;                 
    uint16_t po_cur_ctl_ = 0;
    uint32_t po_cur_bd_frame_offset_bytes_ = 0;
    bool     po_running_ = false;

    uint64_t dma_tick_counter_ = 0;

    uint32_t reset_delay_counter_ = 0;
    

    uint16_t read_nam(uint32_t of);
    void write_nam(uint32_t of, uint16_t val);
    void codec_command_begin();
    void codec_command_complete();
    void handle_0x26_power(uint16_t value);
    void handle_0x2A_ext_audio_status(uint16_t value);
    
    uint32_t read_glob_sta();
    uint32_t read_nabm(uint32_t of, uint8_t width);
    void write_nabm(uint32_t of, uint32_t val, uint8_t width);

    void cold_reset();
    void warm_reset();
    
    
    uint16_t read_po_sr() const {
        return po_status_;
    }

    void write_po_sr(uint16_t val) {
        // Write-1-to-clear for bits 2–4
        po_status_ &= ~(val & 0x001C);
        TRACE_PO_SR_CHANGE();
    }

    void set_run(bool enable) {
        running_ = enable;
        if (enable) {
            po_status_ &= ~0x0001;  // clear DCH
            po_status_ |=  0x0002;  // set CIP
            TRACE_PO_SR_CHANGE();

            
        } else {
            po_status_ |=  0x0001;  // halted
            po_status_ &= ~0x0002;  // clear CIP
            TRACE_PO_SR_CHANGE();
        }
    }

    void init_pci_config();

    void init_grpci2_cap(uint8_t off, uint8_t next);

    void init_codec_cold();
    void init_codec_warm();

    void write16(uint16_t off, uint16_t v) noexcept {
        config_[off + 0] = static_cast<uint8_t>(v & 0x00FF);
        config_[off + 1] = static_cast<uint8_t>((v >> 8) & 0x00FF);
    }

    void write32(uint16_t off, uint32_t v) noexcept {
        switch (off) {
            case BAR0: {   // NAM BAR
                uint8_t bar_idx = 0;
                if (v == 0xFFFFFFFFu) {
                    //std::cout << "[AC97] write32, probing NAM bar\n";

                    probing_bar_[bar_idx] = true;
                } else if (v == 0) {
                    //std::cout << "[AC97] write32, disabling NAM bar, idx=" << to_hex(bar_idx) << "\n";

                    // BAR disabled
                    probing_bar_[bar_idx] = false;
                    bar_values_[bar_idx] = 0;

                    // DO NOT ATTACH MEMORY
                    nam_base_ = 0;
                    
                } else {
                    //std::cout << "[AC97] write32, creating NAM bar, 0x" << to_hex(v) << "\n";

                    probing_bar_[bar_idx] = false;

                    // Linux writes a physical MMIO address here
                    bar_values_[bar_idx] = (v & 0xFFFFFFF0u);

                    // AC'97 NAM region = first BAR
                    nam_base_ = bar_values_[bar_idx];
                    //std::cout << "[AC97] write32, creating new memory area at 0x" << to_hex(nam_base_) << "\n";

                    //mctrl_.attach_bank<PCIMMIOBank>(*this, nam_base_);
                }
                break;
            }

            case BAR1: {   // NABM BAR
                uint8_t bar_idx = 1;
                if (v == 0xFFFFFFFFu) {
                    //std::cout << "[AC97] write32, probing NABM bar\n";

                    probing_bar_[bar_idx] = true;
                } else if (v == 0) {
                    //std::cout << "[AC97] write32, disabling NAM bar, idx=" << to_hex(bar_idx) << "\n";

                    // BAR disabled
                    probing_bar_[bar_idx] = false;
                    bar_values_[bar_idx] = 0;

                    // DO NOT ATTACH MEMORY
                    nabm_base_ = 0;
                    
                } else {
                    //std::cout << "[AC97] write32, creating NABM bar, 0x" << to_hex(v) << "\n";

                    probing_bar_[bar_idx] = false;

                    bar_values_[bar_idx] = (v & 0xFFFFFFF0u);

                    // AC'97 NABM region = second BAR
                    nabm_base_ = bar_values_[bar_idx];
                    //std::cout << "[AC97] write32, creating new memory area at 0x" << to_hex(nabm_base_) << "\n";
                    //mctrl_.attach_bank<PCIMMIOBank>(*this, nabm_base_);
                }
                break;
            }
            default:
                config_[off + 0] = static_cast<uint8_t>(v & 0x000000FF);
                config_[off + 1] = static_cast<uint8_t>((v >> 8) & 0x000000FF);
                config_[off + 2] = static_cast<uint8_t>((v >> 16) & 0x000000FF);
                config_[off + 3] = static_cast<uint8_t>((v >> 24) & 0x000000FF);
                break;  
        }
        
    }

    uint16_t read16(uint16_t off) const noexcept {
        return static_cast<uint16_t>(config_[off + 0])
            | static_cast<uint16_t>(config_[off + 1]) << 8;
    }

    uint32_t read32(uint16_t off) const noexcept {
        uint8_t bar_idx = 0;
        switch (off) {
            case BAR0:
            case BAR1:
                bar_idx = (off - BAR0) / 4;  // 0 or 1

                if (probing_bar_[bar_idx]) {
                    //std::cout << "[AC97] read32, probing NABM or NAM bar\n";

                    // 256-byte I/O aperture → return I/O mask
                    //return 0xFFFF00FCu;  // bit0=0 for mask, BAR says I/O
                    // Return size mask for 256-byte memory BAR
                    return 0xFFFFFF00u;
                }
                return bar_values_[bar_idx];
            default:
                // Read from config space
                return static_cast<uint32_t>(config_[off + 0])
                    | static_cast<uint32_t>(config_[off + 1]) << 8
                    | static_cast<uint32_t>(config_[off + 2]) << 16
                    | static_cast<uint32_t>(config_[off + 3]) << 24;
        }

        
    }

    static std::string to_hex(uint32_t val) {
        char buf[11];
        snprintf(buf, sizeof(buf), "%08X", val);
        return buf;
    }

    static std::string to_hex(uint16_t val) {
        char buf[11];
        snprintf(buf, sizeof(buf), "%04X", val);
        return buf;
    }

    static std::string to_hex(uint8_t val) {
        char buf[11];
        snprintf(buf, sizeof(buf), "%02X", val);
        return buf;
    }

    uint32_t mem_read32(uint32_t addr) {
        uint32_t val = 0;
        if(!mem_read_(addr, &val, 4))
            throw std::runtime_error("[AC97] mem_read32: Could no access memory at 0x" + to_hex(addr));
        return val;
    }

    uint16_t mem_read16(uint32_t addr) {
        uint16_t val = 0;
        if(!mem_read_(addr, &val, 2))
            throw std::runtime_error("[AC97] mem_read16: Could no access memory at 0x" + to_hex(addr));
        return val;
    }
};
