#pragma once

// amba_pnp_dump.hpp
#include <cstdint>
#include <functional>
#include <string>
#include <cstdio>
#include <array>
#include <vector>

#include "gaisler/ambapp.h"


struct AmbaIdName {
    uint32_t vendor;   // 8-bit vendor id in top byte of word0
    uint32_t device;   // device id (implementation-defined width; we'll match on upper 12 bits extracted)
    const char* name;
};

static std::vector<AmbaIdName> get_amba_names();

// Provide your memory read primitive (must be safe for MMIO)
using Read32 = std::function<uint32_t(uint32_t addr)>;

struct AmbaAHBPnpSlot {
    uint32_t w0;  // Vendor, device, irq, version, irq
    uint32_t w1;  // UD1
    uint32_t w2;  // UD2
    uint32_t w3;  // Ud3
    uint32_t bar0;  // 
    uint32_t bar1;  // 
    uint32_t bar2;  // 
    uint32_t bar3;  // 
    
    uint32_t addr; // address of w0 in memory map
    bool     empty() const { return (w0 | w1 | w2 | w3 | bar0 | bar1 | bar2 | bar3) == 0; }
};


struct AmbaAPBPnpSlot {
    uint32_t w0;  // ID/misc (layout depends on bus/type)
    uint32_t w1;  // bar
    uint32_t addr; // address of w0 in memory map
    uint32_t base;
    bool     empty() const { return (w0 | w1 ) == 0; }
};

static inline const char* lookup_name(const std::vector<AmbaIdName> &vec,
                                      uint32_t vendor8, uint32_t device12)
{
    for (const auto& it : vec) {
        if (it.vendor == vendor8 && it.device == device12) return it.name;
    }
    return nullptr;
}

// Heuristic extraction commonly seen in AMBA/GRLIB PnP words.
// Even if your exact layout differs, we still print raw words.
struct DecodedId {
    uint32_t vendor8;   // top 8 bits of w0
    uint32_t device12;  // bits 12..23 of w0 (heuristic)
    uint32_t version5;  // bits 5..9 (heuristic)
    uint32_t index5;    // bits 0..4 (slot-local index)
};


static inline void print_slot_line_apb(const char* bus,
                                   const char* sect,
                                   int slot,
                                   const AmbaAPBPnpSlot& s,
                                   const std::vector<AmbaIdName>& names)
{
    if (s.empty()) return;

    auto vendor = ambapp_pnp_vendor(s.w0);
    auto device = ambapp_pnp_device(s.w0); 
    const char* nm = lookup_name(names, vendor, device);

    

    std::printf("[%s:%s] slot=%2d  w0=%08X w1=%08X @%08X\n",
                bus, sect, slot, s.w0, s.w1, s.addr);

    std::printf("    vendor=0x%02X  device=0x%03X  ver=%u  irq=%u",
                vendor, 
                device,
                ambapp_pnp_ver(s.w0),
                ambapp_pnp_irq(s.w0));

    if (nm) std::printf("  (%s)", nm);
    std::printf("\n");

    std::printf("    bar: addr=0x%08X  mask=0x%08X  type=%u\n",
        ambapp_pnp_apb_start(s.w1, s.base ),
        ambapp_pnp_apb_mask(s.w1),
        ambapp_pnp_mbar_type(s.w1));
    
}


static inline void print_slot_line_ahb(const char* bus,
                                   const char* sect,
                                   int slot,
                                   const AmbaAHBPnpSlot& s,
                                   const std::vector<AmbaIdName>& names)
{
    if (s.empty()) return;

    auto vendor = ambapp_pnp_vendor(s.w0);
    auto device = ambapp_pnp_device(s.w0); 
    
    const char* nm = lookup_name(names, vendor, device);

    

    std::printf("[%s:%s] slot=%2d  w0=%08X w1=%08X w2=%08X w3=%08X  @%08X\n",
                bus, sect, slot, s.w0, s.w1, s.w2, s.w3, s.addr);

    std::printf("    vendor=0x%02X  device=0x%03X  ver=%u  irq=%u",
                vendor, 
                device,
                ambapp_pnp_ver(s.w0),
                ambapp_pnp_irq(s.w0));

    if (nm) std::printf("  (%s)", nm);
    std::printf("\n");

    // Print bars
    for(int i = 0; i < 4; ++i)
    {
        auto bar = *(&s.bar0 + i);
        auto type = ambapp_pnp_mbar_type(bar);
        auto start_addr = (type == AMBA_TYPE_MEM) ? ambapp_pnp_mbar_start(bar) : ambapp_pnp_ahbio_adr(bar, AMBA_DEFAULT_IOAREA);
        
        if(bar>0) {
            std::printf("\tbar %d:\t start==0x%08X, mask=0x%08X, type=0x%02x (%s)\n", i, 
                start_addr,
                ambapp_pnp_mbar_mask(bar),
                type,
                (type == AMBA_TYPE_MEM) ? "AHB Memory" : "AHBIO"
                );
        } else 
            std::printf("\tbar %d:\t [EMPTY]]\n", i); 
            
    }
    /*
    std::printf("    base=0x%08X", base);
    if (size) std::printf("  size≈0x%llX (%llu bytes)", (unsigned long long)size, (unsigned long long)size);
    std::printf("\n");*/
}


// Read one 8-byte PnP slot (4 words)
static inline AmbaAPBPnpSlot read_slot_apb(const Read32& r32, uint32_t base, int slot_idx) {
    AmbaAPBPnpSlot s{};
    s.addr = base + slot_idx * 8u;
    s.w0 = r32(s.addr + 0);
    s.w1 = r32(s.addr + 4);
    return s;
}

// Read one 32-byte PnP slot (8 words)
static inline AmbaAHBPnpSlot read_slot_ahb(const Read32& r32, uint32_t base, int slot_idx) {
    AmbaAHBPnpSlot s{};
    s.addr = base + slot_idx * 32u;
    s.w0 = r32(s.addr + 0);
    s.w1 = r32(s.addr + 4);
    s.w2 = r32(s.addr + 8);
    s.w3 = r32(s.addr + 12);
    s.bar0 = r32(s.addr + 16);
    s.bar1 = r32(s.addr + 20);
    s.bar2 = r32(s.addr + 24);
    s.bar3 = r32(s.addr + 28);
    
    return s;
}

struct AmbaPnpAddrs {
    uint32_t ahb_master_base = 0xFFFFF000u; // per your note
    uint32_t ahb_slave_base  = 0xFFFFF800u; // per your note
    uint32_t apb_pnp_base    = 0x800FF000u; // per your note
    uint32_t apb_base    = 0x80000000u; // per your note
    int      ahb_master_slots = 16;         // typical GRLIB limit
    int      ahb_slave_slots  = 16;
    int      apb_slots        = 16;         // per APB bridge
};


inline void print_amba_pnp(const Read32& read32)
{
    AmbaPnpAddrs addrs{};
    auto names = get_amba_names();

    std::printf("=========== AMBA Plug-and-Play Dump ===========\n");

    // AHB Masters
    std::printf("-- AHB Masters @ 0x%08X --\n", addrs.ahb_master_base);
    for (int i = 0; i < addrs.ahb_master_slots; ++i) {
        auto s = read_slot_ahb(read32, addrs.ahb_master_base, i);
        if (!s.empty()) print_slot_line_ahb("AHB", "MST", i, s, names);
    }

    // AHB Slaves
    std::printf("-- AHB Slaves  @ 0x%08X --\n", addrs.ahb_slave_base);
    for (int i = 0; i < addrs.ahb_slave_slots; ++i) {
        auto s = read_slot_ahb(read32, addrs.ahb_slave_base, i);
        if (!s.empty()) print_slot_line_ahb("AHB", "SLV", i, s, names);
    }

    // APB (this is per-APB bridge; if you have multiple APB buses, call this per-bus)
    std::printf("-- APB Slaves  @ 0x%08X --\n", addrs.apb_pnp_base);
    for (int i = 0; i < addrs.apb_slots; ++i) {
        auto s = read_slot_apb(read32, addrs.apb_pnp_base, i);
        s.base = addrs.apb_base;
        if (!s.empty()) print_slot_line_apb("APB", "SLV", i, s, names);
    }

    std::printf("===============================================\n");
}






static std::vector<AmbaIdName> get_amba_names() {
    std::vector<AmbaIdName> names;

    names = {
        {VENDOR_GAISLER, GAISLER_LEON2DSU, "LEON2DSU - LEON2 Debug Support Unit"},
        {VENDOR_GAISLER, GAISLER_LEON3, "LEON3 - LEON3 SPARC V8 Processor"},
        {VENDOR_GAISLER, GAISLER_LEON3DSU, "LEON3DSU - LEON3 Debug Support Unit"},
        {VENDOR_GAISLER, GAISLER_ETHAHB, "ETHAHB - OC ethernet AHB interface"},
        {VENDOR_GAISLER, GAISLER_APBMST, "APBMST - AHB/APB Bridge"},
        {VENDOR_GAISLER, GAISLER_AHBUART, "AHBUART - AHB Debug UART"},
        {VENDOR_GAISLER, GAISLER_SRCTRL, "SRCTRL - Simple SRAM Controller"},
        {VENDOR_GAISLER, GAISLER_SDCTRL, "SDCTRL - PC133 SDRAM Controller"},
        {VENDOR_GAISLER, GAISLER_SSRCTRL, "SSRCTRL - Synchronous SRAM Controller"},
        {VENDOR_GAISLER, GAISLER_I2C2AHB, "I2C2AHB - I2C to AHB Bridge"},
        {VENDOR_GAISLER, GAISLER_APBUART, "APBUART - Generic UART"},
        {VENDOR_GAISLER, GAISLER_IRQMP, "IRQMP - Multi-processor Interrupt Ctrl."},
        {VENDOR_GAISLER, GAISLER_AHBRAM, "AHBRAM - Single-port AHB SRAM module"},
        {VENDOR_GAISLER, GAISLER_AHBDPRAM, "AHBDPRAM - Dual-port AHB SRAM module"},
        {VENDOR_GAISLER, GAISLER_GRIOMMU2, "GRIOMMU2 - IOMMU secondary master i/f"},
        {VENDOR_GAISLER, GAISLER_GPTIMER, "GPTIMER - Modular Timer Unit"},
        {VENDOR_GAISLER, GAISLER_PCITRG, "PCITRG - Simple 32-bit PCI Target"},
        {VENDOR_GAISLER, GAISLER_PCISBRG, "PCISBRG - Simple 32-bit PCI Bridge"},
        {VENDOR_GAISLER, GAISLER_PCIFBRG, "PCIFBRG - Fast 32-bit PCI Bridge"},
        {VENDOR_GAISLER, GAISLER_PCITRACE, "PCITRACE - 32-bit PCI Trace Buffer"},
        {VENDOR_GAISLER, GAISLER_DMACTRL, "DMACTRL - PCI/AHB DMA controller"},
        {VENDOR_GAISLER, GAISLER_AHBTRACE, "AHBTRACE - AMBA Trace Buffer"},
        {VENDOR_GAISLER, GAISLER_DSUCTRL, "DSUCTRL - DSU/ETH controller"},
        {VENDOR_GAISLER, GAISLER_CANAHB, "CANAHB - OC CAN AHB interface"},
        {VENDOR_GAISLER, GAISLER_GPIO, "GPIO - General Purpose I/O port"},
        {VENDOR_GAISLER, GAISLER_AHBROM, "AHBROM - Generic AHB ROM"},
        {VENDOR_GAISLER, GAISLER_AHBJTAG, "AHBJTAG - JTAG Debug Link"},
        {VENDOR_GAISLER, GAISLER_ETHMAC, "ETHMAC - GR Ethernet MAC"},
        {VENDOR_GAISLER, GAISLER_SWNODE, "SWNODE - SpaceWire Node Interface"},
        {VENDOR_GAISLER, GAISLER_SPW, "SPW - SpaceWire Serial Link"},
        {VENDOR_GAISLER, GAISLER_AHB2AHB, "AHB2AHB - AHB-to-AHB Bridge"},
        {VENDOR_GAISLER, GAISLER_USBDC, "USBDC - GR USB 2.0 Device Controller"},
        {VENDOR_GAISLER, GAISLER_USB_DCL, "USB_DCL - USB Debug Communication Link"},
        {VENDOR_GAISLER, GAISLER_DDRMP, "DDRMP - Multi-port DDR controller"},
        {VENDOR_GAISLER, GAISLER_ATACTRL, "ATACTRL - ATA controller"},
        {VENDOR_GAISLER, GAISLER_DDRSP, "DDRSP - Single-port DDR266 controller"},
        {VENDOR_GAISLER, GAISLER_EHCI, "EHCI - USB Enhanced Host Controller"},
        {VENDOR_GAISLER, GAISLER_UHCI, "UHCI - USB Universal Host Controller"},
        {VENDOR_GAISLER, GAISLER_I2CMST, "I2CMST - AMBA Wrapper for OC I2C-master"},
        {VENDOR_GAISLER, GAISLER_SPW2, "SPW2 - GRSPW2 SpaceWire Serial Link"},
        {VENDOR_GAISLER, GAISLER_AHBDMA, "AHBDMA - Simple AHB DMA controller"},
        {VENDOR_GAISLER, GAISLER_NUHOSP3, "NUHOSP3 - Nuhorizons Spartan3 IO I/F"},
        {VENDOR_GAISLER, GAISLER_CLKGATE, "CLKGATE - Clock gating unit"},
        {VENDOR_GAISLER, GAISLER_SPICTRL, "SPICTRL - SPI Controller"},
        {VENDOR_GAISLER, GAISLER_DDR2SP, "DDR2SP - Single-port DDR2 controller"},
        {VENDOR_GAISLER, GAISLER_SLINK, "SLINK - SLINK Master"},
        {VENDOR_GAISLER, GAISLER_GRTM, "GRTM - CCSDS Telemetry Encoder"},
        {VENDOR_GAISLER, GAISLER_GRTC, "GRTC - CCSDS Telecommand Decoder"},
        {VENDOR_GAISLER, GAISLER_GRPW, "GRPW - PacketWire to AMBA AHB I/F"},
        {VENDOR_GAISLER, GAISLER_GRCTM, "GRCTM - CCSDS Time Manager"},
        {VENDOR_GAISLER, GAISLER_GRHCAN, "GRHCAN - ESA HurriCANe CAN with DMA"},
        {VENDOR_GAISLER, GAISLER_GRFIFO, "GRFIFO - FIFO Controller"},
        {VENDOR_GAISLER, GAISLER_GRADCDAC, "GRADCDAC - ADC / DAC Interface"},
        {VENDOR_GAISLER, GAISLER_GRPULSE, "GRPULSE - General Purpose I/O with Pulses"},
        {VENDOR_GAISLER, GAISLER_GRTIMER, "GRTIMER - Timer Unit with Latches"},
        {VENDOR_GAISLER, GAISLER_AHB2PP, "AHB2PP - AMBA AHB to Packet Parallel I/F"},
        {VENDOR_GAISLER, GAISLER_GRVERSION, "GRVERSION - Version and Revision Register"},
        {VENDOR_GAISLER, GAISLER_APB2PW, "APB2PW - PacketWire Transmit Interface"},
        {VENDOR_GAISLER, GAISLER_PW2APB, "PW2APB - PacketWire Receive Interface"},
        {VENDOR_GAISLER, GAISLER_GRCAN, "GRCAN - CAN Controller with DMA"},
        {VENDOR_GAISLER, GAISLER_I2CSLV, "I2CSLV - I2C Slave"},
        {VENDOR_GAISLER, GAISLER_U16550, "U16550 - Simple 16550 UART"},
        {VENDOR_GAISLER, GAISLER_AHBMST_EM, "AHBMST_EM - AMBA Master Emulator"},
        {VENDOR_GAISLER, GAISLER_AHBSLV_EM, "AHBSLV_EM - AMBA Slave Emulator"},
        {VENDOR_GAISLER, GAISLER_GRTESTMOD, "GRTESTMOD - Test report module"},
        {VENDOR_GAISLER, GAISLER_ASCS, "ASCS - ASCS Master"},
        {VENDOR_GAISLER, GAISLER_IPMVBCTRL, "IPMVBCTRL - IPM-bus/MVBC memory controller"},
        {VENDOR_GAISLER, GAISLER_SPIMCTRL, "SPIMCTRL - SPI Memory Controller"},
        {VENDOR_GAISLER, GAISLER_L4STAT, "L4STAT - LEON4 Statistics Unit"},
        {VENDOR_GAISLER, GAISLER_LEON4, "LEON4 - LEON4 SPARC V8 Processor"},
        {VENDOR_GAISLER, GAISLER_LEON4DSU, "LEON4DSU - LEON4 Debug Support Unit"},
        {VENDOR_GAISLER, GAISLER_PWM, "PWM - PWM generator"},
        {VENDOR_GAISLER, GAISLER_L2CACHE, "L2CACHE - L2-Cache Controller"},
        {VENDOR_GAISLER, GAISLER_SDCTRL64, "SDCTRL64 - 64-bit PC133 SDRAM Controller"},
        {VENDOR_GAISLER, GAISLER_GR1553B, "GR1553B - MIL-STD-1553B Interface"},
        {VENDOR_GAISLER, GAISLER_1553TST, "1553TST - MIL-STD-1553B Test Device"},
        {VENDOR_GAISLER, GAISLER_GRIOMMU, "GRIOMMU - IO Memory Management Unit"},
        {VENDOR_GAISLER, GAISLER_FTAHBRAM, "FTAHBRAM - Generic FT AHB SRAM module"},
        {VENDOR_GAISLER, GAISLER_FTSRCTRL, "FTSRCTRL - Simple FT SRAM Controller"},
        {VENDOR_GAISLER, GAISLER_AHBSTAT, "AHBSTAT - AHB Status Register"},
        {VENDOR_GAISLER, GAISLER_LEON3FT, "LEON3FT - LEON3FT SPARC V8 Processor"},
        {VENDOR_GAISLER, GAISLER_FTMCTRL, "FTMCTRL - Memory controller with EDAC"},
        {VENDOR_GAISLER, GAISLER_FTSDCTRL, "FTSDCTRL - FT PC133 SDRAM Controller"},
        {VENDOR_GAISLER, GAISLER_FTSRCTRL8, "FTSRCTRL8 - FT 8-bit SRAM/16-bit IO Ctrl"},
        {VENDOR_GAISLER, GAISLER_MEMSCRUB, "MEMSCRUB - AHB Memory Scrubber"},
        {VENDOR_GAISLER, GAISLER_FTSDCTRL64, "FTSDCTRL64 - 64-bit FT SDRAM Controller"},
        {VENDOR_GAISLER, GAISLER_NANDFCTRL, "NANDFCTRL - NAND Flash Controller"},
        {VENDOR_GAISLER, GAISLER_N2DLLCTRL, "N2DLLCTRL - N2X DLL Dynamic Config. i/f"},
        {VENDOR_GAISLER, GAISLER_N2PLLCTRL, "N2PLLCTRL - N2X PLL Dynamic Config. i/f"},
        {VENDOR_GAISLER, GAISLER_SPI2AHB, "SPI2AHB - SPI to AHB Bridge"},
        {VENDOR_GAISLER, GAISLER_DDRSDMUX, "DDRSDMUX - Muxed FT DDR/SDRAM controller"},
        {VENDOR_GAISLER, GAISLER_AHBFROM, "AHBFROM - Flash ROM Memory"},
        {VENDOR_GAISLER, GAISLER_PCIEXP, "PCIEXP - Xilinx PCI EXPRESS Wrapper"},
        {VENDOR_GAISLER, GAISLER_APBPS2, "APBPS2 - PS2 interface"},
        {VENDOR_GAISLER, GAISLER_VGACTRL, "VGACTRL - VGA controller"},
        {VENDOR_GAISLER, GAISLER_LOGAN, "LOGAN - On chip Logic Analyzer"},
        {VENDOR_GAISLER, GAISLER_SVGACTRL, "SVGACTRL - SVGA frame buffer"},
        {VENDOR_GAISLER, GAISLER_T1AHB, "T1AHB - Niagara T1 PCX/AHB bridge"},
        {VENDOR_GAISLER, GAISLER_MP7WRAP, "MP7WRAP - CoreMP7 wrapper"},
        {VENDOR_GAISLER, GAISLER_GRSYSMON, "GRSYSMON - AMBA wrapper for System Monitor"},
        {VENDOR_GAISLER, GAISLER_GRACECTRL, "GRACECTRL - System ACE I/F Controller"},
        {VENDOR_GAISLER, GAISLER_ATAHBSLV, "ATAHBSLV - AMBA Test Framework AHB Slave"},
        {VENDOR_GAISLER, GAISLER_ATAHBMST, "ATAHBMST - AMBA Test Framework AHB Master"},
        {VENDOR_GAISLER, GAISLER_ATAPBSLV, "ATAPBSLV - AMBA Test Framework APB Slave"},
        {VENDOR_GAISLER, GAISLER_MIGDDR2, "MIGDDR2 - Xilinx MIG DDR2 Controller"},
        {VENDOR_GAISLER, GAISLER_LCDCTRL, "LCDCTRL - LCD Controller"},
        {VENDOR_GAISLER, GAISLER_SWITCHOVER, "SWITCHOVER - Switchover Logic"},
        {VENDOR_GAISLER, GAISLER_FIFOUART, "FIFOUART - UART with large FIFO"},
        {VENDOR_GAISLER, GAISLER_MUXCTRL, "MUXCTRL - Analogue multiplexer control"},
        {VENDOR_GAISLER, GAISLER_B1553BC, "B1553BC - AMBA Wrapper for Core1553BBC"},
        {VENDOR_GAISLER, GAISLER_B1553RT, "B1553RT - AMBA Wrapper for Core1553BRT"},
        {VENDOR_GAISLER, GAISLER_B1553BRM, "B1553BRM - AMBA Wrapper for Core1553BRM"},
        {VENDOR_GAISLER, GAISLER_AES, "AES - Advanced Encryption Standard"},
        {VENDOR_GAISLER, GAISLER_ECC, "ECC - Elliptic Curve Cryptography"},
        {VENDOR_GAISLER, GAISLER_PCIF, "PCIF - AMBA Wrapper for CorePCIF"},
        {VENDOR_GAISLER, GAISLER_CLKMOD, "CLKMOD - CPU Clock Switching Ctrl module"},
        {VENDOR_GAISLER, GAISLER_HAPSTRAK, "HAPSTRAK - HAPS HapsTrak I/O Port"},
        {VENDOR_GAISLER, GAISLER_TEST_1X2, "TEST_1X2 - HAPS TEST_1x2 interface"},
        {VENDOR_GAISLER, GAISLER_WILD2AHB, "WILD2AHB - WildCard CardBus interface"},
        {VENDOR_GAISLER, GAISLER_BIO1, "BIO1 - Basic I/O board BIO1"},
        {VENDOR_GAISLER, GAISLER_AESDMA, "AESDMA - AES 256 DMA"},
        {VENDOR_GAISLER, GAISLER_GRPCI2, "GRPCI2 - GRPCI2 PCI/AHB bridge"},
        {VENDOR_GAISLER, GAISLER_GRPCI2_DMA, "GRPCI2_DMA - GRPCI2 DMA interface"},
        {VENDOR_GAISLER, GAISLER_GRPCI2_TB, "GRPCI2_TB - GRPCI2 Trace buffer"},
        {VENDOR_GAISLER, GAISLER_MMA, "MMA - Memory Mapped AMBA"},
        {VENDOR_GAISLER, GAISLER_SATCAN, "SATCAN - SatCAN controller"},
        {VENDOR_GAISLER, GAISLER_CANMUX, "CANMUX - CAN Bus multiplexer"},
        {VENDOR_GAISLER, GAISLER_GRTMRX, "GRTMRX - CCSDS Telemetry Receiver"},
        {VENDOR_GAISLER, GAISLER_GRTCTX, "GRTCTX - CCSDS Telecommand Transmitter"},
        {VENDOR_GAISLER, GAISLER_GRTMDESC, "GRTMDESC - CCSDS Telemetry Descriptor"},
        {VENDOR_GAISLER, GAISLER_GRTMVC, "GRTMVC - CCSDS Telemetry VC Generator"},
        {VENDOR_GAISLER, GAISLER_GEFFE, "GEFFE - Geffe Generator"},
        {VENDOR_GAISLER, GAISLER_GPREG, "GPREG - General Purpose Register"},
        {VENDOR_GAISLER, GAISLER_GRTMPAHB, "GRTMPAHB - CCSDS Telemetry VC AHB Input"},
        {VENDOR_GAISLER, GAISLER_SPWCUC, "SPWCUC - CCSDS CUC / SpaceWire I/F"},
        {VENDOR_GAISLER, GAISLER_SPW2_DMA, "SPW2_DMA - GRSPW Router DMA interface"},
        {VENDOR_GAISLER, GAISLER_SPWROUTER, "SPWROUTER - GRSPW Router"},
        {VENDOR_GAISLER, GAISLER_EDCLMST, "EDCLMST - EDCL master interface"},
        {VENDOR_GAISLER, GAISLER_GRPWTX, "GRPWTX - PacketWire Transmitter with DMA"},
        {VENDOR_GAISLER, GAISLER_GRPWRX, "GRPWRX - PacketWire Receiver with DMA"},
        {VENDOR_GAISLER, GAISLER_GPREGBANK, "GPREGBANK - General Purpose Register Bank"},
        {VENDOR_GAISLER, GAISLER_MIG_7SERIES, "MIG_7SERIES - Xilinx MIG Controller"},
        {VENDOR_GAISLER, GAISLER_GRSPW2_SIST, "GRSPW2_SIST - GRSPW Router SIST"},
        {VENDOR_GAISLER, GAISLER_SGMII, "SGMII - XILINX SGMII Interface"},
        {VENDOR_GAISLER, GAISLER_RGMII, "RGMII - Gaisler RGMII Interface"},
        {VENDOR_GAISLER, GAISLER_IRQGEN, "IRQGEN - Interrupt generator"},
        {VENDOR_GAISLER, GAISLER_GRDMAC, "GRDMAC - GRDMAC DMA Controller"},
        {VENDOR_GAISLER, GAISLER_AHB2AVLA, "AHB2AVLA - Avalon-MM memory controller"},
        {VENDOR_GAISLER, GAISLER_SPWTDP, "SPWTDP - CCSDS TDP / SpaceWire I/F"},
        {VENDOR_GAISLER, GAISLER_L3STAT, "L3STAT - LEON3 Statistics Unit"},
        {VENDOR_GAISLER, GAISLER_GR740THS, "GR740THS - Temperature sensor"},
        {VENDOR_GAISLER, GAISLER_GRRM, "GRRM - Reconfiguration Module"},
        {VENDOR_GAISLER, GAISLER_CMAP, "CMAP - CCSDS Memory Access Protocol"},
        {VENDOR_GAISLER, GAISLER_CPGEN, "CPGEN - Discrete Command Pulse Gen"},
        {VENDOR_GAISLER, GAISLER_AMBAPROT, "AMBAPROT - AMBA Protection Unit"},
        {VENDOR_GAISLER, GAISLER_IGLOO2_BRIDGE, "IGLOO2_BRIDGE - Microsemi SF2/IGLOO2 MSS/HPMS"},
        {VENDOR_GAISLER, GAISLER_AHB2AXI, "AHB2AXI - AMBA AHB/AXI Bridge"},
        {VENDOR_GAISLER, GAISLER_AXI2AHB, "AXI2AHB - AMBA AXI/AHB Bridge"},
        {VENDOR_GAISLER, GAISLER_FDIR_RSTCTRL, "FDIR_RSTCTRL - FDIR Reset Controller"},
        {VENDOR_GAISLER, GAISLER_APB3MST, "APB3MST - AHB/APB3 Bridge"},
        {VENDOR_GAISLER, GAISLER_LRAM, "LRAM - Dual-port AHB(/CPU) On-Chip RAM"},
        {VENDOR_GAISLER, GAISLER_BOOTSEQ, "BOOTSEQ - Custom AHB sequencer"},
        {VENDOR_GAISLER, GAISLER_TCCOP, "TCCOP - CCSDS Telecommand Decoder (COP)"},
        {VENDOR_GAISLER, GAISLER_SPIMASTER, "SPIMASTER - Simple SPI Master"},
        {VENDOR_GAISLER, GAISLER_SPISLAVE, "SPISLAVE - Dual-port SPI Slave"},
        {VENDOR_GAISLER, GAISLER_GRSRIO, "GRSRIO - Serial RapidIO Logical Layer"},
        {VENDOR_GAISLER, GAISLER_AHBLM2AHB, "AHBLM2AHB - AHB-Lite master to AHB master"},
        {VENDOR_GAISLER, GAISLER_AHBS2NOC, "AHBS2NOC - AHB slave to NoC"},
        {VENDOR_GAISLER, GAISLER_TCAU, "TCAU - Authentication Unit"},
        {VENDOR_GAISLER, GAISLER_GRTMDYNVCID, "GRTMDYNVCID - CCSDS Telemetry Dynamic VCID"},
        {VENDOR_GAISLER, GAISLER_RNOCIRQPROP, "RNOCIRQPROP - RNoC Interrupt propagator"},
        {VENDOR_GAISLER, GAISLER_FTADDR, "FTADDR - DDR2/DDR3 controller with EDAC"},
        {VENDOR_GAISLER, GAISLER_ATG, "ATG - AMBA2 Test Pattern Generator"},
        {VENDOR_GAISLER, GAISLER_DFITRACE, "DFITRACE - DFI2.1 Trace Buffer"},
        {VENDOR_GAISLER, GAISLER_SELFTEST, "SELFTEST - TV selftest module"},
        {VENDOR_GAISLER, GAISLER_DFIERRINJ, "DFIERRINJ - DFI error injection module"},
        {VENDOR_GAISLER, GAISLER_DFICHECK, "DFICHECK - DFI timing check module"},
        {VENDOR_GAISLER, GAISLER_GRCANFD, "GRCANFD - CAN-FD Controller with DMA"},
        {VENDOR_GAISLER, GAISLER_NIM, "NIM - Synchronous serial interface"},
        {VENDOR_GAISLER, GAISLER_GRSHYLOC, "GRSHYLOC - SHYLOC Compressor with DMA"},
        {VENDOR_GAISLER, GAISLER_GRTACHOM, "GRTACHOM - Simple Digital Tachometer"},
        {VENDOR_GAISLER, GAISLER_L5STAT, "L5STAT - LEON5 Statistics Unit"},
        {VENDOR_GAISLER, GAISLER_LEON5, "LEON5 - LEON5 SPARC V8 Processor"},
        {VENDOR_GAISLER, GAISLER_LEON5DSU, "LEON5DSU - LEON5 Debug Support Unit"},
        {VENDOR_GAISLER, GAISLER_SPFI, "SPFI - GRSPFI SpaceFibre Serial Link"},
        {VENDOR_GAISLER, GAISLER_RV64GC, "RV64GC - NOEL-V RISC-V Processor"},
        {VENDOR_GAISLER, GAISLER_RVDM, "RVDM - RISC-V Debug Module"},
        {VENDOR_GAISLER, GAISLER_FTMCTRL2, "FTMCTRL2 - Memory controller with EDAC"},
        {VENDOR_GAISLER, GAISLER_GRDMAC2, "GRDMAC2 - GRDMAC2 DMA Controller"},
        {VENDOR_GAISLER, GAISLER_GRSCRUB, "GRSCRUB - GRSCRUB FPGA Scrubber"},
        {VENDOR_GAISLER, GAISLER_GRPLIC, "GRPLIC - RISC-V PLIC"},
        {VENDOR_GAISLER, GAISLER_CLINT, "CLINT - RISC-V CLINT"},
        {VENDOR_GAISLER, GAISLER_SOCBRIDGE, "SOCBRIDGE - SoC to SoC bridge"},
        {VENDOR_GAISLER, GAISLER_NANDFCTRL2, "NANDFCTRL2 - NAND Flash Controller 2"},
        {VENDOR_GAISLER, GAISLER_DARE65THS, "DARE65THS - DARE65T Temperature sensor"},
        {VENDOR_GAISLER, GAISLER_WIZL, "WIZL - GRWIZL WizardLink Serial Link"},
        {VENDOR_GAISLER, GAISLER_HSSL, "HSSL - GRHSSL SpaceFibre + WizardLink"},
        {VENDOR_GAISLER, GAISLER_GRWATCHDOG, "GRWATCHDOG - Watchdog unit with sep clock"},
        {VENDOR_GAISLER, GAISLER_ETRACE, "ETRACE - RISC-V E-trace encoder"},
        {VENDOR_GAISLER, GAISLER_LEON5ADSU, "LEON5ADSU - LEON5 Advanced Debug Unit"},
        {VENDOR_GAISLER, GAISLER_LEON5DMAB, "LEON5DMAB - LEON5 IOMMU and DMA bridge"},
        {VENDOR_GAISLER, GAISLER_ACLINT, "ACLINT - RISC-V ACLINT"},
        {VENDOR_GAISLER, GAISLER_IMSIC, "IMSIC - RISC-V IMSIC"},
        {VENDOR_GAISLER, GAISLER_GRAPLIC, "GRAPLIC - RISC-V APLIC"},
        {VENDOR_GAISLER, GAISLER_L2CL, "L2CL - L2-Cache Controller - Lite"},
        {VENDOR_GAISLER, GAISLER_L2CACHE_IO, "L2CACHE_IO - L2-Cache Controller I/O Port"},
        {VENDOR_GAISLER, GAISLER_AHB2AHB_STR, "AHB2AHB_STR - AHB/AHB Stripe Bridge"},
        {VENDOR_GAISLER, GAISLER_GRIOMMURV, "GRIOMMURV - RISCV IO Memory Management Unit"},
        {VENDOR_ESA, ESA_LEON2, "LEON2 , LEON2 SPARC V8 Processor"},
        {VENDOR_ESA, ESA_LEON2APB, "LEON2APB , LEON2 Peripheral Bus"},
        {VENDOR_ESA, ESA_IRQ, "IRQ , LEON2 Interrupt Controller"},
        {VENDOR_ESA, ESA_TIMER, "TIMER , LEON2 Timer"},
        {VENDOR_ESA, ESA_UART, "UART , LEON2 UART"},
        {VENDOR_ESA, ESA_CFG, "CFG , LEON2 Configuration Register"},
        {VENDOR_ESA, ESA_IO, "IO , LEON2 Input/Output"},
        {VENDOR_ESA, ESA_MCTRL, "MCTRL , LEON2 Memory Controller"},
        {VENDOR_ESA, ESA_PCIARB, "PCIARB , PCI Arbiter"},
        {VENDOR_ESA, ESA_HURRICANE, "HURRICANE , HurriCANe/HurryAMBA CAN Ctrl"},
        {VENDOR_ESA, ESA_SPW_RMAP, "SPW_RMAP , UoD/Saab SpaceWire/RMAP link"},
        {VENDOR_ESA, ESA_AHBUART, "AHBUART , LEON2 AHB Debug UART"},
        {VENDOR_ESA, ESA_SPWA, "SPWA , ESA/ASTRIUM SpaceWire link"},
        {VENDOR_ESA, ESA_BOSCHCAN, "BOSCHCAN , SSC/BOSCH CAN Ctrl"},
        {VENDOR_ESA, ESA_IRQ2, "IRQ2 , LEON2 Secondary Irq Controller"},
        {VENDOR_ESA, ESA_AHBSTAT, "AHBSTAT , LEON2 AHB Status Register"},
        {VENDOR_ESA, ESA_WPROT, "WPROT , LEON2 Write Protection"},
        {VENDOR_ESA, ESA_WPROT2, "WPROT2 , LEON2 Extended Write Protection"},
        {VENDOR_ESA, ESA_PDEC3AMBA, "PDEC3AMBA , ESA CCSDS PDEC3AMBA TC Decoder"},
        {VENDOR_ESA, ESA_PTME3AMBA, "PTME3AMBA , ESA CCSDS PTME3AMBA TM Encoder"},
    };

    return names;
}