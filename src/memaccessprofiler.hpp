#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <ostream>
#include <iomanip>

class MemAccessProfiler {
public:
    enum class Op : uint8_t { Read, Write };

    struct Totals {
        uint64_t reads = 0, writes = 0;
        uint64_t read_bytes = 0, write_bytes = 0;
        uint64_t r_by_size[9] = {0}; // index by bytes (1,2,4,8) else 0
        uint64_t w_by_size[9] = {0};
    };

    struct PageStat {
        uint64_t reads = 0, writes = 0;
        uint64_t read_bytes = 0, write_bytes = 0;

        uint64_t total_ops()   const { return reads + writes; }
        uint64_t total_bytes() const { return read_bytes + write_bytes; }
    };

    struct RangeStat {
        uint32_t start_addr = 0;
        uint32_t end_addr_inclusive = 0;
        PageStat agg{};
    };

    explicit MemAccessProfiler(uint32_t page_shift = 12) // 4 KiB default
        : page_shift_(page_shift),
          page_size_(1u << page_shift_),
          page_mask_(~(page_size_ - 1u)) {}

    inline void record(Op op, uint32_t addr, uint32_t size_bytes) {
        // totals
        if (op == Op::Read) {
            totals_.reads++;
            totals_.read_bytes += size_bytes;
            bump_size(totals_.r_by_size, size_bytes);
        } else {
            totals_.writes++;
            totals_.write_bytes += size_bytes;
            bump_size(totals_.w_by_size, size_bytes);
        }

        // per-page stats
        const uint32_t page = addr >> page_shift_;
        auto &ps = pages_[page];
        if (op == Op::Read) {
            ps.reads++;
            ps.read_bytes += size_bytes;
        } else {
            ps.writes++;
            ps.write_bytes += size_bytes;
        }
    }

    const Totals& totals() const { return totals_; }

    // ---- Reporting helpers ----

    // Top N individual pages by ops or bytes
    std::vector<std::pair<uint32_t, PageStat>> top_pages_by_ops(size_t n = 10) const {
        return top_pages(n, /*by_bytes=*/false);
    }
    std::vector<std::pair<uint32_t, PageStat>> top_pages_by_bytes(size_t n = 10) const {
        return top_pages(n, /*by_bytes=*/true);
    }

    // Merge contiguous pages into ranges and rank them.
    // Great for "what regions are hot?" rather than single pages.
    std::vector<RangeStat> top_ranges_by_ops(size_t n = 10) const {
        return top_ranges(n, /*by_bytes=*/false);
    }
    std::vector<RangeStat> top_ranges_by_bytes(size_t n = 10) const {
        return top_ranges(n, /*by_bytes=*/true);
    }

    // Print a useful summary to a stream
    void dump(std::ostream& os, size_t top_n = 10) const {
        os << "MemAccessProfiler\n";
        os << "---------------------------------------------\n";
        os << "Totals:\n";
        os << "  Reads : " << totals_.reads  << " (" << totals_.read_bytes  << " bytes)\n";
        os << "  Writes: " << totals_.writes << " (" << totals_.write_bytes << " bytes)\n";
        os << "  Total : " << (totals_.reads + totals_.writes)
           << " (" << (totals_.read_bytes + totals_.write_bytes) << " bytes)\n";

        auto print_sizes = [&](const char* label, const uint64_t by_size[9]) {
            os << "  " << label << " by size: "
               << "1B=" << by_size[1] << ", "
               << "2B=" << by_size[2] << ", "
               << "4B=" << by_size[4] << ", "
               << "8B=" << by_size[8] << "\n";
        };
        print_sizes("Reads ", totals_.r_by_size);
        print_sizes("Writes", totals_.w_by_size);

        os << "\nTop " << top_n << " pages by ops (page_size=" << page_size_ << "):\n";
        auto tp = top_pages_by_ops(top_n);
        print_pages(os, tp);

        os << "\nTop " << top_n << " ranges by ops (merged contiguous pages):\n";
        auto tr = top_ranges_by_ops(top_n);
        print_ranges(os, tr);

        os << "\nTop " << top_n << " pages by bytes:\n";
        auto tpb = top_pages_by_bytes(top_n);
        print_pages(os, tpb);

        os << "\nTop " << top_n << " ranges by bytes:\n";
        auto trb = top_ranges_by_bytes(top_n);
        print_ranges(os, trb);
    }

private:
    uint32_t page_shift_;
    uint32_t page_size_;
    uint32_t page_mask_;

    Totals totals_{};
    std::unordered_map<uint32_t, PageStat> pages_; // key = page number

    static inline void bump_size(uint64_t arr[9], uint32_t sz) {
        if (sz == 1 || sz == 2 || sz == 4 || sz == 8) arr[sz]++;
        else arr[0]++; // unexpected size
    }

    std::vector<std::pair<uint32_t, PageStat>> top_pages(size_t n, bool by_bytes) const {
        std::vector<std::pair<uint32_t, PageStat>> v;
        v.reserve(pages_.size());
        for (const auto& kv : pages_) v.push_back(kv);

        auto key = [&](const PageStat& ps) -> uint64_t {
            return by_bytes ? ps.total_bytes() : ps.total_ops();
        };

        std::partial_sort(v.begin(),
                          v.begin() + std::min(n, v.size()),
                          v.end(),
                          [&](const auto& a, const auto& b) {
                              return key(a.second) > key(b.second);
                          });

        if (v.size() > n) v.resize(n);
        return v;
    }

    std::vector<RangeStat> top_ranges(size_t n, bool by_bytes) const {
        if (pages_.empty()) return {};

        // Collect & sort page numbers
        std::vector<uint32_t> page_keys;
        page_keys.reserve(pages_.size());
        for (const auto& kv : pages_) page_keys.push_back(kv.first);
        std::sort(page_keys.begin(), page_keys.end());

        // Merge contiguous pages
        std::vector<RangeStat> ranges;
        RangeStat cur{};
        bool have = false;

        auto addr_of_page = [&](uint32_t page) -> uint32_t {
            return page << page_shift_;
        };

        for (uint32_t p : page_keys) {
            const auto& ps = pages_.at(p);
            if (!have) {
                have = true;
                cur.start_addr = addr_of_page(p);
                cur.end_addr_inclusive = addr_of_page(p) + (page_size_ - 1);
                cur.agg = ps;
                continue;
            }

            uint32_t expected_next_start = (cur.end_addr_inclusive + 1);
            uint32_t p_start = addr_of_page(p);

            if (p_start == expected_next_start) {
                // extend
                cur.end_addr_inclusive = p_start + (page_size_ - 1);
                add_into(cur.agg, ps);
            } else {
                ranges.push_back(cur);
                cur.start_addr = p_start;
                cur.end_addr_inclusive = p_start + (page_size_ - 1);
                cur.agg = ps;
            }
        }
        if (have) ranges.push_back(cur);

        auto key = [&](const PageStat& ps) -> uint64_t {
            return by_bytes ? ps.total_bytes() : ps.total_ops();
        };

        std::partial_sort(ranges.begin(),
                          ranges.begin() + std::min(n, ranges.size()),
                          ranges.end(),
                          [&](const RangeStat& a, const RangeStat& b) {
                              return key(a.agg) > key(b.agg);
                          });

        if (ranges.size() > n) ranges.resize(n);
        return ranges;
    }

    static inline void add_into(PageStat& a, const PageStat& b) {
        a.reads += b.reads;
        a.writes += b.writes;
        a.read_bytes += b.read_bytes;
        a.write_bytes += b.write_bytes;
    }

    static void print_pages(std::ostream& os,
                            const std::vector<std::pair<uint32_t, PageStat>>& tp) {
        os << "  " << std::left
           << std::setw(12) << "Page"
           << std::setw(12) << "Addr"
           << std::setw(10) << "Ops"
           << std::setw(10) << "Bytes"
           << std::setw(10) << "R"
           << std::setw(10) << "W"
           << "\n";

        for (auto& [page, ps] : tp) {
            uint32_t addr = page << 12; // NOTE: if you change page_shift_, adjust this display below
            os << "  " << std::left
               << std::setw(12) << page
               << "0x" << std::hex << std::setw(10) << addr << std::dec
               << std::setw(10) << ps.total_ops()
               << std::setw(10) << ps.total_bytes()
               << std::setw(10) << ps.reads
               << std::setw(10) << ps.writes
               << "\n";
        }
    }

    static void print_ranges(std::ostream& os, const std::vector<RangeStat>& tr) {
        os << "  " << std::left
           << std::setw(12) << "Start"
           << std::setw(12) << "End"
           << std::setw(10) << "Ops"
           << std::setw(10) << "Bytes"
           << std::setw(10) << "R"
           << std::setw(10) << "W"
           << "\n";

        for (auto& r : tr) {
            os << "  0x" << std::hex << std::setw(10) << r.start_addr
               << " 0x" << std::setw(10) << r.end_addr_inclusive
               << std::dec
               << std::setw(10) << r.agg.total_ops()
               << std::setw(10) << r.agg.total_bytes()
               << std::setw(10) << r.agg.reads
               << std::setw(10) << r.agg.writes
               << "\n";
        }
    }
};
