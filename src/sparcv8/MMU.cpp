#include "MMU.h"

std::pair< std::function<u32(u32)>, std::function<void(u32,u32)> > MMU::IOmap[0x10000];

u32 MMU::control_reg = 0x0;
u32 MMU::ccr = 0x0;
u32 MMU::iccr = 0x0;
u32 MMU::dccr = 0x0;
u32 MMU::ctx_tbl_ptr = 0x0;
u32 MMU::ctx_n = 0x0;
u32 MMU::last_ctx_n = 0x0;
u32 MMU::fault_status_reg = 0x0;
u32 MMU::fault_address_reg = 0x0;

//std::vector<std::pair<u32, SDRAM2>> MMU::base_addrs_regions;

// Default 1MB at 0x0, can be overwritten by addRamRegion
//u32 MMU::base_ram = 0x0000;
//SDRAM2 MMU::ram(0x1 << 20);

bool MMU::tlb_miss = true;
MMU::TLBEntry MMU::tlbs[3][3] = { {0,0,0}, {0,0,0} ,{0,0,0}  };
int MMU::tlb_pos[3] = {2,2,2};
/*
void MMU::addRAMRegion(u32 base_paddr, u32 sizebytes) {
	base_ram = base_paddr;    
   	ram = SDRAM2(sizebytes); 

	//base_addrs_regions.push_back({base_paddr, SDRAM2(sizebytes)});
    

    // reshuffle all regions to have incerementing phys base addr 
    //std::sort(base_addrs_regions.begin(), base_addrs_regions.end(), [](auto &left, auto &right)	{ return left.first < right.first; });

}
*/

