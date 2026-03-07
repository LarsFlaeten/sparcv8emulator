#pragma once
// Memory controller, and associated memory banks

#include <cstdint>
#include <array>
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <iomanip>
#include <string>
#include <atomic>
#ifndef NDEBUG
#include <cassert>
#endif

#include "../common.h"

#if 0
#define PROFILE_MEM_ACCESS
#include "../memaccessprofiler.hpp"
#endif

std::string demangle(const char* name);

enum class Endian {
    Little,
    Big
};

struct AtomicResult {
  bool ok;          // false => bus error / not supported
  uint32_t old;     // old value (byte in low 8 bits for ldstub)
};

enum class MemBusStatus : u8 { Ok, NoDevice /* decode hole */, DeviceError /* optional */ };



class IMemoryBank {
public:
    IMemoryBank(Endian endian = Endian::Big) : bankEndian(endian) {}
    virtual ~IMemoryBank() = default;

    virtual bool contains(u32 addr) const = 0;
    virtual u8 read8(u32 addr) const = 0;
    virtual void write8(u32 addr, u8 val) = 0;
    virtual u32 get_base() const = 0;
    virtual u64 get_end_exclusive() const = 0;
    virtual u32 get_size() const = 0;
    
    virtual void lock() {}; // For lockable banks (ROM-type)
    
    // Gets a pointer to the host data buffer
    virtual u32* get_ptr() = 0;

    // Atomics with RAM access (default: not supported)
    virtual AtomicResult atomic_ldstub8(uint32_t paddr) { throw std::runtime_error("LDSTUB on nonram bank");  return {false, 0}; }
    virtual AtomicResult atomic_swap32 (uint32_t paddr, uint32_t newv) { throw std::runtime_error("SWAP on nonram bank"); return {false, 0}; }
    
    /*
    virtual AtomicResult atomic_casa32 (uint32_t paddr, uint32_t expected, uint32_t desired, bool* swapped) {
        throw std::runtime_error("CASA on nonram bank");
        if (swapped) *swapped = false;
        return {false, 0};
    }
    */
    virtual u16 read16(u32 addr, bool align = true) const {
        #ifndef NDEBUG
        if (align && (addr & 1))
            assert(false);
        #endif

        u8 hi = read8(addr);
        u8 lo = read8(addr + 1);
        return (bankEndian == Endian::Big) ? (hi << 8 | lo) : (lo << 8 | hi);
    }

    virtual void write16(u32 addr, u16 val, bool align = true) {
        #ifndef NDEBUG
        if (align && (addr & 1))
            assert(false);
        #endif
        if (bankEndian == Endian::Big) {
            write8(addr,     (val >> 8) & 0xFF);
            write8(addr + 1,  val       & 0xFF);
        } else {
            write8(addr,      val       & 0xFF);
            write8(addr + 1, (val >> 8) & 0xFF);
        }
    }

    virtual u32 read32(u32 addr, bool align = true) const {
        #ifndef NDEBUG
        if (align && (addr & 3))
            assert(false);
        #endif

        u8 b0 = read8(addr);
        u8 b1 = read8(addr + 1);
        u8 b2 = read8(addr + 2);
        u8 b3 = read8(addr + 3);
        if (bankEndian == Endian::Big) {
            return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
        } else {
            return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
        }
    }

    virtual void write32(u32 addr, u32 val, bool align = true) {
        #ifndef NDEBUG
        if (align && (addr & 3))
            assert(false);
        #endif
        
        if (bankEndian == Endian::Big) {
            write8(addr,     (val >> 24) & 0xFF);
            write8(addr + 1, (val >> 16) & 0xFF);
            write8(addr + 2, (val >> 8)  & 0xFF);
            write8(addr + 3,  val        & 0xFF);
        } else {
            write8(addr,      val        & 0xFF);
            write8(addr + 1, (val >> 8)  & 0xFF);
            write8(addr + 2, (val >> 16) & 0xFF);
            write8(addr + 3, (val >> 24) & 0xFF);
        }
    }

    virtual u64 read64(u32 addr, bool align = true) const {
        #ifndef NDEBUG
        if (align && (addr & 7))
            assert(false);
        #endif
       
        u32 hi, lo;
        if (bankEndian == Endian::Big) {
            hi = read32(addr);
            lo = read32(addr + 4);
        } else {
            lo = read32(addr);
            hi = read32(addr + 4);
        }
        return (static_cast<u64>(hi) << 32) | lo;
    }

    virtual void write64(u32 addr, u64 val, bool align = true) {
        #ifndef NDEBUG
        if (align && (addr & 7))
            assert(false);
        #endif
        u32 hi = static_cast<u32>(val >> 32);
        u32 lo = static_cast<u32>(val & 0xFFFFFFFF);
        if (bankEndian == Endian::Big) {
            write32(addr,     hi);
            write32(addr + 4, lo);
        } else {
            write32(addr,     lo);
            write32(addr + 4, hi);
        }
    }

    // Derived must override these if locks are used
    virtual u8 read8_nolock(u32 addr) const noexcept {
        return read8(addr);
    }

    virtual void write8_nolock(u32 addr, u8 val) noexcept {
        write8(addr, val);
        return;
    }

    // Derivde must override these if locks are used
    virtual u16 read16_nolock(u32 addr, bool align = true) const noexcept {
        return read16(addr, align);
    }

    virtual void write16_nolock(u32 addr, u16 val, bool align = true) noexcept {
        write16(addr, val, align);
        return;
    }

    virtual u32 read32_nolock(u32 addr, bool align = true) const noexcept {
        return read32(addr, align);
    }

    virtual void write32_nolock(u32 addr, u32 val, bool align = true) noexcept{
        write32(addr, val, align);
        return;
    }



    Endian get_endian() const { return bankEndian; }

    virtual std::shared_mutex& get_mutex(u32 paddr) {return mtx;}


protected:
    std::shared_mutex mtx = {}; // Dummy mutex

    Endian bankEndian;

    static std::string to_hex(u32 val) {
        char buf[11];
        snprintf(buf, sizeof(buf), "%08X", val);
        return buf;
    }
};


template <size_t N>
class RomBank : public IMemoryBank {
public:
    RomBank(u32 base, Endian endian = Endian::Big) : IMemoryBank(endian), base(base), writeable(true) {
        data.fill(0);
    }

    bool contains(u32 addr) const override {
        return (uint64_t(addr) - uint64_t(base)) < uint64_t(N);
    }

    u8 read8(u32 addr) const override {
        //check_range(addr);
        return data[addr - base];
    }

    void write8(u32 addr, u8 val) override {
        if(!writeable)
            throw std::runtime_error("Write to read-only memory");
        else {
            //check_range(addr);
            data[addr - base] = val;
        }
    }

    u32 get_base() const override { return base; }
    u64 get_end_exclusive() const override { return (u64)base + (u64)N; }
    u32 get_size() const override { return N; }
    void lock() override { writeable = false; }

    u32* get_ptr() override { return reinterpret_cast<u32*>(data.data());}

private:
    u32 base;
    bool writeable;
    std::array<u8, N> data;

    void check_range(u32 addr) const {
        if (!contains(addr))
            throw std::out_of_range("ROM access out of range");
    }
};

class RamBank : public IMemoryBank {
public:
    RamBank(u32 base, size_t size, Endian endian = Endian::Big)
        : IMemoryBank(endian), base(base), size(size), data(size, 0) {
        
        if (bankEndian == Endian::Big) {
            read16_fn_  = &RamBank::read16_be;
            read32_fn_  = &RamBank::read32_be;
            write16_fn_ = &RamBank::write16_be;
            write32_fn_ = &RamBank::write32_be;
        } else {
            read16_fn_  = &RamBank::read16_le;
            read32_fn_  = &RamBank::read32_le;
            write16_fn_ = &RamBank::write16_le;
            write32_fn_ = &RamBank::write32_le;
        }

    }

    bool contains(u32 addr) const override {
        return (uint64_t(addr) - uint64_t(base)) < uint64_t(size);
    }

    u8 read8(u32 addr) const noexcept override  {
        return data[addr - base];
    }

    void write8(u32 addr, u8 val) noexcept override {
        data[addr - base] = val;
    }

    u16 read16(u32 addr, bool align = true) const override {
        return (this->*read16_fn_)(addr, align);
    }

    void write16(u32 addr, u16 val, bool align = true) override {
        #ifndef NDEBUG
        auto off64 = uint64_t(addr) - uint64_t(base);
        if (off64 + 3 >= size) {
            fprintf(stderr,
                "RamBank OOB write16: addr=%08x base=%08x off=%llx mem_size=%zu\n",
                addr, base, (unsigned long long)off64, size);
            std::abort();
        }
        #endif
        (this->*write16_fn_)(addr, val, align);
    }

    u32 read32(u32 addr, bool align = true) const override {
        return (this->*read32_fn_)(addr, align);
    }

    void write32(u32 addr, u32 val, bool align = true) override {
        #ifndef NDEBUG
        auto off64 = uint64_t(addr) - uint64_t(base);
        if (off64 + 3 >= size) {
            fprintf(stderr,
                "RamBank OOB write32: addr=%08x base=%08x off=%llx mem_size=%zu\n",
                addr, base, (unsigned long long)off64, size);
            std::abort();
        }
        #endif
        (this->*write32_fn_)(addr, val, align);
    }
    

    u16 read16_be(u32 addr, bool align = true) const {
#ifndef NDEBUG
        if (align && (addr & 1)) std::abort();
#endif
        const u32 off = addr - base;
#ifndef NDEBUG
        if (off + 1 >= data.size()) std::abort();
#endif
        const u8* p = &data[off];
        // SPARC RAM as big-endian
        return (u16(p[0]) << 8) | u16(p[1]);
    }

    void write16_be(u32 addr, u16 val, bool align = true) {
#ifndef NDEBUG
        if (align && (addr & 1)) std::abort();
#endif
        const u32 off = addr - base;
#ifndef NDEBUG
        if (off + 1 >= data.size()) std::abort();
#endif
        u8* p = &data[off];
        p[0] = u8((val >> 8) & 0xff);
        p[1] = u8((val >> 0) & 0xff);
    }

    u32 read32_be(u32 addr, bool align = true) const {
        // assume align already handled above; if you keep align param:
#ifndef NDEBUG
        if (align && (addr & 3)) std::abort();
#endif
        const u32 off = addr - base;              // or however you map
        const u8* p = &data[off];                  // mem_ is contiguous u8 storage

        // Guest is big-endian on SPARC; assemble without calling read8()
        return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
    }

    void write32_be(u32 addr, u32 val, bool align = true) {
#ifndef NDEBUG
        if (align && (addr & 3)) std::abort();
#endif
        const u32 off = addr - base;
        u8* p = &data[off];
        p[0] = (val >> 24) & 0xff;
        p[1] = (val >> 16) & 0xff;
        p[2] = (val >>  8) & 0xff;
        p[3] = (val >>  0) & 0xff;
    }

    u16 read16_le(u32 addr, bool align = true) const {
#ifndef NDEBUG
        if (align && (addr & 1)) std::abort();
#endif
        const u32 off = addr - base;
#ifndef NDEBUG
        if (off + 1 >= data.size()) std::abort();
#endif
        const u8* p = &data[off];
        // SPARC RAM as big-endian
        return (u16(p[1]) << 8) | u16(p[0]);
    }

    void write16_le(u32 addr, u16 val, bool align = true) {
#ifndef NDEBUG
        if (align && (addr & 1)) std::abort();
#endif
        const u32 off = addr - base;
#ifndef NDEBUG
        if (off + 1 >= data.size()) std::abort();
#endif
        u8* p = &data[off];
        p[1] = u8((val >> 8) & 0xff);
        p[0] = u8((val >> 0) & 0xff);
    }

    u32 read32_le(u32 addr, bool align = true) const {
        // assume align already handled above; if you keep align param:
#ifndef NDEBUG
        if (align && (addr & 3)) std::abort();
#endif
        const u32 off = addr - base;              // or however you map
        const u8* p = &data[off];                  // mem_ is contiguous u8 storage

        return (u32(p[3]) << 24) | (u32(p[2]) << 16) | (u32(p[1]) << 8) | u32(p[0]);
    }

    void write32_le(u32 addr, u32 val, bool align = true) {
#ifndef NDEBUG
        if (align && (addr & 3)) std::abort();
#endif
        const u32 off = addr - base;
        u8* p = &data[off];
        p[3] = (val >> 24) & 0xff;
        p[2] = (val >> 16) & 0xff;
        p[1] = (val >>  8) & 0xff;
        p[0] = (val >>  0) & 0xff;
    }


    u32 get_base() const override { return base; }
    u64 get_end_exclusive() const override { return (u64)base + size; }
    u32 get_size() const override { return size; }

    u32* get_ptr() override { return reinterpret_cast<u32*>(data.data());}

    static constexpr int NUM_SHARDS = 64;

    std::shared_mutex& get_mutex(u32 addr) override {
        return shard_mtx_[(addr >> 12) & (NUM_SHARDS - 1)];
    }

#ifdef PERF_STATS
    struct RamStats {
        std::atomic<uint64_t> lock_acquired{0};
        std::atomic<uint64_t> lock_contended{0};  // try_lock failed → had to block
    };
    mutable RamStats ram_stats_;
public:
    const RamStats& get_ram_stats() const { return ram_stats_; }
    void perf_lock_acquired()  { ram_stats_.lock_acquired .fetch_add(1, std::memory_order_relaxed); }
    void perf_lock_contended() { ram_stats_.lock_contended.fetch_add(1, std::memory_order_relaxed); }
private:
#endif

    AtomicResult atomic_ldstub8(uint32_t paddr) override {
        std::unique_lock<std::shared_mutex> lk(get_mutex(paddr));
        uint8_t old = read8_nolock(paddr);
        write8_nolock(paddr, 0xFF);
        return {true, (uint32_t)old};
    }

    AtomicResult atomic_swap32(uint32_t paddr, uint32_t newv) override {
        std::unique_lock<std::shared_mutex> lk(get_mutex(paddr));
        uint32_t old = read32_nolock(paddr);
        write32_nolock(paddr, newv);
        return {true, old};
    }

    /*
    AtomicResult atomic_casa32(uint32_t paddr, uint32_t expected, uint32_t desired, bool* swapped) override {
        std::lock_guard lk(global_ram_mtx);
        uint32_t old = read32_nolock(paddr);
        if (old == expected) {
            write32_nolock(paddr, desired);
            if (swapped) *swapped = true;
        } else {
            if (swapped) *swapped = false;
        }
        return {true, old};
    }
    */
private:
    u32 base;
    size_t size;
    std::vector<u8> data;

    mutable std::array<std::shared_mutex, NUM_SHARDS> shard_mtx_;

    using Read16Fn  = u16 (RamBank::*)(u32,bool) const;
    using Read32Fn  = u32 (RamBank::*)(u32,bool) const;
    using Write16Fn = void (RamBank::*)(u32,u16,bool);
    using Write32Fn = void (RamBank::*)(u32,u32,bool);

    Read16Fn  read16_fn_;
    Read32Fn  read32_fn_;
    Write16Fn write16_fn_;
    Write32Fn write32_fn_;

    void check_range(u32 addr) const {
        if (!contains(addr))
            throw std::out_of_range("RAM access out of range");
    }

    
  public: 

    virtual u8 read8_nolock(u32 addr) const noexcept override {
        return data[addr - base];
    }

    virtual void write8_nolock(u32 addr, u8 val) noexcept override {
        data[addr - base] = val;
    }

    virtual u32 read32_nolock(u32 addr, bool align = true) const noexcept override {
        #ifndef NDEBUG
        if (align && (addr & 3))
            assert(false);
        #endif
        const u8* p = &data[addr - base];
        if (bankEndian == Endian::Big) {
            return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
        } else {
            return (u32(p[3]) << 24) | (u32(p[2]) << 16) | (u32(p[1]) << 8) | u32(p[0]);
        }
    }

    virtual void write32_nolock(u32 addr, u32 val, bool align = true) noexcept override {
        #ifndef NDEBUG
        if (align && (addr & 3))
            assert(false);
        #endif
        u8* p = &data[addr - base];
        if (bankEndian == Endian::Big) {
            p[0] = (val >> 24) & 0xFF;
            p[1] = (val >> 16) & 0xFF;
            p[2] = (val >>  8) & 0xFF;
            p[3] =  val        & 0xFF;
        } else {
            p[0] =  val        & 0xFF;
            p[1] = (val >>  8) & 0xFF;
            p[2] = (val >> 16) & 0xFF;
            p[3] = (val >> 24) & 0xFF;
        }
    }

};



class MCtrl {
public:
    MCtrl() : instance_id_(next_instance_id()) {}

    uint64_t instance_id() const noexcept { return instance_id_; }
    

    template<typename BankType, typename... Args>
    BankType& attach_bank(Args&&... args) {
        auto bank = std::make_unique<BankType>(std::forward<Args>(args)...);

        for (const auto& existing : banks) {
            if (ranges_overlap_u64(bank->get_base(), bank->get_end_exclusive(),
                               existing->get_base(), existing->get_end_exclusive())) {
                throw std::runtime_error("Bank region overlaps with existing memory");
            }
        }

        banks.push_back(std::move(bank));
        ++generation_;

        auto& b = dynamic_cast<BankType&>(*banks.back());
        return b;
    }

    u8 read8(u32 addr) const {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Read, addr, 1);
        #endif
        auto b = find_bank_or_null(addr);
        if(!b)
            throw std::out_of_range("[MCTRL] Out of range: 0x" + to_hex(addr));
        return b->read8(addr);
    }

    u16 read16(u32 addr, bool align = true) const {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Read, addr, 2);
        #endif
        auto b = find_bank_or_null(addr);
        if(!b)
            throw std::out_of_range("[MCTRL] Out of range: 0x" + to_hex(addr));
        return b->read16(addr, align);
    }

    u32 read32(u32 addr, bool align = true) const {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Read, addr, 4);
        #endif
        auto b = find_bank_or_null(addr);
        if(!b)
            throw std::out_of_range("[MCTRL] Out of range: 0x" + to_hex(addr));
        return b->read32(addr, align);
    }

    u64 read64(u32 addr, bool align = true) const {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Read, addr, 8);
        #endif
        auto b = find_bank_or_null(addr);
        if(!b)
            throw std::out_of_range("[MCTRL] Out of range: 0x" + to_hex(addr));
        return b->read64(addr, align);
    }

    void write8(u32 addr, u8 val) {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Write, addr, 1);
        #endif
        auto b = find_bank_or_null(addr);
        if(!b)
            throw std::out_of_range("[MCTRL] Out of range: 0x" + to_hex(addr));
        
        b->write8(addr, val);
    }

    void write16(u32 addr, u16 val, bool align = true) {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Write, addr, 2);
        #endif
        auto b = find_bank_or_null(addr);
        if(!b)
            throw std::out_of_range("[MCTRL] Out of range: 0x" + to_hex(addr));
        
        b->write16(addr, val, align);
    }

    void write32(u32 addr, u32 val, bool align = true) {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Write, addr, 4);
        #endif
        
        auto b = find_bank_or_null(addr);
        if(!b)
            throw std::out_of_range("[MCTRL] Out of range: 0x" + to_hex(addr));
        b->write32(addr, val, align);
    }

    void write64(u32 addr, u64 val, bool align = true) {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Write, addr, 8);
        #endif
        auto b = find_bank_or_null(addr);
        if(!b)
            throw std::out_of_range("[MCTRL] Out of range: 0x" + to_hex(addr));
        
        b->write64(addr, val, align);
    }

    MemBusStatus try_read8(u32 addr, u32& out) const noexcept {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Read, addr, 1);
        #endif
        
        auto* b = find_bank_or_null(addr);
        if (!b) return MemBusStatus::NoDevice;
        // For now keep the existing IMemoryBank::read32 implementation
        out = b->read8(addr);
        return MemBusStatus::Ok;
    }

    MemBusStatus try_read16(u32 addr, u32& out, bool align = true) const noexcept {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Read, addr, 2);
        #endif
        
        auto* b = find_bank_or_null(addr);
        if (!b) return MemBusStatus::NoDevice;
        // For now keep the existing IMemoryBank::read32 implementation
        out = b->read16(addr, align);
        return MemBusStatus::Ok;
    }

    MemBusStatus try_read32(u32 addr, u32& out, bool align = true) const noexcept {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Read, addr, 4);
        #endif
        
        auto* b = find_bank_or_null(addr);
        if (!b) return MemBusStatus::NoDevice;
        // For now keep the existing IMemoryBank::read32 implementation
        out = b->read32(addr, align);
        return MemBusStatus::Ok;
    }

    MemBusStatus try_read64(u32 addr, u32& out, bool align = true) const noexcept {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Read, addr, 8);
        #endif
        
        auto* b = find_bank_or_null(addr);
        if (!b) return MemBusStatus::NoDevice;
        // For now keep the existing IMemoryBank::read32 implementation
        out = b->read64(addr, align);
        return MemBusStatus::Ok;
    }

    MemBusStatus try_write8(u32 addr, u32 value) noexcept {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Write, addr, 1);
        #endif
        
        auto* b = find_bank_or_null(addr);
        if (!b) return MemBusStatus::NoDevice;
        b->write8(addr, value);
        return MemBusStatus::Ok;
    }

    MemBusStatus try_write16(u32 addr, u32 value, bool align = true) noexcept {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Write, addr, 2);
        #endif
        
        
        auto* b = find_bank_or_null(addr);
        if (!b) return MemBusStatus::NoDevice;
        b->write16(addr, value, align);
        return MemBusStatus::Ok;
    }

    MemBusStatus try_write32(u32 addr, u32 value, bool align = true) noexcept {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Write, addr, 4);
        #endif
        


        auto* b = find_bank_or_null(addr);
        if (!b) return MemBusStatus::NoDevice;
        b->write32(addr, value, align);
        return MemBusStatus::Ok;
    }

    MemBusStatus try_write64(u32 addr, u32 value, bool align = true) noexcept {
        #ifdef PROFILE_MEM_ACCESS
        g_memprof.record(MemAccessProfiler::Op::Write, addr, 8);
        #endif
        
        auto* b = find_bank_or_null(addr);
        if (!b) return MemBusStatus::NoDevice;
        b->write64(addr, value, align);
        return MemBusStatus::Ok;
    }

    AtomicResult atomic_ldstub8(uint32_t paddr) noexcept {
        auto* bank = find_bank_or_null(paddr);
        if (!bank) return {false, 0};
        return bank->atomic_ldstub8(paddr);
    }

    AtomicResult atomic_swap32(uint32_t paddr, uint32_t newv) noexcept {
        auto* bank = find_bank_or_null(paddr);
        if (!bank) return {false, 0};
        return bank->atomic_swap32(paddr, newv);
    }

    /*
    AtomicResult atomic_casa32(uint32_t paddr, uint32_t expected, uint32_t desired, bool* swapped) noexcept {
        auto* bank = find_bank_or_null(paddr);
        if (!bank) { if (swapped) *swapped = false; return {false, 0}; }
        return bank->atomic_casa32(paddr, expected, desired, swapped);
    }
    */

    IMemoryBank* get_bank(u32 paddr) {
        return find_bank_or_null(paddr);
    }

    // Thread local cache of last bank, since we are often hammering the
    // same area
    IMemoryBank* find_bank_or_null(u32 addr) const noexcept {
        struct Cache {
            uint64_t owner_id = 0;
            uint64_t gen = 0;
            IMemoryBank* b = nullptr;
            u32 start = 0, size = 0;
        };

        thread_local Cache c;

        const uint64_t my_id = instance_id();

        // Single predictable guard
        if (__builtin_expect(c.owner_id != my_id || c.gen != generation_, 0)) {
            //fprintf(stderr, "cache invalidate: this=%ld old_owner=%ld old_gen=%llu new_gen=%llu\n",
            //my_id, c.owner_id,
            //(unsigned long long)c.gen,
            //(unsigned long long)generation_);
            c.owner_id = my_id;
            c.gen = generation_;
            c.b = nullptr;
            c.start = c.size = 0;
        }

        #ifndef NDEBUG
        if (c.b) {
            const u32 b0 = c.b->get_base();
            const u32 bs = (u32)c.b->get_size();
            if (b0 != c.start || bs != c.size) {
                fprintf(stderr,
                    "CACHE CORRUPT: addr=%08x this=%p gen=%llu "
                    "c.b=%p b.base=%08x b.size=%08x  cached.base=%08x cached.size=%08x\n",
                    addr, (void*)this, (unsigned long long)c.gen,
                    (void*)c.b, b0, bs, c.start, c.size);
                std::abort();
            }
        }
        #endif
        
        // Super-fast hit check without virtual call:
        if (c.b && addr >= c.start && (addr - c.start < c.size)) {        
            return c.b;
        }
        //if (c.b && c.b->contains(addr))
        //    return c.b;
        

        for (const auto& bank : banks) {
            if (bank->contains(addr)) {
                auto p = bank.get();
                c.b = p;
                c.start = p->get_base();
                c.size = p->get_size();
                #ifndef NDEBUG
                if (!(addr >= c.start && (addr - c.start) < c.size)) {
                    fprintf(stderr, "BAD RANGE: addr=%08x base=%08x size=%08x bank=%p\n",
                            addr, c.start, c.size, (void*)c.b);
                    std::abort();
                }
                #endif
                return p;
            }
        }
        c.b = nullptr;
        c.start = 0;
        c.size = 0;
        return nullptr;
    }

    IMemoryBank* find_bank(u32 addr) const {
        if (auto* b = find_bank_or_null(addr)) return b;
        throw std::out_of_range("No bank for addr");
    }

    void clear_banks() {
       banks.clear();
        ++generation_;
    }

    void debug_list_banks() const {
        std::cout << "\n[Memory Map Overview]\n";
        std::cout << "Idx |      Start -       End  |   Size   | Access | Endian | Type\n";
        std::cout << "----+-------------------------+----------+--------+--------+-------------------\n";

        for (size_t i = 0; i < banks.size(); ++i) {
            const auto& bank = banks[i];
            u32 start = bank->get_base();
            u64 end   = bank->get_end_exclusive() - 1;
            size_t size    = end - start + 1;
            std::string size_str = (size >= (1024 * 1024)) ?
                std::to_string(size / (1024 * 1024)) + " MB" :
                (size >= 1024) ? std::to_string(size / 1024) + " KB" :
                std::to_string(size) + " B";

            // Try-write to detect read-only status (non-destructive)
            bool writable = true;
            try {
                bank->write32(start, bank->read32(start));
            } catch (...) {
                writable = false;
            }

            std::string access = writable ? "R/W" : "RO";
            std::string endian = (bank->get_endian() == Endian::Big) ? "Big" : "Little";
            std::string type = demangle(typeid(*bank).name());

            std::cout << std::setw(3) << i << " | 0x"
                    << std::hex << std::setw(8) << std::setfill('0') << start
                    << " - 0x" << std::setw(8) << end
                    << " | " << std::right << std::setw(8) << std::setfill(' ') << std::left << size_str
                    << " | " << std::setw(6) << access
                    << " | " << std::setw(6) << endian
                    << " | " << type << "\n";
        }

        std::cout << std::dec; // Reset formatting
    }
private:
    std::vector<std::unique_ptr<IMemoryBank>> banks;
    u64 generation_ = 1;
    uint64_t instance_id_;

    std::mutex mtx = {};

    static uint64_t next_instance_id() {
        static std::atomic<uint64_t> g{1};
        return g.fetch_add(1, std::memory_order_relaxed);
    }

#ifdef PROFILE_MEM_ACCESS
private:
    mutable MemAccessProfiler g_memprof{12}; // 4KiB

public:
    void print_profile() {
        g_memprof.dump(std::cout, 10);
    } 
private:
#endif

    static bool ranges_overlap_u64(u32 a_start, u64 a_end,
                               u32 b_start, u64 b_end) {
        return (u64(a_start) < b_end) && (u64(b_start) < a_end);
    }

    static std::string to_hex(u32 val) {
        char buf[11];
        snprintf(buf, sizeof(buf), "%08X", val);
        return buf;
    }
};




