#ifndef _MMU_H_
#define _MMU_H_

#include <cstdint>
#include <array>
#include <atomic>


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
        mutable u64 last_used = 0;
        u8 level = 0;
    };

    std::array<Entry, TLB_ENTRIES> entries;
    mutable u64 use_counter = 0;
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
    MCtrl&  GetMCTRL() {return mctrl;}

    void SetControlReg(u32 value) {
        control_reg = value;
    }
    u32 GetControlReg() {return control_reg;}
 
    bool GetEnabled() {return control_reg & 0x1;}
 
    TLB& get_itlb() {return itlb;}
    TLB& get_dtlb() {return dtlb;}
    
    bool GetNoFault() {return (control_reg & 0x2) >> 1 == 0x1;};

    void SetCCR(u32 value) {
        ccr = value;
    }
    u32 GetCCR() {return ccr;}
     
    void SetICCR(u32 value) {
        iccr = value;
    }
    u32 GetICCR() {return iccr;}
     
    void SetDCCR(u32 value) {
        dccr = value;
    }
    u32 GetDCCR() {return dccr;}
 
    void SetCtxTblPtr(u32 value) {
        ctx_tbl_ptr = value;
    }
    u32 GetCtxTblPtr() {return ctx_tbl_ptr;}
 
    void SetCtxNumber(u32 value) {
        ctx_n = value;
    }
    u32 GetCtxNumber() {return ctx_n;}
 
    u32 GetFaultAddress() {return fault_address_reg;}

    u32 GetFaultStatus() {return fault_status_reg;}
    void ClearFaultStatus() {fault_status_reg = 0x0;}

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
        
        itlb.flush();
        dtlb.flush();
    }

    void nop() { return; }

    // FLush TLB
    void flush() { 
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
        
        if(GetEnabled()) {

            auto res = translate_va(virt_addr, supervisor, rw, report_faults);
            
            if(!res.ok)
            {
                u32 FT;
                // We failed
                // Get FT from fault status reg and return it
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

        // Get mtx for the page/bank in question, and lock it
        auto pbank = mctrl.get_bank(phys_addr);
        if(!pbank) {// No physical bank at this address..
            return bus_fault();  
        }
        auto& mtx = pbank->get_mutex(phys_addr);

#ifdef PROFILE_LOCKS
        ProfiledLock lk(mtx, mtx_profiles_.ram);
#elif defined(PERF_STATS)
        // Probe contention via try_lock before blocking
        {
            auto* rb = dynamic_cast<RamBank*>(pbank);
            if (rb) {
                if (!mtx.try_lock()) {
                    rb->perf_lock_contended();
                    mtx.lock();
                }
                rb->perf_lock_acquired();
            } else {
                mtx.lock();
            }
        }
        std::unique_lock<std::mutex> lk(mtx, std::adopt_lock);
#else
        std::lock_guard<std::mutex> lk(mtx);
#endif

        if constexpr (rw==intent_store)
        {
            switch(size) {
                case(1):
                    pbank->write8_nolock(phys_addr, value);
                    break;
                case(2):
                    pbank->write16_nolock(phys_addr, value, false);
                    break;
                case(4):
                    pbank->write32_nolock(phys_addr, value, false);
                    break;
                default:
                    throw std::runtime_error("Error write size != {1,2,4}");
            }
            
        }
        else // read or execute
        {
            switch(size) {
                case(1):
                    value = pbank->read8_nolock(phys_addr);
                    break;
                case(2):
                    value = pbank->read16_nolock(phys_addr, false);
                    break;
                case(4):
                    value = pbank->read32_nolock(phys_addr, false);
                    break;
                default:
                    throw std::runtime_error("Error read size != {1,2,4}");
            }
        }
        
        return 0;
    }

};





#endif // _MMU_H_

