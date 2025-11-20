#pragma once
// Memory controller, and associated memory banks

#include <cstdint>
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <iomanip>
#include <string>

#include "../common.h"

std::string demangle(const char* name);

enum class Endian {
    Little,
    Big
};

class IMemoryBank {
public:
    IMemoryBank(Endian endian = Endian::Big) : bankEndian(endian) {}
    virtual ~IMemoryBank() = default;

    virtual bool contains(u32 addr) const = 0;
    virtual u8 read8(u32 addr) const = 0;
    virtual void write8(u32 addr, u8 val) = 0;
    virtual u32 get_base() const = 0;
    virtual u32 get_limit() const = 0;
    virtual void lock() {}; // For lockable banks (ROM-type)
    
    // Gets a pointer to the host data buffer
    virtual u32* get_ptr() = 0;

    virtual u16 read16(u32 addr, bool align = true) const {
        if (align && (addr & 1))
            throw std::runtime_error("Unaligned 16-bit read at 0x" + to_hex(addr));
        u8 hi = read8(addr);
        u8 lo = read8(addr + 1);
        return (bankEndian == Endian::Big) ? (hi << 8 | lo) : (lo << 8 | hi);
    }

    virtual void write16(u32 addr, u16 val, bool align = true) {
        if (align && (addr & 1))
            throw std::runtime_error("Unaligned 16-bit write at 0x" + to_hex(addr));
        if (bankEndian == Endian::Big) {
            write8(addr,     (val >> 8) & 0xFF);
            write8(addr + 1,  val       & 0xFF);
        } else {
            write8(addr,      val       & 0xFF);
            write8(addr + 1, (val >> 8) & 0xFF);
        }
    }

    virtual u32 read32(u32 addr, bool align = true) const {
        if (align && (addr & 3))
            throw std::runtime_error("Unaligned 32-bit read at 0x" + to_hex(addr));
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
        if (align && (addr & 3))
            throw std::runtime_error("Unaligned 32-bit write at 0x" + to_hex(addr));
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
        if (align && (addr & 7))
            throw std::runtime_error("Unaligned 64-bit read at 0x" + to_hex(addr));
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
        if (align && (addr & 7))
            throw std::runtime_error("Unaligned 64-bit write at 0x" + to_hex(addr));
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
    Endian get_endian() const { return bankEndian; }

protected:
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
        return addr >= base && addr < base + N;
    }

    u8 read8(u32 addr) const override {
        check_range(addr);
        return data[addr - base];
    }

    void write8(u32 addr, u8 val) override {
        if(!writeable)
            throw std::runtime_error("Write to read-only memory");
        else {
            check_range(addr);
            data[addr - base] = val;
        }
    }

    u32 get_base() const override { return base; }
    u32 get_limit() const override { return base + N; }
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
        : IMemoryBank(endian), base(base), size(size), data(size, 0) {}

    bool contains(u32 addr) const override {
        return addr >= base && addr < base + size;
    }

    u8 read8(u32 addr) const override {
        check_range(addr);
        return data[addr - base];
    }

    void write8(u32 addr, u8 val) override {
        check_range(addr);
        data[addr - base] = val;
    }

    u32 get_base() const override { return base; }
    u32 get_limit() const override { return base + size; }

    u32* get_ptr() override { return reinterpret_cast<u32*>(data.data());}

private:
    u32 base;
    size_t size;
    std::vector<u8> data;

    void check_range(u32 addr) const {
        if (!contains(addr))
            throw std::out_of_range("RAM access out of range");
    }
};



class MCtrl {
public:
    template<typename BankType, typename... Args>
    BankType& attach_bank(Args&&... args) {
        auto bank = std::make_unique<BankType>(std::forward<Args>(args)...);

        for (const auto& existing : banks) {
            if (ranges_overlap(bank->get_base(), bank->get_limit(),
                               existing->get_base(), existing->get_limit())) {
                throw std::runtime_error("Bank region overlaps with existing memory");
            }
        }

        banks.push_back(std::move(bank));

        auto& b = dynamic_cast<BankType&>(*banks.back());
        return b;
    }

    u8 read8(u32 addr) const {
        return find_bank(addr)->read8(addr);
    }

    u16 read16(u32 addr, bool align = true) const {
        return find_bank(addr)->read16(addr, align);
    }

    u32 read32(u32 addr, bool align = true) const {
        return find_bank(addr)->read32(addr, align);
    }

    u64 read64(u32 addr, bool align = true) const {
        return find_bank(addr)->read64(addr, align);
    }

    void write8(u32 addr, u8 val) {
        find_bank(addr)->write8(addr, val);
    }

    void write16(u32 addr, u16 val, bool align = true) {
        find_bank(addr)->write16(addr, val, align);
    }

    void write32(u32 addr, u32 val, bool align = true) {
        
        find_bank(addr)->write32(addr, val, align);
    }

    void write64(u32 addr, u64 val, bool align = true) {
        find_bank(addr)->write64(addr, val, align);
    }

    IMemoryBank* find_bank(u32 addr) const {
        for (const auto& bank : banks) {
            if (bank->contains(addr)) return bank.get();
        }
        //debug_list_banks();
        throw std::out_of_range("No memory mapped at addr " + to_hex(addr));
    }

    void debug_read8(u32 addr) const {
        try {
            const IMemoryBank* bank = find_bank(addr);
            u8 val = bank->read8(addr);

            std::cout << "[debug_read8]  Addr: 0x" << std::hex << addr
                    << " -> Value: 0x" << +val
                    << " (Bank: 0x" << bank->get_base()
                    << " - 0x" << (bank->get_limit() - 1)
                    << ")\n";
        } catch (const std::exception& e) {
            std::cerr << "[debug_read8]  Error at 0x" << std::hex << addr
                    << ": " << e.what() << "\n";
        }
    }

    void debug_read16(u32 addr, bool align = true) const {
        try {
            const IMemoryBank* bank = find_bank(addr);
            u16 val = bank->read16(addr, align);
            Endian endian = bank->get_endian();

            std::cout << "[debug_read16] Addr: 0x" << std::hex << addr
                    << " -> Value: 0x" << val
                    << " (Bank: 0x" << bank->get_base()
                    << " - 0x" << (bank->get_limit() - 1)
                    << ", " << (endian == Endian::Big ? "Big" : "Little") << "-endian)\n";
        } catch (const std::exception& e) {
            std::cerr << "[debug_read16] Error at 0x" << std::hex << addr
                    << ": " << e.what() << "\n";
        }
    }

    void debug_read32(u32 addr, bool align = true) const {
        try {
            const IMemoryBank* bank = find_bank(addr);
            u32 val = bank->read32(addr, align);
            Endian endian = bank->get_endian();

            std::cout << "[debug_read32] Addr: 0x" << std::hex << addr
                    << " -> Value: 0x" << val
                    << " (Bank: 0x" << bank->get_base()
                    << " - 0x" << (bank->get_limit() - 1)
                    << ", " << (endian == Endian::Big ? "Big" : "Little") << "-endian)\n";
        } catch (const std::exception& e) {
            std::cerr << "[debug_read32] Error at 0x" << std::hex << addr
                    << ": " << e.what() << "\n";
        }
    }
    void debug_read64(u32 addr, bool align = true) const {
        try {
            const IMemoryBank* bank = find_bank(addr);
            uint64_t val = bank->read64(addr, align);
            Endian endian = bank->get_endian();

            std::cout << "[debug_read64] Addr: 0x" << std::hex << addr
                    << " -> Value: 0x" << std::setw(16) << std::setfill('0') << val
                    << " (Bank: 0x" << bank->get_base()
                    << " - 0x" << (bank->get_limit() - 1)
                    << ", " << (endian == Endian::Big ? "Big" : "Little") << "-endian)\n";
        } catch (const std::exception& e) {
            std::cerr << "[debug_read64] Error at 0x" << std::hex << addr
                    << ": " << e.what() << "\n";
        }
    }

    void debug_list_banks() const {
        std::cout << "\n[Memory Map Overview]\n";
        std::cout << "Idx |      Start -       End  |   Size   | Access | Endian | Type\n";
        std::cout << "----+-------------------------+----------+--------+--------+-------------------\n";

        for (size_t i = 0; i < banks.size(); ++i) {
            const auto& bank = banks[i];
            u32 start = bank->get_base();
            u32 end   = bank->get_limit() - 1;
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

    

    static bool ranges_overlap(u32 a_start, u32 a_end,
                                u32 b_start, u32 b_end) {
        return (a_start < b_end) && (b_start < a_end);
    }

    static std::string to_hex(u32 val) {
        char buf[11];
        snprintf(buf, sizeof(buf), "%08X", val);
        return buf;
    }



};