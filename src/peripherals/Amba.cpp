#include "Amba.h"

#include "gaisler/ambapp.h"

#define GR712RC_SYSTEMID 0x7120e70
#define GR740_REV0_SYSTEMID 0x07401038
#define GR740_REV1_SYSTEMID 0x07401039

#include "gaisler/ambapp_ids.h"

#define AMB_VERSION 0x0
#define AMB_VERSION_1 0x1


void amba_ahb_setup(SDRAM2& io_area) {
    // Set AHB CTRL device id @ 0xfffffff0
    io_area.Write(0x000ffff0/4, 0x0 );
    //io_area.Write(0x000ffff0/4, GR740_REV1_SYSTEMID);


    // Set AMBA AHB CTRL entires
    
    // mst0 at 0xfffff000 - LEON3
    auto p = io_area.getPtr(0x000ff000/4);
    ambapp_pnp_ahb* ahb = reinterpret_cast<ambapp_pnp_ahb*>(p); 
    ahb->id = (VENDOR_GAISLER << 24) | (GAISLER_LEON3 << 12) | (AMB_VERSION << 5);

    // mst1 at 0xfffff020 - AHB Debug UART
    //p = io_area.getPtr(0x000ff020/4);
    //ahb = reinterpret_cast<ambapp_pnp_ahb*>(p); 
    //ahb->id = (VENDOR_GAISLER << 24) | (GAISLER_AHBUART << 12) | (AMB_VERSION << 5);


    // slv0 at 0xfffff800 - AHBAPB Bridge with pnp, type 1 (APB IO AREA)
    p = io_area.getPtr(0x000ff800/4);
    ahb = reinterpret_cast<ambapp_pnp_ahb*>(p); 
    ahb->id = (VENDOR_GAISLER << 24) | (GAISLER_APBMST << 12) | (AMB_VERSION << 5);
    ahb->mbar[0] = (0x800 << 20) | (0x0 << 16) | (0xfff << 4) | 2; // 1 Mb Mask 
    //                             not P or C                   TSUM uses 2...        

    // slv0 at 0xfffff820 - Memory controller 0x60000000, size 16MB, type 0010 (AHB Memory area)
    p = io_area.getPtr(0x000ff820/4);
    ahb = reinterpret_cast<ambapp_pnp_ahb*>(p); 
    ahb->id = (VENDOR_ESA << 24) | (ESA_MCTRL << 12) | (AMB_VERSION << 5);
    //ahb->mbar[0] = (0x600 << 20) | (0x3 << 16) | (0xff0 << 4) | 2; // 16Mb mask
    //ahb->mbar[0] = (0x600 << 20) | (0x3 << 16) | (0xf00 << 4) | 2; // 256Mb mask
 
    // TSIM: memory controller @ 820
    // 0xfffff820  0400f000  00000000  00000000  00000000    ................
    // 0xfffff830  0003e002  2000e002  4003c002  00000000    .... ...@.......
    ahb->mbar[0] = (0x000 << 20) | (0x3 << 16) | (0xe00 << 4) | 2; //0x0003e002;
    ahb->mbar[1] = (0x200 << 20) | (0x0 << 16) | (0xe00 << 4) | 2; //0x2000e002;
    ahb->mbar[2] = (0x400 << 20) | (0x3 << 16) | (0xc00 << 4) | 2; //0x4003c002;
 
}

void amba_apb_setup(SDRAM2& io_area, u32 base) {

    // slv0 at 0x800ff000 - European Space Agency Leon2 Memory Controller
    auto p = io_area.getPtr((0x800ff000-base)/4);
    ambapp_pnp_apb* apb = reinterpret_cast<ambapp_pnp_apb*>(p); 
    apb->id = (VENDOR_ESA << 24) | (ESA_MCTRL << 12) | (AMB_VERSION << 5);
    apb->iobar = (0x000 << 20) | (0xfff << 4) | AMBA_TYPE_APBIO; // 80000000 - 800000ff

    // slv1 at 0x800ff008 - APBUART IRQ 2
    p = io_area.getPtr((0x800ff008-base)/4);
    apb = reinterpret_cast<ambapp_pnp_apb*>(p); 
    apb->id = (VENDOR_GAISLER << 24) | (GAISLER_APBUART << 12) | (AMB_VERSION_1 << 5) | (0x2 & 0xf) ;
    apb->iobar = (0x001 << 20) | (0xfff << 4) | AMBA_TYPE_APBIO; // 80000100 - 800001ff

    // slv2 at 0x800ff010 - Interrupt Controller
    p = io_area.getPtr((0x800ff010-base)/4);
    apb = reinterpret_cast<ambapp_pnp_apb*>(p); 
    apb->id = (VENDOR_GAISLER << 24) | (GAISLER_IRQMP << 12) | (AMB_VERSION << 5);
    apb->iobar = (0x002 << 20) | (0xfff << 4) | AMBA_TYPE_APBIO; // 80000200 - 800002ff

    // slv3 at 0x800ff018 - Timer Unit IRQ 8
    p = io_area.getPtr((0x800ff018-base)/4);
    apb = reinterpret_cast<ambapp_pnp_apb*>(p); 
    apb->id = (VENDOR_GAISLER << 24) | (GAISLER_GPTIMER << 12) | (AMB_VERSION << 5) | (0x8 & 0xf);
    apb->iobar = (0x003 << 20) | (0xfff << 4) | AMBA_TYPE_APBIO; // 80000300 - 800003ff

    // slv7 at 0x800ff020 - APB UART IRQ 3
    p = io_area.getPtr((0x800ff020-base)/4);
    apb = reinterpret_cast<ambapp_pnp_apb*>(p); 
    apb->id = (VENDOR_GAISLER << 24) | (GAISLER_APBUART << 12) | (AMB_VERSION_1 << 5) | (0x3 & 0xf);
    apb->iobar = (0x009 << 20) | (0xfff << 4) | AMBA_TYPE_APBIO; // 80000900 - 800009ff


}
 
