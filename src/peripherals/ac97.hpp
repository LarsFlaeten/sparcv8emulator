#pragma once
// AC97_Generic_PCI_Device.cpp (GRPCI2-integrated, Intel-compatible IDs)
// Generic AC'97 PCI audio controller emulating an Intel ICH AC'97 device (0x8086:0x2415)
// with GRPCI2-style AHB mappings:
//   - PCI I/O space:   0xFFFA0000
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
void set_audio_thread_name() {
    pthread_setname_np(pthread_self(), "host audio");
}



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
                     std::function<bool(uint32_t, const void*, size_t)> mem_write)
        : dev_num_(device_number), mem_read_(mem_read), mem_write_(mem_write) {

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

private:

    uint8_t dev_num_{0};
    std::array<uint8_t,256> config_{};
    std::array<uint8_t,256> nam_{};
    std::function<bool(uint32_t, void*, size_t)> mem_read_;
    std::function<bool(uint32_t, const void*, size_t)> mem_write_;
    std::function<void()> raise_intx_;
    std::unique_ptr<HostAudioOut> host_audio_;

    //uint32_t dma_ticks_per_buffer_ = 417; // TODO: Guess for now, Dynamic update
    //uint32_t dma_ticks_per_buffer_ = 1; // TODO: Guess for now, Dynamic update
    uint32_t dma_ticks_per_buffer_ = 10; // TODO: Guess for now, Dynamic update
    
    uint32_t bar_values_[kNumBars]{};  // store assigned base addresses
    bool probing_bar_[kNumBars]{};     // optional flags for 0xFFFFFFFF probe
    bool running_ = false;
    
    // NAM = codec register file (0x00–0x7F)
    uint16_t codec_regs_[0x80/2] = {};

    static constexpr uint32_t GS_CRDY_CODEC0 = 0x00000100;  // Codec-0 ready
    //static constexpr uint32_t GS_CRDY = 0x00000100;     // Codec ready
    static constexpr uint32_t GS_S0R  = 0x00800000;   // semaphore bit for codec 0
    static constexpr uint32_t GS_S1R  = 0x01000000;     // Codec semaphore (Codec 1)
    
    // semaphore state variables
    bool semaphore_pulse_pending_ = false;
    bool semaphore_state_ = false; // false = 0 (busy), true = 1 (ready)

    // NABM = bus-master / DMA control area
    uint32_t bdbar_playback_ = 0;
    uint32_t bdbar_capture_  = 0;
    uint32_t glob_cnt_       = 0;
    uint32_t glob_sta_       = GS_CRDY_CODEC0;  // bit8 = Codec0 Ready

    uint8_t pi_control_ = 0;
    uint8_t po_control_ = 0;
    uint8_t mc_control_ = 0;

    uint16_t pi_status_ = 0;
    uint16_t po_status_ = 0x0001; // DCH=1, stopped initially
    uint16_t mc_status_ = 0;

    uint8_t pi_civ_ = 0;
    uint8_t po_civ_ = 0;
    uint8_t mc_civ_ = 0;

    uint8_t pi_lvi_ = 0;
    uint8_t po_lvi_ = 0;
    uint8_t mc_lvi_ = 0;

    uint16_t po_picb_ = 0x0100;

    uint16_t power_ctrl_ = 0;
    uint16_t power_status_ = 0;
    
    

    uint16_t read_nam(uint32_t of);
    void write_nam(uint32_t of, uint16_t val);
    void codec_command_begin();
    void codec_command_complete();
    
    uint32_t read_glob_sta();
    uint32_t read_nabm(uint32_t of, uint8_t width);
    void write_nabm(uint32_t of, uint32_t val, uint8_t width);

    void cold_reset();
    
    uint16_t read_po_sr() const {
        return po_status_;
    }

    void write_po_sr(uint16_t val) {
        // Write-1-to-clear for bits 2–4
        po_status_ &= ~(val & 0x001C);
    }

    void set_run(bool enable) {
        running_ = enable;
        if (enable) {
            po_status_ &= ~0x0001;  // clear DCH
            po_status_ |=  0x0002;  // set CIP

            
        } else {
            po_status_ |=  0x0001;  // halted
            po_status_ &= ~0x0002;  // clear CIP
        }

        po_picb_ = 0x2000 / 4; //buf_len_in_samples; 
    }

    void init_pci_config(){
        write16(0x00,kVendorId); 
        write16(0x02,kDeviceId);
        config_[0x08]=0x01; 
        config_[0x09]=kProgIf; 
        config_[0x0A]=kClassSub; 
        config_[0x0B]=kClassBase; 
        config_[0x0E]=kHeaderType;
        write16(0x06,read16(0x06)|kStatusCapsListBit);
        write32(BAR0,0x00000001); 
        write32(BAR1,0x00000001);
        write32(0x30, 0x00000000); // No expansion ROM
        config_[0x3D]=0x01; 
        config_[0x3C]=0xFF;
        config_[kCapPtrOffset]=0x50;
        
        // Initialize GRPCI2 capability at 0x50
        init_grpci2_cap(0x50, 0x00);

        
        init_codec();
    }

    void init_grpci2_cap(uint8_t off, uint8_t next) {
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
    }

    void init_codec();

    void write16(uint16_t off, uint16_t v) noexcept {
        config_[off + 0] = static_cast<uint8_t>(v & 0x00FF);
        config_[off + 1] = static_cast<uint8_t>((v >> 8) & 0x00FF);
    }

    void write32(uint16_t off, uint32_t v) noexcept {
        switch (off) {
            case BAR0:
            case BAR1: {
                uint8_t bar_idx = (off - BAR0) / 4;

                if (v == 0xFFFFFFFFu) {
                    // Linux probing BAR size
                    probing_bar_[bar_idx] = true;
                } else {
                    probing_bar_[bar_idx] = false;
                    // Keep BAR0/1 as I/O type (bit0=1), ignore writes to bits [3:0].
                    bar_values_[bar_idx] = (v & 0xFFFFFFF0u) | 0x1u;
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
                    // Return mask to indicate 256-byte I/O space
                    return 0xFFFFFF01u;
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
};
