#ifndef _MMU_H_
#define _MMU_H_

#include "CPU.h"

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


static unsigned SwapBytes(unsigned value, unsigned size)
{
        if(size >= 2) value = ((value & 0xFF00FF00u) >> 8)
                                    | ((value & 0x00FF00FFu) << 8);
            if(size >= 4) value = (value >> 16) | (value << 16);
                return value;
}


/* The following data structures provide templated access
 * of different data sizes within a 32-bit integer.
 */
template<unsigned size, typename T=void>
struct MemDataRef
{
    union { u32 value; T alt[4 / sizeof(T)]; } d;
    T& reffun(unsigned ind) { return d.alt[ind >> (sizeof(T)==2)]; }
};
template<> struct MemDataRef<1,void>: public MemDataRef<0, u8>  { };
template<> struct MemDataRef<2,void>: public MemDataRef<0, u16> { };
template<> struct MemDataRef<4,void>: public MemDataRef<0, u32> { };

// RAM device is very simple.
template<unsigned size_bytes>
class SDRAM
{
    // The first version was stack allocated..
    //u32 buffer[size_bytes / 4];

    std::vector<u32> buffer;

    public:
        SDRAM() {
            buffer.reserve(size_bytes/4);
            buffer.resize(size_bytes/4);
        }
        u32 Read(u32 index) const        { 
                return buffer.at(index); // With bounds checking 
        }
        
        void Write(u32 index, u32 value) { 
                buffer.at(index) = value; // with bounds checking 
        }

        unsigned getSizeBytes()const {
            return size_bytes;
        }
};

class SDRAM2
{

    std::vector<u32> buffer;
    u32 size_bytes;
    public:
        SDRAM2(u32 size_bytes) : size_bytes(size_bytes){
            buffer.reserve(size_bytes/4);
            buffer.resize(size_bytes/4);
        }
        u32 Read(u32 index) const        { 
                return buffer.at(index); // With bounds checking 
        }
        
        void Write(u32 index, u32 value) { 
                buffer.at(index) = value; // with bounds checking 
        }

        u32* getPtr(u32 index) {
            return &(buffer.at(index));
        }

        unsigned getSizeBytes()const {
            return size_bytes;
        }
};


enum intent { intent_load=0, intent_store=1, intent_execute=2 };

class MMU {
    // MMUR REGS
    static u32 control_reg;
    static u32 ccr, iccr, dccr;        
    static u32 ctx_tbl_ptr;
    static u32 ctx_n, last_ctx_n;
    static u32 fault_status_reg;
    static u32 fault_address_reg;

    //static std::vector<std::pair<u32, SDRAM2>> base_addrs_regions;
	//static u32 base_ram;
	//static SDRAM2 ram;
    public:	
	struct TLBEntry {
		u32 va_index;
		u32 PTE;
		u8 level;
	};

    private:
    
    static TLBEntry tlbs[3][3];
	static int tlb_pos[3];
	   
    static bool tlb_miss;
public:
   
    static std::pair<  std::function<u32(u32)>,
                std::function<void(u32,u32)>
             > IOmap[];

    static void SetControlReg(u32 value) {
        control_reg = value;
    }
    static u32 GetControlReg() {return control_reg;}
 
    static bool GetEnabled() {return control_reg & 0x1;}
 
    static bool TLBMiss() {return tlb_miss;}
     
    static void SetCCR(u32 value) {
        ccr = value;
    }
    static u32 GetCCR() {return ccr;}
     
    static void SetICCR(u32 value) {
        iccr = value;
    }
    static u32 GetICCR() {return iccr;}
     
    static void SetDCCR(u32 value) {
        dccr = value;
    }
    static u32 GetDCCR() {return dccr;}
 
    static void SetCtxTblPtr(u32 value) {
        ctx_tbl_ptr = value;
    }
    static u32 GetCtxTblPtr() {return ctx_tbl_ptr;}
 
    static void SetCtxNumber(u32 value) {
        ctx_n = value;
    }
    static u32 GetCtxNumber() {return ctx_n;}
 
    static u32 GetFaultAddress() {return fault_address_reg;}

    static u32 GetFaultStatus() {return fault_status_reg;}
    static void ClearFaultStatus() {fault_status_reg = 0x0;}

    static void reset() {
        control_reg = 0x0;
        ccr = 0x0;
        iccr = 0x0;
        dccr = 0x0;
        ctx_tbl_ptr = 0x0;
        ctx_n = 0x0;
        last_ctx_n = 0x0;
        fault_status_reg = 0x0;
        fault_address_reg = 0x0;

        tlb_miss = true;
        for(int i = 0; i < 3; ++i) {
            tlb_pos[i] = 2;
            for(int j = 0; j < 3; ++j) {
                tlbs[i][j] = {0,0,0};
            }
        }
    }

    static void nop() { return; }

    // FLush TLB
    static void flush() { 
        for(int i = 0; i < 3; ++i)
            for(int j = 0; j < 3; ++j) {
                tlbs[j][i] = {0, 0, 0};
            }
       
        last_ctx_n = ctx_n; 
        return; 
    }

	static TLBEntry TLBLookup(intent rw, u32 virt_addr) {
        //tlb_miss = true;
        //return 0;

		if(ctx_n != last_ctx_n) {
			// SHould we flush TLB here?
			tlb_miss = true;
			return {0,0,0};
		}        
		
		u32 j = (u32)rw;	
		for(int i = 0; i < 3; ++i) {
            TLBEntry& tlb = tlbs[j][(i+tlb_pos[j])%3];
            u32 index_va = 0x0;	
            switch(tlb.level) {
			    case(3): index_va = virt_addr & ~0xFFF; break;// 4Kb size
			    case(2): index_va = virt_addr & ~0x3FFFF; break; // 256Kb size
			    case(1): index_va = virt_addr & ~0xFFFFFF; break; // 16Mb size
                case(0): index_va = virt_addr; break;
                default: throw std::runtime_error("Lookup: Level need to be 0-3");
		    }
	        
            

		    if(tlb.va_index == index_va) {
				tlb_miss = false;
				return tlb;
			} 	
		}
	


        tlb_miss = true;
		return {0,0,0};
	}

	static void TLBCache(intent rw, u32 virt_addr, u32 PTE, u8 level) {
		// Store PTE in TLB with va index
		u32 index_va;
		switch(level) {
			case(3): index_va = virt_addr & ~0xFFF; break;// 4Kb size
			case(2): index_va = virt_addr & ~0x3FFFF; break; // 256Kb size
			case(1): index_va = virt_addr & ~0xFFFFFF; break; // 16Mb size
			default: 
                     throw std::runtime_error("Level need to be 1-3");
		}
		
        u32 j = (u32)rw;
		tlb_pos[j] = (tlb_pos[j] + 1)%3; 
		tlbs[j][tlb_pos[j]] = {index_va, PTE, level};
		//std::cout << "va: 0x" <<std::hex << virt_addr << ": TLBCache " << j << ", " << std::hex << index_va << "/PTE: " << PTE <<std::dec << " (" << (int)level << ")\n"; 
	
        last_ctx_n = ctx_n;
    }

    static u32 MemAccessBypassRead4(u32 pa, bool reverse) {
        auto S = [=](u32 v) -> u32
                { return (reverse != CROSS_ENDIAN) ? SwapBytes(v,4) : v; };
        
        MemDataRef<4> data;
        data.d.value = IOmap[pa/0x10000].first(pa & ~3);
    
        return S(data.d.value);
    }


    static void MemAccessBypassWrite4(u32 pa, u32 value, bool reverse) {
        auto S = [=](u32 v) -> u32
                { return (reverse != CROSS_ENDIAN) ? SwapBytes(v,4) : v; };
        IOmap[pa/0x10000].second(pa & ~3, S(value));
    }

    struct translation {
        u32 vaddr;
        u32 paddr;
        u8 level;
    };

    static translation translate(u32 vaddr, bool supervisor, intent rw=intent_load) {
        translation tr;

        tr.vaddr = vaddr;
        tr.paddr = translate_va(vaddr, supervisor, tr.level, rw);
        return tr;

    }


    static std::string rw_str(intent rw) {
        switch(rw) {
            case(intent_load): return "intent_load";
            case(intent_store): return "intent_store";
            case(intent_execute): return "intent_exec";
            default: return "unknown";
        }

    }

    static std::string at_str(int at) {
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

    static std::string acc_str(int acc, bool super) {
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

    static std::string ft_str(int ft) {
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

    static u32 get_access_type(intent rw, bool supervisor) {
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

    static u32 translate_va(u32 virt_addr, bool supervisor, u8& level, intent rw=intent_load) {
     

       
        // |  IND 1  |  IND 2  |  IND 3  |  PAGE OFFSET  |
        // 31        23        17        11              0	
        u32 ind1 = (virt_addr >> 24) & LOBITS8;
        u32 ind2 = (virt_addr >> 18) & LOBITS6;
        u32 ind3 = (virt_addr >> 12) & LOBITS6;
        
        // Lookup from TLB
        TLBEntry tlb = TLBLookup(rw, virt_addr);

        u32 PTE = tlb.PTE;
        level = tlb.level;

        // PTE == 0 means cahce miss, do table walk
        if(PTE == 0) {
            level = 0;	
            // Fetch context table entry:
            if(ctx_n > 0)
                throw std::runtime_error("ctx_n != 0 Needds testing");
            if(ctx_n > 255)
                throw std::runtime_error("ctx_n > 255 error");

            u32 l1_tbl_ptr = (ctx_tbl_ptr << 4)+ ctx_n * 4;
            u32 lx = MMU::MemAccessBypassRead4(l1_tbl_ptr, CROSS_ENDIAN);

            u32 ET = lx & 3;
            u32 PTP = (lx & ~3) >> 2;	
            
            u32 pa = 0x0;

            // MMU Table walk
            if( ET == 1) { // PTD, continue to level 1
                pa = (PTP << 6) + (ind1 * 4);
                lx = MMU::MemAccessBypassRead4(pa, CROSS_ENDIAN);
                
                ET = lx & 3;
                PTP = (lx & ~3) >> 2;
                level = 1;	
                if( ET == 1) { // PTD, continue to level 2
                    pa = (PTP << 6) + (ind2 * 4);
                    lx = MMU::MemAccessBypassRead4(pa, CROSS_ENDIAN);
                    
                    ET = lx & 3;
                    PTP = (lx & ~3) >> 2;
                    level = 2;	
                    if( ET == 1) {
                        // PTD, continue to level 3
                        pa = (PTP << 6) + (ind3 * 4);
                        lx = MMU::MemAccessBypassRead4(pa, CROSS_ENDIAN);
                        
                        ET = lx & 3;
                        PTP = (lx & ~3) >> 2;
                        level = 3;
                    }
                }
            }

            PTE = lx;

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
            fault_address_reg = virt_addr & ~0xfff; // Only show page, not page offset
            fault_status_reg = 0x0 | ((level&0x3) << 8) | (AT & 0x7) << 5 | FT << 2 | 0x1 << 1;
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
    int static MemAccess(u32 virt_addr, u32& value, bool reverse, bool supervisor = true)
    {
        u32 phys_addr = 0x0;
        u8 level = 0;
       
        // Check alignment
        if(virt_addr % size != 0) {
            u32 AT = get_access_type(rw, supervisor);
            u32 FT = 1;
            fault_status_reg = 0x0 | ((level&0x3) << 8) | (AT & 0x7) << 5 | FT << 2 | 0x1 << 1;
            fault_address_reg = virt_addr & ~0xfff;
            return -3;
        }

        
    
        if(GetEnabled()) {

            phys_addr = translate_va(virt_addr, supervisor, level, rw);
            
            if((phys_addr == 0xffffffff) && (virt_addr != 0xffffffff))
            {
                // We failed
                // Get FT from fault status reg and return it
                u32 FT = (fault_status_reg >> 2) & 0b111;
                return -FT;

            }

            if(virt_addr == 0xffd03170) // && virt_addr < 0xffd3ffff)
               std::cout << "VA: 0x" << std::hex << virt_addr << " -> PA: 0x" << phys_addr << "\n"; 
        } else {
            // MMU disabled - phys addr == virt addr
            phys_addr = virt_addr;

        }


       try
        {
            // An "E" bit in a TLB reverses the endianess for that page (except in opcode lookups).
            // NOt for SPARCif(rw != intent_execute) reverse ^= e_bit;
            // This macro byteswaps if needed due to host/guest endian differences or the reverse bit.
            auto S = [=](u32 v) -> u32
                { return size>1 && (reverse != CROSS_ENDIAN) ? SwapBytes(v,size) : v; };
            // Read full 32 bits from the memory/device,
            // unless we're going to replace it entirely.
            MemDataRef<size> data;
            if(rw != intent_store || size != 4)
            {
                NOfprintf(stderr,"IOread(%08X) = ", (unsigned)phys_addr);
                data.d.value = IOmap[phys_addr/0x10000].first(phys_addr & ~3);
                NOfprintf(stderr, "%08X\n", (unsigned)data.d.value);
            }
            // Create a reference to the relevant data
            auto& r = data.reffun( (phys_addr & (4-size)) ^ (reverse ? (4-size) : 0) );
            if(rw==intent_store)
            {
                // Write to the relevant data, and commit
                // the entire 32-bit word the memory/device. Byteswap if needed.
                r = S(value);
                NOfprintf(stderr,"IOwrite(%08X,%08X)\n",(unsigned)phys_addr,(unsigned)data.d.value);
                IOmap[phys_addr/0x10000].second(phys_addr & ~3, data.d.value);
            }
            else
                value = S(r); // Read the relevant data. Byteswap if needed.
        }
        catch(const std::bad_function_call& c)
        {
            //fprintf(stderr, "MemAccess(virt=%08X,phys=%08X,size=%u,reverse=%s,mode=%s,UM=%d,VM=%d> failed because of bad function call!\n",
            u8 FT = 1;
            fault_status_reg = 0x0 | ((level&0x3) << 8) | FT << 2;
            fault_address_reg = virt_addr & ~0xfff;
            return -4;
        }
        return 0;
    }

};





#endif // _MMU_H_

