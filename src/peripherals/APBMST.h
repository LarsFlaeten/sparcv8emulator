#ifndef _APBMST_H_
#define _APBMST_H_

#include "IRQMP.h"
#include "GPTIMER.h"
#include "APBUART.h"

#include "../sparcv8/MMU.h"

// APBCTRL implements the AHB/APB Bridge with pnp.
// Usually resides at 0x800xxxxx on a 1 MB range
//
// The slaves of the APB are instanciated as members
//
class APBCTRL {
    private:    
    
        SDRAM2  mem;
        APBUART apbuart;
        IRQMP   irq;
        GPTIMER timer;
        SDRAM2  ahbuart;
    
    public:
        APBCTRL() :  
            mem(0x100), 
            apbuart(),
            irq(),
            timer(),//8, 31),
            ahbuart(0x100)
        {

        }

        GPTIMER& GetTimer() { return timer; }
        IRQMP& GetIntc() { return irq; }
        APBUART& GetUART() {return apbuart;}

        u32 Read(u32 va) {
            if( (va < 0x80000000) || (va > 0x800fffff) )
            {
                std::cerr << "APB Master was adressed out of its range.\n";
                return 0;
            } else if ( (va & 0xfff00) >> 8 == 0x000) {
                // Return data from 255 bytes Memory range 000-0ff;
                std::cout << "Read APBCTRL, va = " << std::hex << va << std::dec << "\n";
                return mem.Read((va & 0x0ff)/4);        
            } else if ( (va & 0xfff00) >> 8 == 0x001) {
                // Return data from slv 1 (APBUART)
                //std::cout << "Read APBCTRL(APBUART), va = " << std::hex << va << std::dec << "\n";
                return apbuart.Read(va & 0x0ff);        
            } else if ( (va & 0xfff00) >> 8 == 0x002) {
                // Return data from slv 2 (IRQMP)
                std::cout << "Read APBCTRL(IRQMP), va = " << std::hex << va << std::dec << "\n";
                return irq.Read(va & 0x0ff);        
            } else if ( (va & 0xfff00) >> 8 == 0x003) {
                // Return data from slv 3 (GRTIMER)
                //std::cout << "Read APBCTRL(GRTIMER), va = " << std::hex << va << std::dec << "\n";
                return timer.Read(va & 0x0ff);        
            } else if ( (va & 0xfff00) >> 8 == 0x009) {
                // Return data from slv 7 (AHB UART)
                //std::cout << "Read APBCTRL(AHB UART), va = " << std::hex << va << std::dec << "\n";
                return ahbuart.Read(va & 0x0ff);        
            } else {
                std::cerr << "APB Master was adressed ouside any registered peripheral.\n";
                return 0;
            } 


        }
        void Write(u32 va, u32 value) {
            if( (va < 0x80000000) || (va > 0x800fffff) )
            {
                std::cerr << "APB Master was adressed out of its range.\n";
                return;
            } else if ( (va & 0xfff00) >> 8 == 0x000) {
                std::cout << "Write APBCTRL, va = " << std::hex << va << std::dec << "\n";
                // Return data from 255 bytes Memory range 000-0ff;
                mem.Write((va & 0x0ff)/4, value);        
            } else if ( (va & 0xfff00) >> 8 == 0x001) {
                // Return data from slv 1 (APBUART)
                apbuart.Write(va & 0x0ff, value);        
            } else if ( (va & 0xfff00) >> 8 == 0x002) {
                std::cout << "Write APBCTRL, va = " << std::hex << va << std::dec << "\n";
                // Return data from slv 2 (IRQMP)
                irq.Write(va & 0x0ff, value);        
            } else if ( (va & 0xfff00) >> 8 == 0x003) {
                //std::cout << "Write APBCTRL, va = " << std::hex << va << std::dec << "\n";
                // Return data from slv 3 (GRTIMER)
                timer.Write(va & 0x0ff, value);        
            } else if ( (va & 0xfff00) >> 8 == 0x009) {
                std::cout << "Write APBCTRL, va = 0x" << std::hex << va << " -> " << value << std::dec << "\n";
                // Return data from slv 7 (AHB UART)
                ahbuart.Write(va & 0x0ff, value);        
            } else {
                std::cerr << "APB Master was adressed ouside any registered peripheral.\n";
                return;
            } 
    
            return;
        }
};


#endif

