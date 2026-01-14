#ifndef _MMU_H_
#define _MMU_H_

#include "../peripherals/MCTRL.h"

#include "CPU_defines.h"

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

#include <cstdint>
#include <array>

constexpr int TLB_ENTRIES = 16; // emulate LEON3 default

class TLB {
public:
    TLB();

    bool lookup(u32 context, u32 vaddr, u32& pte_out, u8& level_out) const;
    void insert(u32 context, u32 vaddr, u8 level, u32 pte);
    void invalidate_strict(u32 context, u32 vaddr);
    void flush(); // flush all entries

    void debug_dump(const std::string& label = "TLB") const;

private:
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

    inline u32 MemAccessBypassRead4(u32 pa) const {
        return mctrl.read32(pa);
    }

    inline void MemAccessBypassWrite4(u32 pa, u32 value) {
        mctrl.write32(pa, value);
    }

    inline u32 get_access_type(intent rw, bool supervisor) {
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

    
    

    u32 translate_va(u32 virt_addr, bool supervisor, intent rw=intent_load, bool report_faults = true);
      
    
    
    /* All memory access is routed through this template function. */
    /* It handles reads and writes of different sizes,
     * with or without virtual memory mapping,
     * with or without byte order swaps (reverse).
     *
     * returns:
     * 0  - OK, data read or written
     * <0 - MMU FAULT
     */
    template<intent rw=intent_load, unsigned size = 4>
    int MemAccess(u32 virt_addr, u32& value, bool reverse, bool supervisor = true, bool report_faults = true)
    {
        u32 phys_addr = 0x0;
        u8 level = 0;
       
        // Check alignment
        // We drop aligment check, since the relevant instructions allready does this test
        // and software (linux) does not rely on FSR/FAR in unaligned-traps.
        /*
        if(virt_addr % size != 0) {
            u32 AT = get_access_type(rw, supervisor);
            u32 FT = 3;
            u32 FAV = 0x2;
            if(report_faults) {
                fault_status_reg = 0x0 | ((level&0x3) << 8) | (AT & 0x7) << 5 | FT << 2 | FAV;
                fault_address_reg = virt_addr & ~0xfff;
            }
            return -FT;
        }
        */
        
    
        if(GetEnabled()) {

            phys_addr = translate_va(virt_addr, supervisor, rw, report_faults);
            
            if((phys_addr == 0xffffffff) && (virt_addr != 0xffffffff))
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

        } else {
            // MMU disabled - phys addr == virt addr
            phys_addr = virt_addr;
        }

        try
        {
            if(rw==intent_store)
            {
                switch(size) {
                    case(1):
                        mctrl.write8(phys_addr, value);
                        break;
                    case(2):
                        mctrl.write16(phys_addr, value, false);
                        break;
                    case(4):
                        mctrl.write32(phys_addr, value, false);
                        break;
                    default:
                        throw std::runtime_error("Error write size != {1,2,4}");
                }
            }
            else // read or execute
            {
                switch(size) {
                    case(1):
                        value = mctrl.read8(phys_addr);
                        break;
                    case(2):
                        value = mctrl.read16(phys_addr, false);
                        break;
                    case(4):
                        value = mctrl.read32(phys_addr, false);
                        break;
                    default:
                        throw std::runtime_error("Error read size != {1,2,4}");
                }
            }

        }
        catch(const std::out_of_range& c)
        {
            //fprintf(stderr, "MemAccess(virt=%08X,phys=%08X,size=%u,reverse=%s,mode=%s,UM=%d,VM=%d> failed because of bad function call!\n",
            u8 FT = 1;
            u32 FAV = 0x2;
            if(report_faults) {
                fault_status_reg = 0x0 | ((level&0x3) << 8) | FT << 2 | FAV;
                fault_address_reg = virt_addr & ~0xfff;
            }
            return -FT;
        }
        
        return 0;
    }

};





#endif // _MMU_H_

