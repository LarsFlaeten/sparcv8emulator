#include "debug.h"
#include "sparcv8/MMU.h"

#include <string>
#include <iomanip>






void debug_dumpmem(u32 pa, int n) {
    if(n < 0) n = 1;

    if(n > 16) n = 16;
   
    int count = 0;

    for(int i = 0; i < 4; ++i) {
        std::cout << std::hex << "0x" << pa + 4*i*4;
        std::string strrep;
        for(int j = 0; j < 4; ++j) {
            u32 v = MMU::MemAccessBypassRead4(pa + 4*(i*4 + j), CROSS_ENDIAN);
            std::cout <<  "  " << std::setfill('0') << std::setw(8) << v;
            char a = v & 0xff;
            char b = (v & 0xff00) >> 8;
            char c = (v & 0xff0000) >> 16;
            char d = (v & 0xff000000) >> 24;
            if( a >= 33 && a <= 126) strrep.push_back(a); else strrep.push_back('.');
            if( b >= 33 && b <= 126) strrep.push_back(b); else strrep.push_back('.');
            if( c >= 33 && c <= 126) strrep.push_back(c); else strrep.push_back('.');
            if( d >= 33 && d <= 126) strrep.push_back(d); else strrep.push_back('.');
            strrep.push_back(' ');

            ++count;
            if(count >= n)
                break;
        }
        std::cout << "  " << strrep << std::endl;
        
        if(count >= n)
            break;
    }
}

void debug_dumpmemv(u32 va, int n) {
    if(n < 0) n = 1;

    if(n > 16) n = 16;
    
    int count = 0;

    for(int i = 0; i < 4; ++i) {
        std::cout << std::hex << "0x" << va + 4*i*4;
        std::string strrep;
        for(int j = 0; j < 4; ++j) {
            u32 v;
            MMU::MemAccess(va + 4*(i*4 + j), v, CROSS_ENDIAN, true);
            std::cout <<  "  " << std::setfill('0') << std::setw(8) << v;
            char a = v & 0xff;
            char b = (v & 0xff00) >> 8;
            char c = (v & 0xff0000) >> 16;
            char d = (v & 0xff000000) >> 24;
            if( a >= 33 && a <= 126) strrep.push_back(a); else strrep.push_back('.');
            if( b >= 33 && b <= 126) strrep.push_back(b); else strrep.push_back('.');
            if( c >= 33 && c <= 126) strrep.push_back(c); else strrep.push_back('.');
            if( d >= 33 && d <= 126) strrep.push_back(d); else strrep.push_back('.');
            strrep.push_back(' ');
             
            ++count;
            if(count >= n)
                break;
        }
        std::cout << "  " << strrep << std::endl;
             
        if(count >= n)
                break;
    }
}


void debug_registerdump(CPU& cpu) {

    cpu.dump_regs(true);
    std::cout << "\n";
    std::cout << "   psr: " << std::hex << std::setw(8) << std::setfill('0') << cpu.get_psr();
    std::cout << "   wim: " << std::hex << std::setw(8) << std::setfill('0') << cpu.get_wim();
    std::cout << "   tbr: " << std::hex << std::setw(8) << std::setfill('0') << cpu.get_tbr();
    std::cout << "   y: " << std::hex << std::setw(8) << std::setfill('0') << cpu.get_y_reg() << "\n";

}

std::string get_perms(u32 c, u32 acc) {
    std::string ret = "";
    if( (c&0x1) ==  1)
        ret += 'c';
    else
        ret += '-';

    switch(acc) {
        case(0):
            ret += "r--r--";
            break;
        case(1):
            ret += "rw-rw-";
            break;
        case(2):
            ret += "r-xr-x";
            break;
        case(3):
            ret += "rwxrwx";
            break;
        case(4):
            ret += "--x--x";
            break;
        case(5):
            ret += "rw-r--";
            break;
        case(6):
            ret += "r-x---";
            break;
        case(7):
            ret += "rwx---";
            break;
        default:
            ret += "error-";
            break;
    }

    return ret;
}

struct TR {
    u32 start_va;
    u32 end_va;
    u32 start_pa;
    u32 end_pa;
    std::string perms;
    u32 pages;
};


void debug_mmu_tables() {

    std::vector<TR> trs;

    auto ctx_tbl_ptr = MMU::GetCtxTblPtr();
    auto ctx_n = MMU::GetCtxNumber();
    std::cout << "MMU table for CPU 0, ctx " << std::hex << ctx_n << "\n";
        
    // Fetch PTD from the context table 
    u32 l1_tbl_ptr = (ctx_tbl_ptr << 4) + ctx_n * 4;
    u32 ptd = MMU::MemAccessBypassRead4(l1_tbl_ptr, CROSS_ENDIAN);
    //std::cout << "  PTE " << lx << "\n";
    
    u32 et = ptd & 0x3;
    u32 ptp = (ptd & ~0x3) << 4; 

    if(et != 0x1) {
        std::cout << " error level 0 ptd " << ptd << ", et should be 1\n";
        return;           
    }

    // Level 1 page table is 1024 bytes
    for(int i = 0; i < 1024; i += 4) {
        ptd = MMU::MemAccessBypassRead4(ptp + i, CROSS_ENDIAN);
        et = ptd & 0x3;
        // i = bits 24-31 of virtual address
        if(et == 2) {
            // PTE
            u32 pte = ptd;
            u32 ppn = pte >> 8;
            u32 c = (pte >> 7) & 0x1;
            u32 acc = (pte >> 2) & 0x7;
            u32 start_va = ((i/4)<<24);
            TR tr = { start_va, start_va + 0xffffff, ppn << 12, (ppn << 12) + 0xffffff, get_perms(c, acc), 4096 };
            trs.push_back(tr);
       } else if(et == 1) {
           //PTD
           u32 ptp_l1 = ptd >> 2;
           for(int j = 0; j < 256; j += 4) {
                u32 ptd_l2 = MMU::MemAccessBypassRead4((ptp_l1 << 6) + j, CROSS_ENDIAN);
                u32 et_l2 = ptd_l2 & 0x3;
                if(et_l2 == 2) {
                    // PTE
                    u32 pte_l2 = ptd_l2;
                    u32 ppn_l2 = pte_l2 >> 8;
                    u32 c = (pte_l2 >> 7) & 0x1;
                    u32 acc = (pte_l2 >> 2) & 0x7;
                    u32 start_va = ((i/4)<<24) + ((j/4)<<18);

                    TR tr = { start_va, start_va + 0xfffff, ppn_l2 << 12, (ppn_l2 << 12) + 0xfffff, get_perms(c, acc), 256 };
                    trs.push_back(tr);
                } else if(et_l2 == 1) {
                    u32 ptp_l2 = ptd_l2 >> 2;
                    for(int k = 0; k < 256; k += 4) {
                        u32 ptd_l3 = MMU::MemAccessBypassRead4((ptp_l2 << 6) + k, CROSS_ENDIAN);
                        u32 et_l3 = ptd_l3 & 0x3;
                        if(et_l3 == 2) {
                            // PTE
                            u32 pte_l3 = ptd_l3;
                            u32 ppn_l3 = pte_l3 >> 8;
                            u32 c = (pte_l3 >> 7) & 0x1;
                            u32 acc = (pte_l3 >> 2) & 0x7;
                            u32 start_va = ((i/4)<<24) + ((j/4)<<18) + ((k/4)<<12);

                            TR tr = { start_va, start_va + 0xfff, ppn_l3 << 12, (ppn_l3 << 12) + 0xfff, get_perms(c, acc), 1 };
                            trs.push_back(tr);
         


                        }
 
                    }
 


                } 
           }
        


       }
    }

    // Truncate / collapse trs
    for(int i = 0; i < trs.size()-1; ++i) {
        if(         ((trs[i].end_va + 1) == trs[i+1].start_va) 
                &&  ((trs[i].end_pa + 1) == trs[i+1].start_pa)
                &&  (trs[i].perms.compare(trs[i+1].perms) == 0 )    ) {
            // We can collapse these two:
            trs[i+1].start_va = trs[i].start_va;
            trs[i+1].start_pa = trs[i].start_pa;
            trs[i+1].pages += trs[i].pages;
            trs[i].pages = 0;
            trs[i].start_va = 0;
            trs[i].end_va = 0;
        }

    }


    for(auto& tr : trs) {
        if(tr.pages > 0)
            std::cout << std::hex << "0x" << tr.start_va << "-0x" << tr.end_va << " -> 0x" << tr.start_pa << "-0x" << tr.end_pa << " " << tr.perms << " [" << std::dec << tr.pages << " pages]\n";
    }

}
 
