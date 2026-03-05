#include "MMU.h"

#include <iostream>
#include <iomanip>

#include <format>

constexpr u32 L3_PAGE_MASK = 0xfffff000;
constexpr u32 L2_PAGE_MASK = 0xfffc0000;
constexpr u32 L1_PAGE_MASK = 0xff000000;



TLB::TLB() {
    entries.fill(Entry{});
}

bool TLB::is_valid(u32 pte) {
    return (pte & 0x3) == 0x2; // PTE ET == 0x2
}

bool TLB::lookup(u32 context, u32 vaddr, u32& pte_out, u8& level_out) const {
    for (const auto& entry : entries) {
        if (!is_valid(entry.pte)) {
            pte_out = 0;
            level_out = 0;
            continue;
        }
        if (entry.context == context && (vaddr & entry.mask) == entry.vaddr_tag) {
            entry.last_used = ++use_counter;
            pte_out = entry.pte;
            level_out = entry.level;
            return (entry.pte & ~0x3) || entry.level;
        }
    }
    pte_out = 0;
    level_out = 0;
    return false;
}

void TLB::insert(u32 context, u32 vaddr, u8 level, u32 pte) {
    if (!is_valid(pte)) {
        return; // Don't insert invalid PTEs
    }
    u32 mask = MMU::get_addr_level_mask(level);

    for (auto& entry : entries) {
        if (!is_valid(entry.pte)) {
            entry.context = context;
            entry.vaddr_tag = vaddr & mask;
            entry.mask = mask;
            entry.pte = pte;
            entry.last_used = ++use_counter;
            entry.level = level;
            return;
        }
    }

    auto lru_entry = &entries[0];
    for (auto& entry : entries) {
        if (entry.last_used < lru_entry->last_used) {
            lru_entry = &entry;
        }
    }

    lru_entry->context = context;
    lru_entry->vaddr_tag = vaddr & mask;
    lru_entry->mask = mask;
    lru_entry->pte = pte;
    lru_entry->last_used = ++use_counter;
    lru_entry->level = level;
}

// We can choose two behaviors:
// Invalidate by context + vaddr (strict)
// Invalidate all entries of a context (on context switch)
// Below: strict invalidate by context and virtual address
// TODO: Linux boot seems to identify that we use strict, figure out how this can be chosen
void TLB::invalidate_strict(u32 context, uint32_t vaddr) {
    for (auto& entry : entries) {
        if (!is_valid(entry.pte)) {
            continue;
        }
        if (entry.context == context && (vaddr & entry.mask) == entry.vaddr_tag) {
            entry.pte = 0;
        }
    }
}

void TLB::flush() {
    for (auto& entry : entries) {
        entry.pte = 0;
    }
}

void TLB::debug_dump(const std::string& label) const {
    std::cout << "===== " << label << " Dump =====\n";
    std::cout << std::left << std::setw(5)  << "Idx"
              << std::setw(10) << "Ctx"
              << std::setw(12) << "VAddr"
              << std::setw(10) << "Mask"
              << std::setw(12) << "PageSize"
              << std::setw(10) << "PTE"
              << std::setw(12) << "LastUsed"
              << "\n";

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (!is_valid(e.pte)) continue;

        std::string size_str;
        switch (e.mask) {
            case L3_PAGE_MASK: size_str = "4 KB"; break;
            case L2_PAGE_MASK: size_str = "256 KB"; break;
            case L1_PAGE_MASK: size_str = "16 MB"; break;
            default: size_str = "?"; break;
        }

        std::cout << std::setw(5)  << i
                  << std::setw(10) << e.context
                  << "0x" << std::hex << std::setw(8) << e.vaddr_tag
                  << " 0x" << std::setw(8) << e.mask
                  << std::setw(12) << size_str
                  << "0x" << std::setw(8) << e.pte
                  << std::dec << std::setw(12) << e.last_used
                  << "\n";
    }

    std::cout << "===========================\n";
}

std::string rw_str(intent rw) {
    switch(rw) {
        case(intent_load): return "intent_load";
        case(intent_store): return "intent_store";
        case(intent_execute): return "intent_exec";
        default: return "unknown";
    }

}

std::string at_str(int at) {
    switch(at) {
        case(0): return "Load from User Data Space";
        case(1): return "Load from Supervisor Data Space";
        case(2): return "Load/Execute from User Instruction Space";
        case(3): return "Load/Execute from Supervisor Instruction Space";
        case(4): return "Store to User Data Space";
        case(5): return "Store to Supervisor Data Space";
        case(6): return "Store to User Instruction Space";
        case(7): return "Store to Supervisor Instruction Space";
        default: return "unknown";
    }

}

std::string acc_str(int acc, bool super) {
    if(super) {
       switch(acc) {
            case(0): return "S:Read Only";
            case(1): return "S:Read/Write";
            case(2): return "S:Read/Execute";
            case(3): return "S:Read/Write/Execute";
            case(4): return "S:Execute Only";
            case(5): return "S:Read/Write";
            case(6): return "S:Read/Execute";
            case(7): return "S:Read/Write/Execute";
            default: return "S:unknown";
       }
    } else {
         switch(acc) {
            case(0): return "U:Read Only";
            case(1): return "U:Read/Write";
            case(2): return "U:Read/Execute";
            case(3): return "U:Read/Write/Execute";
            case(4): return "U:Execute Only";
            case(5): return "U:Read Only";
            case(6): return "U:No Access";
            case(7): return "U:No Access";
            default: return "U:unknown";
         } 
    }
}

std::string ft_str(int ft) {
    switch(ft) {
        case(0): return "None";
        case(1): return "Invalid address error";
        case(2): return "Protection error";
        case(3): return "Privilege violation error";
        case(4): return "Translation error";
        case(5): return "Access bus error";
        case(6): return "Internal error";
        case(7): return "Reserved";
        default: return "unknown";

    }
}



u32 MMU::get_PTE(u32 virt_addr, u8& level) {
    // |  IND 1  |  IND 2  |  IND 3  |  PAGE OFFSET  |
    // 31      24 23     18 17     12 11            0	
    u32 ind1 = (virt_addr >> 24) & LOBITS8;
    u32 ind2 = (virt_addr >> 18) & LOBITS6;
    u32 ind3 = (virt_addr >> 12) & LOBITS6;

    u32 l1_tbl_ptr = (ctx_tbl_ptr << 4) + ctx_n * 4;
    u32 lx = MemAccessBypassRead4(l1_tbl_ptr);

    u32 ET = lx & 3;
    u32 PTP = (lx & ~3) >> 2;	
    
    u32 pa = 0x0;

    // MMU Table walk
    if( ET == 1) { // PTD, continue to level 1
        pa = (PTP << 6) + (ind1 * 4);
        lx = MemAccessBypassRead4(pa);
        
        ET = lx & 3;
        PTP = (lx & ~3) >> 2;
        level = 1;	
        if( ET == 1) { // PTD, continue to level 2
            pa = (PTP << 6) + (ind2 * 4);
            lx = MemAccessBypassRead4(pa);
            
            ET = lx & 3;
            PTP = (lx & ~3) >> 2;
            level = 2;	
            if( ET == 1) {
                // PTD, continue to level 3
                pa = (PTP << 6) + (ind3 * 4);
                lx = MemAccessBypassRead4(pa);
                
                ET = lx & 3;
                PTP = (lx & ~3) >> 2;
                level = 3;
            }
        }
    }

    u32 PTE = lx;
    return PTE;
}

AtomicResult MMU::atomic_swap32(u32 vaddr, bool supervisor, u32 value) {
    AtomicResult r = {};
    
    u32 paddr = 0x0;
    if(GetEnabled()) {
        auto tr = translate_va(vaddr, supervisor, intent_load, !GetNoFault());
        if(!tr.ok) {
            return {false, 0x0U};    
        }

        // Check perms for write too, since swap is rw
        auto ft = check_perms(vaddr,tr.pte, intent_store, supervisor, tr.level, !GetNoFault());
        if(ft != 0) {
            return {false, 0x0U};       
        }

        paddr = tr.pa;
    } else
        paddr = vaddr;

    // Get mtx for the page/bank in question, and lock it
    auto pbank = mctrl.get_bank(paddr);
    if(!pbank) {// No physical bank at this address..
        r.ok = false;
        return r;  
    }
    auto& mtx = pbank->get_mutex(paddr);

#ifdef PROFILE_LOCKS
    ProfiledLock lk(mtx, mtx_profiles_.ram);
#else
    std::lock_guard<std::mutex> lk(mtx);
#endif

    r.old = pbank->read32_nolock(paddr);          // BE handling here
    pbank->write32_nolock(paddr, value);
    r.ok = true;

    return r;
}

AtomicResult MMU::atomic_casa32(u32 vaddr, bool supervisor, u32 expected, u32 desired, bool& swapped) {
    AtomicResult r = {};
    
    u32 paddr_old = 0x0;
    
    MMUTranslateResult tr = {};

    if(GetEnabled()) {
        tr = translate_va(vaddr, supervisor, intent_load, !GetNoFault());
        if(!tr.ok) {
            r.ok = false;
            return r;    
        }

        paddr_old = tr.pa;
    } else
        paddr_old = vaddr;

    // Get mtx for the page/bank in question, and lock it
    auto pbank = mctrl.get_bank(paddr_old);
    if(!pbank) {// No physical bank at this address..
        r.ok = false;
        return r;  
    }
    auto& mtx = pbank->get_mutex(paddr_old);

#ifdef PROFILE_LOCKS
    ProfiledLock lk(mtx, mtx_profiles_.ram);
#else
    std::lock_guard<std::mutex> lk(mtx);
#endif
    // Do the casa:
    r.ok = true;
    r.old = pbank->read32_nolock(paddr_old);
    if (r.old == expected) {
        // Check permissions for store also
        if(GetEnabled()) {
            auto ft = check_perms(vaddr, tr.pte, intent_store, supervisor, tr.level, !GetNoFault());
            if(ft != 0) {
                r.ok = false;
                return r;    
            }
        }
        pbank->write32_nolock(paddr_old, desired);
        swapped = true;
    }

    return r;
}

u8  MMU::check_perms(u32 vaddr, u32 pte, intent rw, bool supervisor, u8 level, bool report_fault) noexcept {
    u8 FT = 0;
    u8 ACC = (pte >> 2) & 0x7;
    auto AT = get_access_type(rw, supervisor);
    u32 ET = pte & SRMMU_ET_MASK;

    if((ET == 1 && level == 3) || ET == 3) {
        FT = 4; // Translation error
    }


    if(ET == 0)
        FT = 1; // Invalid address
    else { 
        // Check against access controls
        // SRMMU page 257 table
        if(AT == 0) {
            if( ACC == 4) FT = 2;
            if( ACC == 6 || ACC == 7) FT = 3;
        } else if(AT == 1) {
            if( ACC == 4) FT = 2;
        } else if(AT == 2) {
            if( ACC == 0 || ACC == 1 || ACC == 5) FT = 2;
            if( ACC == 6 || ACC == 7 ) FT = 3;
        } else if(AT == 3) {
            if( ACC == 0 || ACC == 1 || ACC == 5) FT = 2;
        } else if(AT == 4) {
            if( ACC == 0 || ACC == 2 || ACC == 4 || ACC == 5) FT = 2;
            if( ACC == 6 || ACC == 7 ) FT = 3;
        } else if(AT == 5) {
            if( ACC == 0 || ACC == 2 || ACC == 4 || ACC == 6) FT = 2;
        } else if(AT == 6) {
            if( ACC == 0 || ACC == 1 || ACC == 2 || ACC == 4 || ACC == 5) FT = 2;
            if( ACC == 6 || ACC == 7 ) FT = 3;
        } else if(AT == 7) {
            if( ACC==0 || ACC==1 || ACC==2 || ACC==4 || ACC == 5 || ACC == 6) FT = 2;
        } 
    }

     // Signal fault 
    if(FT != 0) {
        //std::cerr << "MMU Fault, virt_addr = 0x" << std::hex << virt_addr << ", lvl: " << std::dec << (int)level << ", intent=" << rw << " (" << rw_str(rw) << "), AT = " << AT << " (" << at_str(AT) << "), ACC = " << ACC << " (" << acc_str(ACC, supervisor) << "), FT = " << FT << " (" << ft_str(FT) << ")\n";
        if(report_fault) {
            MMUFault f{
                    .far = vaddr,
                    .ft = FT, 
                    .at = AT,
                    .level = level,
                    .fav = true,
                };

            set_fault(f);    
        }
    }

    return FT;
}


MMUTranslateResult MMU::translate_va(u32 vaddr, bool supervisor, intent rw, bool report_faults) {
     
    
    // Lookup from TLB
    auto ctx = GetCtxNumber();
    u32 pte = 0;
    u8 level = 0;
    bool found = false;
    if(rw == intent_execute) {
        found = itlb.lookup(ctx, vaddr, pte, level);
    } else {
        found = dtlb.lookup(ctx, vaddr, pte, level);
    }
   
    
    if(!found) {
        level = 0;	
        // Fetch context table entry:
        if(ctx_n > 255)
            throw std::runtime_error("ctx_n > 255 error");

        pte = get_PTE(vaddr, level);
        // Store PTE in TLB if it is valid
        if((pte & SRMMU_ET_MASK) == SRMMU_ET_PTE) {
            if(rw == intent_execute)
                itlb.insert(ctx, vaddr, level, pte);
            else
                dtlb.insert(ctx, vaddr, level, pte);

        }
    }
    
    
    // Fault handling:    
    u8 FT = check_perms(vaddr, pte, rw, supervisor, level, report_faults);
    
    // Signal fault 
    if(FT != 0) {
        return {false, 0x0, level, 0x0};
    }

    // We have a valid PTE, Assemble physical address
    u32 PPN = (pte & ~0xff) >> 8;
    u32 va = vaddr; // Copy of virt addr that will be wiped according to level
                    // to get the page offset
    switch(level) {
        case(3): va = va & 0xFFF; break; //LOBITS12;
        case(2): va = va & 0x3FFFF; break; //LOBITS18;
        case(1): va = va & 0xffffff; break; //LOBITS24;
        default: break;
    }
    u32 phys_addr = (va | (PPN << 12));

    return {true, phys_addr, level, pte};
}

