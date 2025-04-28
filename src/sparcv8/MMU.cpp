#include "MMU.h"

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

u32 MMU::get_access_type(intent rw, bool supervisor) {
    u32 AT = 0;

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


u32 MMU::get_PTE(u32 virt_addr, u8& level) {
    // |  IND 1  |  IND 2  |  IND 3  |  PAGE OFFSET  |
    // 31      24 23     18 17     12 11            0	
    u32 ind1 = (virt_addr >> 24) & LOBITS8;
    u32 ind2 = (virt_addr >> 18) & LOBITS6;
    u32 ind3 = (virt_addr >> 12) & LOBITS6;

    u32 l1_tbl_ptr = (ctx_tbl_ptr << 4) + ctx_n * 4;
    u32 lx = MMU::MemAccessBypassRead4(l1_tbl_ptr);

    u32 ET = lx & 3;
    u32 PTP = (lx & ~3) >> 2;	
    
    u32 pa = 0x0;

    // MMU Table walk
    if( ET == 1) { // PTD, continue to level 1
        pa = (PTP << 6) + (ind1 * 4);
        lx = MMU::MemAccessBypassRead4(pa);
        
        ET = lx & 3;
        PTP = (lx & ~3) >> 2;
        level = 1;	
        if( ET == 1) { // PTD, continue to level 2
            pa = (PTP << 6) + (ind2 * 4);
            lx = MMU::MemAccessBypassRead4(pa);
            
            ET = lx & 3;
            PTP = (lx & ~3) >> 2;
            level = 2;	
            if( ET == 1) {
                // PTD, continue to level 3
                pa = (PTP << 6) + (ind3 * 4);
                lx = MMU::MemAccessBypassRead4(pa);
                
                ET = lx & 3;
                PTP = (lx & ~3) >> 2;
                level = 3;
            }
        }
    }

    u32 PTE = lx;
    return PTE;
}


u32 MMU::translate_va(u32 virt_addr, bool supervisor, intent rw, bool report_faults) {
     
    u8 level = 0;
   
    // Lookup from TLB
    //TLBEntry tlb = TLBLookup(rw, virt_addr);

    u32 PTE = 0;//tlb.PTE;
    //level = tlb.level;

    // PTE == 0 means cahce miss, do table walk
    if(PTE == 0) {
        level = 0;	
        // Fetch context table entry:
        //if(ctx_n > 0)
        //    throw std::runtime_error("ctx_n != 0 Needds testing");
        if(ctx_n > 255)
            throw std::runtime_error("ctx_n > 255 error");

        PTE = get_PTE(virt_addr, level);
        // Store PTE in TLB if it is valid
        if((PTE & SRMMU_ET_MASK) == SRMMU_ET_PTE)
            TLBCache(rw, virt_addr, PTE, level);
    }
    
    u32 ET = PTE & SRMMU_ET_MASK;

    // Fault handling:    
    u32 FT = 0; // Fault type      
    u32 ACC = (PTE >> 2) & 0x7;
    u32 AT = get_access_type(rw, supervisor);


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
        if(report_faults) {
            if(FT == 2)
                fault_address_reg = virt_addr; // Only show page, not page offset
            else
                fault_address_reg = virt_addr & ~0xfff; // Only show page, not page offset
        
            u32 FAV = 0x2;
            fault_status_reg = 0x0 | ((level&0x3) << 8) | (AT & 0x7) << 5 | FT << 2 | FAV;
        }
        return 0xffffffff;
    }


    // We have a valid PTE, Assemble physical address
    u32 PPN = (PTE & ~0xff) >> 8;
    u32 va = virt_addr; // Copy of virt addr that will be wiped according to level
    switch(level) {
        case(3): va = va & 0xFFF; break; //LOBITS12;
        case(2): va = va & 0x3FFFF; break; //LOBITS18;
        case(1): va = va & 0xffffff; break; //LOBITS24;
        default: break;
    }
    u32 phys_addr = (va | (PPN << 12));

    return phys_addr;
}

