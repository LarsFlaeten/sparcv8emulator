// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <array>
#include <atomic>
#include <shared_mutex>


#include "../peripherals/MCTRL.h"
#include "CPU_defines.h"

#if defined(PROFILE_LOCKS)
#include "../mutexprofiler.hpp"
#endif

/* Declare facilities for detecting and dealing with different byteorder */
#include <endian.h>
#define LITTLE_ENDIAN_HOST  (__BYTE_ORDER == __LITTLE_ENDIAN)
#define LITTLE_ENDIAN_SLAVE 0 /* 0=big endian */
#define NOfprintf(...) /**/
#define CROSS_ENDIAN (LITTLE_ENDIAN_HOST != LITTLE_ENDIAN_SLAVE)

// Defined in MMU.cpp; set to true before CPU threads launch when n_cpus > 1.
// When false, MemAccess<intent_store> skips the per-bank mutex (no contention possible).
extern bool g_smp_mode;

#define SRMMU_PRIV 0x1c
#define SRMMU_VALID 0x02
#define SRMMU_CACHE 0x80
#define SRMMU_INVALID 0x0
#define SRMMU_ET_PTD 0x1
#define SRMMU_ET_PTE 0x2
#define SRMMU_ET_MASK 0x3
#define SRMMU_ACC_S_ALL	(0x7 << 2)
#define SRMMU_ACC_U_ALL	(0x3 << 2)

enum intent { intent_load=0, intent_store=1, intent_execute=2 };


constexpr int TLB_ENTRIES = 16; // emulate LEON3 default

class TLB {
public:
    TLB();

    bool lookup(u32 context, u32 vaddr, u32& pte_out, u8& level_out) const;
    void insert(u32 context, u32 vaddr, u8 level, u32 pte);
    void invalidate_strict(u32 context, u32 vaddr);
    void flush(); // flush all entries

    void debug_dump(const std::string& label = "TLB") const;

#ifdef PERF_STATS
    struct TLBStats {
        std::atomic<uint64_t> hits{0};
        std::atomic<uint64_t> misses{0};
    };
    mutable TLBStats stats_;
public:
    const TLBStats& get_stats() const { return stats_; }
private:
#endif

    struct Entry {
        u32 vaddr_tag = 0;
        u32 mask = 0;
        u32 pte = 0;
        u32 context = 0;
        u8 level = 0;
    };

    std::array<Entry, TLB_ENTRIES> entries;
    u16 next_victim_ = 0;

    // 1-entry micro-cache for fast repeated lookups (per-CPU, mutable for const lookup)
    mutable u32 mc_context_   = ~0u;
    mutable u32 mc_vaddr_tag_ = 0;
    mutable u32 mc_mask_      = 0;
    mutable u32 mc_pte_       = 0;
    mutable u8  mc_level_     = 0;
public:
    static bool is_valid(u32 pte);
};



struct MMUFault {
    u32 far;      // faulting VA (full)
    u8  ft;       // fault type (SRMMU)
    u8  at;       // access type (0..7)
    u8  level;    // 0..3 (table walk level; 0 if N/A)
    bool fav;     // Fault Address Valid
};

struct MMUTranslateResult {
    bool ok;
    u32 pa;
    u8 level;
    u32 pte;
};


class MMU {
    MCtrl& mctrl;
    // MMUR REGS
    u32 control_reg;
    u32 ccr, iccr, dccr;
    u32 ctx_tbl_ptr;
    u32 ctx_n, last_ctx_n;
    u32 fault_status_reg;
    u32 fault_address_reg;

    // L0 instruction fetch cache: skip translate_va + get_bank for consecutive
    // fetches within the same virtual page (common for sequential execution).
    // page_data points to host byte 0 of the mapped virtual page (big-endian).
    struct FetchCache {
        u32       vpage     = ~0u;    // virtual page number (vaddr >> 12); ~0u = invalid
        u32       ctx       = ~0u;    // MMU context at fill time
        bool      super     = false;  // supervisor mode at fill time
        const u8* page_data = nullptr;// host ptr to byte 0 of this virtual page
    };
    FetchCache fetch_cache_;

    // L0 data load cache: 4-entry direct-mapped (indexed by vpage & 3).
    // Same hit-path cost as 1-entry but handles 4 simultaneously live pages.
    static constexpr int DC_WAYS = 4;  // must be power of 2
    struct DataCache {
        u32       vpage     = ~0u;
        u32       ctx       = ~0u;
        bool      super     = false;
        const u8* page_data = nullptr;
    };
    DataCache data_cache_[DC_WAYS];

    // L0 store cache: same layout but with writable page_data.
    // Used in single-CPU mode to bypass translate_va + virtual write on stores.
    struct StoreCache {
        u32  vpage     = ~0u;
        u32  ctx       = ~0u;
        bool super     = false;
        u8*  page_data = nullptr;
    };
    StoreCache store_cache_[DC_WAYS];

#ifdef PERF_STATS
    // Plain counters — each CPU owns its MMU exclusively; stats are read only after join.
    uint64_t dc_hits_{0};
    uint64_t dc_misses_{0};
#endif

    public:
    MMU(MCtrl& mc) : mctrl(mc){

        control_reg = 0x0;
        ccr = 0x0;
        iccr = 0x0;
        dccr = 0x0;
        ctx_tbl_ptr = 0x0;
        ctx_n = 0x0;
        last_ctx_n = 0x0;
        fault_status_reg = 0x0;
        fault_address_reg = 0x0;
    
        reset();
    }	
	

    private:
 
    TLB itlb; // TLB for instructions
    TLB dtlb; // For data R/W

    // SRMMU FSR packer 
    inline u32 make_fsr(u8 level, u8 at, u8 ft, bool fav)
    {
        return ((u32(level & 0x3) << 8) |
                (u32(at    & 0x7) << 5) |
                (u32(ft    & 0x7) << 2) |
                (fav ? 0x2u : 0u));
    }
    
public:
    MCtrl&  get_mctrl() {return mctrl;}

    void set_control_reg(u32 value) {
        if ((value & 0x1) != (control_reg & 0x1)) {
            fetch_cache_.vpage = ~0u; // Invalidate on MMU enable/disable transition
            for (auto& dc : data_cache_)  dc.vpage = ~0u;
            for (auto& sc : store_cache_) sc.vpage = ~0u;
        }
        control_reg = value;
    }
    u32 get_control_reg() {return control_reg;}
 
    bool get_enabled() {return control_reg & 0x1;}
 
    TLB& get_itlb() {return itlb;}
    TLB& get_dtlb() {return dtlb;}
#ifdef PERF_STATS
    struct DCStats { uint64_t& hits; uint64_t& misses; };
    DCStats get_dc_stats() { return {dc_hits_, dc_misses_}; }
#endif
    
    bool get_no_fault() {return (control_reg & 0x2) >> 1 == 0x1;};

    void set_ccr(u32 value) {
        ccr = value;
    }
    u32 get_ccr() {return ccr;}
     
    void set_iccr(u32 value) {
        iccr = value;
    }
    u32 get_iccr() {return iccr;}
     
    void set_dccr(u32 value) {
        dccr = value;
    }
    u32 get_dccr() {return dccr;}
 
    void set_ctx_tbl_ptr(u32 value) {
        ctx_tbl_ptr = value;
    }
    u32 get_ctx_tbl_ptr() {return ctx_tbl_ptr;}
 
    void set_ctx_number(u32 value) {
        ctx_n = value;
    }
    u32 get_ctx_number() {return ctx_n;}
 
    u32 get_fault_address() {return fault_address_reg;}

    u32 get_fault_status() {return fault_status_reg;}
    void clear_fault_status() {fault_status_reg = 0x0;}

    void reset() {
        control_reg = 0x0;
        ccr = 0x0;
        iccr = 0x0;
        dccr = 0x0;
        ctx_tbl_ptr = 0x0;
        ctx_n = 0x0;
        last_ctx_n = 0x0;
        fault_status_reg = 0x0;
        fault_address_reg = 0x0;
        fetch_cache_ = FetchCache{};
        for (auto& dc : data_cache_)  dc = DataCache{};
        for (auto& sc : store_cache_) sc = StoreCache{};

        itlb.flush();
        dtlb.flush();
    }

    void nop() { return; }

    // FLush TLB
    void flush() {
        fetch_cache_.vpage = ~0u; // Invalidate L0 fetch cache
        for (auto& dc : data_cache_)  dc.vpage = ~0u;
        for (auto& sc : store_cache_) sc.vpage = ~0u;
        itlb.flush();
        dtlb.flush();
    }

    #if defined(PROFILE_LOCKS)
    CpuMutexProfiles mtx_profiles_ = {};
    #endif

    AtomicResult atomic_casa32(u32 vaddr, bool supervisor, u32 expected, u32 desired, bool& swapped);

    AtomicResult atomic_swap32(u32 vaddr, bool supervisor, u32 value);

    inline u32 MemAccessBypassRead4(u32 pa) const noexcept {
        u32 out;
        auto r = mctrl.try_read32(pa, out, false);
        if(r == MemBusStatus::Ok)
            return out;
        else
            return 0x0;
    }

    inline void MemAccessBypassWrite4(u32 pa, u32 value) noexcept {
        mctrl.try_write32(pa, value);
        return;
    }

    u8  check_perms(u32 vaddr, u32 pte, intent rw, bool supervisor, u8 level, bool report_fault) noexcept;

    inline u8 get_access_type(intent rw, bool supervisor) {
        u8 AT = 0;

        // ACCESS TYPE
        if(rw == intent_load) {
            if(supervisor)
                AT = 1;
            else 
                AT = 0;
        } else if (rw == intent_store) {
            // We have no way of discerning between store to data or instruction space...
            if(supervisor)
                AT = 5;
            else
                AT = 4;
        } else /*rw == intent_execute*/ {
            if(supervisor)
                AT = 3;
            else
                AT = 2;
        }

        return AT;
    }

    
    u32 get_PTE(u32 virt_addr, u8& level);

    static inline u32 get_addr_level_mask(u8 level) {
        u32 mask = 0;
        switch(level) {
            case(3): mask = ~0xFFF; break; //LOBITS12;
            case(2): mask = ~0x3FFFF; break; //LOBITS18;
            case(1): mask = ~0xffffff; break; //LOBITS24;
            default: throw std::logic_error("Level !=1,2,3 not allowed");
        }
        return mask;
    }

    
    

    MMUTranslateResult translate_va(u32 virt_addr, bool supervisor, intent rw=intent_load, bool report_faults = true);
      
    inline void set_fault(const MMUFault& f)
    {
        fault_address_reg = f.far;
        fault_status_reg  = make_fsr(f.level, f.at, f.ft, f.fav);
    }
    
    /* All memory access is routed through this template function. */
    /* It handles reads and writes of different sizes,
     * with or without virtual memory mapping,
     * with or without byte order swaps (reverse).
     *
     * returns:
     * 0  - OK, data read or written
     * <0 - MMU FAULT
     */

    struct MemAccessResult {
        bool ok;
        // optional: for debug/metrics
    };

    template<intent rw=intent_load, unsigned size = 4>
    int MemAccess(u32 virt_addr, u32& value, bool reverse, bool supervisor = true, bool report_faults = true)
    {
        auto bus_fault = [&](){
            if (report_faults) {
                MMUFault f{
                    .far   = virt_addr,  // full VA
                    .ft    = 5,          // access bus error
                    .at    = get_access_type(rw, supervisor),
                    .level = 0,          // not meaningful here
                    .fav   = true,       // OK for data; also fine for execute
                };
                set_fault(f);
            }
            return -5;
        };


        u32 phys_addr = 0x0;
        const bool mmu_on = get_enabled();

        if(mmu_on) {
            // L0 instruction fetch cache: bypass translate_va + get_bank for
            // repeated fetches within the same virtual page (~1024 instructions/page).
            if constexpr (rw == intent_execute) {
                const u32 vpage = virt_addr >> 12;
                if (__builtin_expect(
                        fetch_cache_.vpage == vpage &&
                        fetch_cache_.super == supervisor &&
                        fetch_cache_.ctx   == ctx_n, 1)) {
                    const u8* p = fetch_cache_.page_data + (virt_addr & 0xFFF);
                    value = (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
                    return 0;
                }
            }

            // L0 data load cache: bypass translate_va + get_bank for repeated
            // loads within the same virtual page (kernel stack, struct fields, etc.).
            if constexpr (rw == intent_load) {
                const u32 vpage = virt_addr >> 12;
                const auto& dc = data_cache_[vpage & (DC_WAYS - 1)];
                if (__builtin_expect(
                        dc.vpage == vpage &&
                        dc.super == supervisor &&
                        dc.ctx   == ctx_n, 1)) {
#ifdef PERF_STATS
                    ++dc_hits_;
#endif
                    const u8* p = dc.page_data + (virt_addr & 0xFFF);
                    switch(size) {
                        case(1): value = p[0]; break;
                        case(2): value = (u32(p[0]) << 8) | u32(p[1]); break;
                        case(4): value = (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]); break;
                        default: break;
                    }
                    return 0;
                }
#ifdef PERF_STATS
                ++dc_misses_;
#endif
            }

            // L0 store cache fast path: direct write for single-CPU mode
            if constexpr (rw == intent_store) {
                if (!g_smp_mode) {
                    const u32 vpage = virt_addr >> 12;
                    auto& sc = store_cache_[vpage & (DC_WAYS - 1)];
                    if (__builtin_expect(
                            sc.vpage == vpage &&
                            sc.super == supervisor &&
                            sc.ctx   == ctx_n, 1)) {
                        u8* p = sc.page_data + (virt_addr & 0xFFF);
                        switch(size) {
                            case(1): p[0] = u8(value); break;
                            case(2): p[0] = (value >> 8) & 0xFF; p[1] = value & 0xFF; break;
                            case(4): p[0] = (value >> 24) & 0xFF; p[1] = (value >> 16) & 0xFF;
                                     p[2] = (value >>  8) & 0xFF; p[3] = value & 0xFF; break;
                            default: break;
                        }
                        return 0;
                    }
                }
            }

            auto res = translate_va(virt_addr, supervisor, rw, report_faults);

            if(!res.ok)
            {
                u32 FT;
                if(report_faults)
                    FT = (fault_status_reg >> 2) & 0b111;
                else
                    FT = 1;
                return -FT;
            }

            phys_addr = res.pa;

        } else {
            // MMU disabled - phys addr == virt addr
            phys_addr = virt_addr;
        }

        // Get bank for the address
        auto pbank = mctrl.get_bank(phys_addr);
        if(!pbank) {// No physical bank at this address..
            return bus_fault();
        }

        if constexpr (rw == intent_store)
        {
            u8* bdata = reinterpret_cast<u8*>(pbank->get_ptr());
            if (!g_smp_mode && mmu_on && bdata) {
                // Single-CPU RAM store: fill store cache and write directly (no lock, no virtual call).
                const u32 vpage = virt_addr >> 12;
                auto& sc = store_cache_[vpage & (DC_WAYS - 1)];
                sc.vpage     = vpage;
                sc.ctx       = ctx_n;
                sc.super     = supervisor;
                sc.page_data = bdata + (phys_addr & ~0xFFFu) - pbank->get_base();
                u8* p = sc.page_data + (virt_addr & 0xFFF);
                switch(size) {
                    case(1): p[0] = u8(value); break;
                    case(2): p[0] = (value >> 8) & 0xFF; p[1] = value & 0xFF; break;
                    case(4): p[0] = (value >> 24) & 0xFF; p[1] = (value >> 16) & 0xFF;
                             p[2] = (value >>  8) & 0xFF; p[3] = value & 0xFF; break;
                    default: throw std::runtime_error("Error write size != {1,2,4}");
                }
            } else {
                // Peripheral bank (bdata==null), SMP, or MMU-disabled: lock + virtual write.
                std::unique_lock<std::shared_mutex> lk;
                if (g_smp_mode) {
                    auto& mtx = pbank->get_mutex(phys_addr);
#if defined(PERF_STATS)
                    pbank->perf_lock(mtx);
                    lk = std::unique_lock<std::shared_mutex>(mtx, std::adopt_lock);
#else
                    lk = std::unique_lock<std::shared_mutex>(mtx);
#endif
                }
                switch(size) {
                    case(1): pbank->write8_nolock(phys_addr, value);           break;
                    case(2): pbank->write16_nolock(phys_addr, value, false);   break;
                    case(4): pbank->write32_nolock(phys_addr, value, false);   break;
                    default: throw std::runtime_error("Error write size != {1,2,4}");
                }
            }
        }
        else // read or execute: no host lock needed (concurrent reads safe; read-write races are SPARC UB)
        {
            // On fetch miss: fill L0I cache, then read directly via host pointer.
            if constexpr (rw == intent_execute) {
                if (mmu_on) {
                    const u8* bdata = reinterpret_cast<const u8*>(pbank->get_ptr());
                    if (bdata) {
                        fetch_cache_.vpage     = virt_addr >> 12;
                        fetch_cache_.ctx       = ctx_n;
                        fetch_cache_.super     = supervisor;
                        fetch_cache_.page_data = bdata + (phys_addr & ~0xFFFu) - pbank->get_base();
                        const u8* p = fetch_cache_.page_data + (virt_addr & 0xFFF);
                        value = (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
                        return 0;
                    }
                }
            }
            // On load miss: fill L0DC, then read directly via host pointer.
            if constexpr (rw == intent_load) {
                if (mmu_on) {
                    const u8* bdata = reinterpret_cast<const u8*>(pbank->get_ptr());
                    if (bdata) {
                        const u32 vpage = virt_addr >> 12;
                        auto& dc = data_cache_[vpage & (DC_WAYS - 1)];
                        dc.vpage     = vpage;
                        dc.ctx       = ctx_n;
                        dc.super     = supervisor;
                        dc.page_data = bdata + (phys_addr & ~0xFFFu) - pbank->get_base();
                        const u8* p = dc.page_data + (virt_addr & 0xFFF);
                        switch(size) {
                            case(1): value = p[0]; break;
                            case(2): value = (u32(p[0]) << 8) | u32(p[1]); break;
                            case(4): value = (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]); break;
                            default: break;
                        }
                        return 0;
                    }
                }
            }
            // Peripheral bank (get_ptr==null) or MMU disabled: fall back to virtual read.
            switch(size) {
                case(1): value = pbank->read8_nolock(phys_addr);           break;
                case(2): value = pbank->read16_nolock(phys_addr, false);   break;
                case(4): value = pbank->read32_nolock(phys_addr, false);   break;
                default: throw std::runtime_error("Error read size != {1,2,4}");
            }
        }

        return 0;
    }

};






