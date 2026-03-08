// SPDX-License-Identifier: MIT
#include "Amba.h"

#include "gaisler/ambapp.h"

#define GR712RC_SYSTEMID 0x7120e70
#define GR740_REV0_SYSTEMID 0x07401038
#define GR740_REV1_SYSTEMID 0x07401039

#include "gaisler/ambapp_ids.h"

#define AMB_VERSION 0x0
#define AMB_VERSION_1 0x1

void amba_ahb_pnp_setup(MCtrl& mctrl) {
    // Set AHB CTRL device id @ 0xfffffff0
    //mctrl.write32(0xfffffff0/4, 0x0 );
    //mctrl.write32(0xfffffff0, GR740_REV1_SYSTEMID);


    // Set AMBA AHB CTRL entires
    
    // mst0 at 0xfffff000 - LEON3
    mctrl.write32(0xfffff000, (VENDOR_GAISLER << 24) | (GAISLER_LEON3 << 12) | (AMB_VERSION << 5));

    // mst at 0xfffff020 - AHB Master - PCI target, Memory
    //mctrl.write32(0xfffff020, (VENDOR_GAISLER << 24) | (GAISLER_GRPCI2 << 12) | (AMB_VERSION << 5));
    //mctrl.write32(0xfffff030, (0x200 << 20) | (0x0 << 16) | (0xc00 << 4) | AMBA_TYPE_AHBIO); // 1 Mb Mask 
    
    // mst at 0xfffff040 - AHB Master - PCI target, IO area
    //mctrl.write32(0xfffff040, (VENDOR_GAISLER << 24) | (GAISLER_GRPCI2 << 12) | (AMB_VERSION << 5) | (0x6 & 0xf));
    //mctrl.write32(0xfffff050, (0x500 << 20) | (0x0 << 16) | (0xc00 << 4) | AMBA_TYPE_AHBIO); // 1 Mb Mask 
    //                                                       0xc00 = 256kb

    // slv0 at 0xfffff800 - AHBAPB Bridge with pnp, type 1 (APB IO AREA)
    //ahb->id
    mctrl.write32(0xfffff800, (VENDOR_GAISLER << 24) | (GAISLER_APBMST << 12) | (AMB_VERSION << 5));
    // ahb->mbar[0]
    mctrl.write32(0xfffff810, (0x800 << 20) | (0x0 << 16) | (0xfff << 4) | 2); // 1 Mb Mask 
    //                             not P or C                   TSIM uses 2...        

    // slv0 at 0xfffff820 - Memory controller 0x60000000, size 16MB, type 0010 (AHB Memory area)
    mctrl.write32(0xfffff820, (VENDOR_ESA << 24) | (ESA_MCTRL << 12) | (AMB_VERSION << 5));
    //ahb->mbar[0] = (0x600 << 20) | (0x3 << 16) | (0xff0 << 4) | 2; // 16Mb mask
    //ahb->mbar[0] = (0x600 << 20) | (0x3 << 16) | (0xf00 << 4) | 2; // 256Mb mask
 
    // TSIM: memory controller @ 820
    // 0xfffff820  0400f000  00000000  00000000  00000000    ................
    // 0xfffff830  0003e002  2000e002  4003c002  00000000    .... ...@.......
    mctrl.write32(0xfffff830, (0x000 << 20) | (0x3 << 16) | (0xe00 << 4) | 2); //0x0003e002;
    mctrl.write32(0xfffff834, (0x200 << 20) | (0x0 << 16) | (0xe00 << 4) | 2); //0x2000e002;
    mctrl.write32(0xfffff838, (0x400 << 20) | (0x3 << 16) | (0xc00 << 4) | 2); //0x4003c002;
 #if 1
    // ahb slv - PCI Initiator interface MEM, IRQ 5
    mctrl.write32(0xfffff840, (VENDOR_GAISLER << 24) | (GAISLER_GRPCI2 << 12) | (AMB_VERSION << 5));// | (0x5 & 0xf));
    mctrl.write32(0xfffff850, (0x240 << 20) | (0x0 << 16) | (0xff0 << 4) | AMBA_TYPE_MEM); // 16 Mb Mask 
    mctrl.write32(0xfffff854, (0xa00 << 20) | (0x0 << 16) | (0xe00 << 4) | AMBA_TYPE_AHBIO); // 1 Mb Mask 
#endif  
    // ahb slv - PCI Initiator interface IO, IRQ 6
    //mctrl.write32(0xfffff860, (VENDOR_GAISLER << 24) | (GAISLER_GRPCI2 << 12) | (AMB_VERSION << 5) | (0x6 & 0xf));
    //mctrl.write32(0xfffff870, (0xa00 << 20) | (0x0 << 16) | (0xe00 << 4) | AMBA_TYPE_AHBIO); // 1 Mb Mask 
    

}

/*
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
*/
void amba_apb_pnp_setup(MCtrl& mctrl) {

    // slv0 at 0x800ff000 - European Space Agency Leon2 Memory Controller
    mctrl.write32(0x800ff000, (VENDOR_ESA << 24) | (ESA_MCTRL << 12) | (AMB_VERSION << 5));
    mctrl.write32(0x800ff004, (0x000 << 20) | (0xfff << 4) | AMBA_TYPE_APBIO); // 80000000 - 800000ff

    // slv1 at 0x800ff008 - APBUART IRQ 4
    mctrl.write32(0x800ff008, (VENDOR_GAISLER << 24) | (GAISLER_APBUART << 12) | (AMB_VERSION_1 << 5) | (0x4 & 0xf) );
    mctrl.write32(0x800ff00c, (0x001 << 20) | (0xfff << 4) | AMBA_TYPE_APBIO); // 80000100 - 800001ff

    // slv2 at 0x800ff010 - Interrupt Controller
    mctrl.write32(0x800ff010, (VENDOR_GAISLER << 24) | (GAISLER_IRQMP << 12) | (AMB_VERSION << 5));
    mctrl.write32(0x800ff014, (0x002 << 20) | (0xfff << 4) | AMBA_TYPE_APBIO); // 80000200 - 800002ff

    // slv3 at 0x800ff018 - Timer Unit IRQ 8
    mctrl.write32(0x800ff018, (VENDOR_GAISLER << 24) | (GAISLER_GPTIMER << 12) | (AMB_VERSION << 5) | (0x8 & 0xf));
    mctrl.write32(0x800ff01c, (0x003 << 20) | (0xfff << 4) | AMBA_TYPE_APBIO); // 80000300 - 800003ff

    // slv7 at 0x800ff028 - APB UART IRQ 3
    mctrl.write32(0x800ff020, (VENDOR_GAISLER << 24) | (GAISLER_APBUART << 12) | (AMB_VERSION_1 << 5) | (0x3 & 0xf));
    mctrl.write32(0x800ff024, (0x009 << 20) | (0xfff << 4) | AMBA_TYPE_APBIO); // 80000900 - 800009ff

#if 1
    // slv4 at 0x800ff020 - PCI BRIDGE CTRL APB IRQ 2
    mctrl.write32(0x800ff028, (VENDOR_GAISLER << 24) | (GAISLER_GRPCI2 << 12) | (AMB_VERSION << 5) | (0x2 & 0xf));
    mctrl.write32(0x800ff02c, (0x004 << 20) | (0xfff << 4) | AMBA_TYPE_APBIO); // 80000400 - 800004ff
#endif
    // slv4 at 0x800ff020 - GLEICHMANN AC97 IRQ 5
    //mctrl.write32(0x800ff028, (VENDOR_GLEICHMANN << 24) | (GLEICHMANN_AC97 << 12) | (AMB_VERSION << 5) | (0x5 & 0xf));
    //mctrl.write32(0x800ff02c, (0x004 << 20) | (0xfff << 4) | AMBA_TYPE_APBIO); // 80000400 - 800004ff

    
    // slv4 at 0x800ff020 - SVGA CTRL IRQ 9
    //mctrl.write32(0x800ff030, (VENDOR_GAISLER << 24) | (GAISLER_SVGACTRL << 12) | (AMB_VERSION << 5) | (0x9 & 0xf));
    //mctrl.write32(0x800ff034, (0x005 << 20) | (0xfff << 4) | AMBA_TYPE_APBIO); // 80000500 - 800005ff



}
 
/*
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
*/
