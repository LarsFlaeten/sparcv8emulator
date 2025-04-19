// Memory controller, and associated memory banks

#include <cstdint>
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <algorithm>


enum class Endian {
    Little,
    Big
};

class IMemoryBank {
public:
    IMemoryBank(Endian endian = Endian::Big) : bankEndian(endian) {}
    virtual ~IMemoryBank() = default;

    virtual bool contains(uint32_t addr) const = 0;
    virtual uint8_t read8(uint32_t addr) const = 0;
    virtual void write8(uint32_t addr, uint8_t val) = 0;
    virtual uint32_t get_base() const = 0;
    virtual uint32_t get_limit() const = 0;
    virtual void lock() {}; // For lockable banks (ROM-type)

    virtual uint16_t read16(uint32_t addr, bool align = true) const {
        if (align && (addr & 1))
            throw std::runtime_error("Unaligned 16-bit read at 0x" + to_hex(addr));
        uint8_t hi = read8(addr);
        uint8_t lo = read8(addr + 1);
        return (bankEndian == Endian::Big) ? (hi << 8 | lo) : (lo << 8 | hi);
    }

    virtual void write16(uint32_t addr, uint16_t val, bool align = true) {
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

    virtual uint32_t read32(uint32_t addr, bool align = true) const {
        if (align && (addr & 3))
            throw std::runtime_error("Unaligned 32-bit read at 0x" + to_hex(addr));
        uint8_t b0 = read8(addr);
        uint8_t b1 = read8(addr + 1);
        uint8_t b2 = read8(addr + 2);
        uint8_t b3 = read8(addr + 3);
        if (bankEndian == Endian::Big) {
            return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
        } else {
            return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
        }
    }

    virtual void write32(uint32_t addr, uint32_t val, bool align = true) {
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

    Endian get_endian() const { return bankEndian; }

protected:
    Endian bankEndian;

    static std::string to_hex(uint32_t val) {
        char buf[11];
        snprintf(buf, sizeof(buf), "%08X", val);
        return buf;
    }
};


template <size_t N>
class RomBank : public IMemoryBank {
public:
    RomBank(uint32_t base, Endian endian = Endian::Big) : IMemoryBank(endian), base(base), writeable(true) {
        data.fill(0);
    }

    bool contains(uint32_t addr) const override {
        return addr >= base && addr < base + N;
    }

    uint8_t read8(uint32_t addr) const override {
        check_range(addr);
        return data[addr - base];
    }

    void write8(uint32_t addr, uint8_t val) override {
        if(!writeable)
            throw std::runtime_error("Write to read-only memory");
        else {
            check_range(addr);
            data[addr - base] = val;
        }
    }

    uint32_t get_base() const override { return base; }
    uint32_t get_limit() const override { return base + N; }
    void lock() override { writeable = false; }

private:
    uint32_t base;
    bool writeable;
    std::array<uint8_t, N> data;

    void check_range(uint32_t addr) const {
        if (!contains(addr))
            throw std::out_of_range("ROM access out of range");
    }
};

class RamBank : public IMemoryBank {
public:
    RamBank(uint32_t base, size_t size, Endian endian = Endian::Big)
        : IMemoryBank(endian), base(base), size(size), data(size, 0) {}

    bool contains(uint32_t addr) const override {
        return addr >= base && addr < base + size;
    }

    uint8_t read8(uint32_t addr) const override {
        check_range(addr);
        return data[addr - base];
    }

    void write8(uint32_t addr, uint8_t val) override {
        check_range(addr);
        data[addr - base] = val;
    }

    uint32_t get_base() const override { return base; }
    uint32_t get_limit() const override { return base + size; }


private:
    uint32_t base;
    size_t size;
    std::vector<uint8_t> data;

    void check_range(uint32_t addr) const {
        if (!contains(addr))
            throw std::out_of_range("RAM access out of range");
    }
};


class MCtrl {
public:
    template<typename BankType, typename... Args>
    void attach_bank(Args&&... args) {
        auto bank = std::make_unique<BankType>(std::forward<Args>(args)...);

        for (const auto& existing : banks) {
            if (ranges_overlap(bank->get_base(), bank->get_limit(),
                               existing->get_base(), existing->get_limit())) {
                throw std::runtime_error("Bank region overlaps with existing memory");
            }
        }

        banks.push_back(std::move(bank));
    }

    uint8_t read8(uint32_t addr) const {
        return find_bank(addr)->read8(addr);
    }

    uint16_t read16(uint32_t addr, bool align = true) const {
        return find_bank(addr)->read16(addr, align);
    }

    uint32_t read32(uint32_t addr, bool align = true) const {
        return find_bank(addr)->read32(addr, align);
    }

    void write8(uint32_t addr, uint8_t val) {
        find_bank(addr)->write8(addr, val);
    }

    void write16(uint32_t addr, uint16_t val, bool align = true) {
        find_bank(addr)->write16(addr, val, align);
    }

    void write32(uint32_t addr, uint32_t val, bool align = true) {
        find_bank(addr)->write32(addr, val, align);
    }

    IMemoryBank* find_bank(uint32_t addr) const {
        for (const auto& bank : banks) {
            if (bank->contains(addr)) return bank.get();
        }
        throw std::out_of_range("No memory mapped at address 0x" + to_hex(addr));
    }
private:
    std::vector<std::unique_ptr<IMemoryBank>> banks;

    

    static bool ranges_overlap(uint32_t a_start, uint32_t a_end,
                                uint32_t b_start, uint32_t b_end) {
        return (a_start < b_end) && (b_start < a_end);
    }

    static std::string to_hex(uint32_t val) {
        char buf[11];
        snprintf(buf, sizeof(buf), "%08X", val);
        return buf;
    }
};