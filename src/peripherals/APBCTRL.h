#ifndef _APBCTRL_H_
#define _APBCTRL_H_

#include "IRQMP.h"
#include "GPTIMER.h"
#include "APBUART.h"

#include "../sparcv8/MMU.h"

// APBCTRL implements the AHB/APB Bridge with pnp.
// Usually resides at 0x800xxxxx on a 1 MB range
//
// The slaves of the APB are instanciated as members
//
class APBCTRL : public IMemoryBank {
    private:    
        size_t size;
        u32 base;

        std::unique_ptr<IMemoryBank> mem;
        APBUART apbuart;
        IRQMP   irq;
        GPTIMER timer;
        APBUART apbuart9;

        bool contains(u32 addr) const override {
            return addr >= base && addr < base + size;
        }
    
    public:
        APBCTRL(u32 base, Endian endian = Endian::Big) : IMemoryBank(endian),
            size(1 * 1024 * 1024 - 4096), // Allways 1 MB - 4096 high bytes
            base(base),
            mem(std::make_unique<RamBank>(0x0, 0x100)), 
            apbuart(),
            irq(),
            timer(8, 31),
            apbuart9()
        {

        }

        GPTIMER& GetTimer() { return timer; }
        IRQMP& GetIntc() { return irq; }
        APBUART& GetUART() {return apbuart;}
        APBUART& GetUART9() {return apbuart9;}

        
        u8 read8(u32 addr) const override {
            throw std::logic_error("read8 not available in APBCTRL");
        }

        void write8(u32 addr, u8 val) override {
            throw std::logic_error("write8 not available in APBCTRL");
        }

        u32 read32(u32 va, bool align = true) const override {
            check_range(va);

            if ( (va & 0xfff00) >> 8 == 0x000) {
                // Return data from 255 bytes Memory range 000-0ff;
                std::cout << "Read APBCTRL, va = " << std::hex << va << std::dec << "\n";
                return mem->read32((va & 0x0ff));        
            } else if ( (va & 0xfff00) >> 8 == 0x001) {
                // Return data from slv 1 (APBUART)
                //std::cout << "Read APBCTRL(APBUART), va = " << std::hex << va << std::dec << "\n";
                return apbuart.read(va & 0x0ff);        
            } else if ( (va & 0xfff00) >> 8 == 0x002) {
                // Return data from slv 2 (IRQMP)
                std::cout << "Read APBCTRL(IRQMP), va = " << std::hex << va << std::dec << "\n";
                return irq.Read(va & 0x0ff);        
            } else if ( (va & 0xfff00) >> 8 == 0x003) {
                // Return data from slv 3 (GRTIMER)
                //std::cout << "Read APBCTRL(GRTIMER), va = " << std::hex << va << std::dec << "\n";
                return timer.Read(va & 0x0ff);        
            } else if ( (va & 0xfff00) >> 8 == 0x009) {
                // Return data from slv 9 (APB UART)
                //std::cout << "Read APBCTRL(APB UART), va = " << std::hex << va << std::dec << "\n";
                return apbuart9.read(va & 0x0ff);        
            } else {
                std::cerr << "APB Master was adressed ouside any registered peripheral.\n";
                return 0;
            } 


        }

        void write32(u32 va, u32 value, bool align = true) override {
            check_range(va);
            
            if ( (va & 0xfff00) >> 8 == 0x000) {
                std::cout << "Write APBCTRL, va = " << std::hex << va << std::dec << "\n";
                // Return data from 255 bytes Memory range 000-0ff;
                mem->write32((va & 0x0ff), value);        
            } else if ( (va & 0xfff00) >> 8 == 0x001) {
                // Return data from slv 1 (APBUART)
                apbuart.write(va & 0x0ff, value);        
            } else if ( (va & 0xfff00) >> 8 == 0x002) {
                std::cout << "Write APBCTRL, va = " << std::hex << va << std::dec << "\n";
                // Return data from slv 2 (IRQMP)
                irq.Write(va & 0x0ff, value);        
            } else if ( (va & 0xfff00) >> 8 == 0x003) {
                //std::cout << "Write APBCTRL, va = " << std::hex << va << std::dec << "\n";
                // Return data from slv 3 (GRTIMER)
                timer.Write(va & 0x0ff, value);        
            } else if ( (va & 0xfff00) >> 8 == 0x009) {
                //std::cout << "Write APBCTRL, va = 0x" << std::hex << va << " -> " << value << std::dec << "\n";
                // Return data from slv 9 (APB UART)
                apbuart9.write(va & 0x0ff, value);        
            } else {
                std::cerr << "APB Master was adressed ouside any registered peripheral.\n";
                return;
            } 
    
            return;
        }

        u32 get_base() const override { return base; }

        u32 get_limit() const override { return base + size; }
        
        void check_range(u32 addr) const {
            if (!contains(addr))
                throw std::out_of_range("ROM access out of range");
        }
};


#endif

