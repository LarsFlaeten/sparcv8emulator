// SPDX-License-Identifier: MIT
#ifndef _SVGA_H_
#define _SVGA_H_

#include "apb_slave.h"
#include "gaisler/ambapp_ids.h"
#include "Display.h"
#include "MCTRL.h"

class SVGA : public apb_slave {
    
private:
    // regs
    u32 stat;
    u32 vlen;
    u32 fporch;
    u32 synlen;
    u32 linlen;
    u32 fbuf;
    u32 dclk0;
    u32 dclk1;
    u32 dclk2;
    u32 dclk3;
    u32 clut;

    Display display;
    MCtrl&  mctrl;
public:
    SVGA(MCtrl& mctrl) : display(640, 480, 32, 60), mctrl(mctrl) {
        stat = 0;
        dclk0 = 40000;
        dclk1 = 20000;
        dclk2 = 15385;
        dclk3 = 0;
        clut = 0;

        // rest of regs are set in reset
        reset();
    }

    u32 vendor_id() const {return VENDOR_GAISLER;}
    u32 device_id() const {return GAISLER_SVGACTRL;}
    

    u32 read(u32 offset) const {
        u32 ret;
        switch(offset) {
            case(0x0): ret = stat; break;
            case(0x4): ret = vlen; break;
            case(0x8): ret = fporch; break;
            case(0xc): ret = synlen; break;
            case(0x10): ret = linlen; break;
            case(0x14): ret = fbuf; break;
            case(0x18): ret = dclk0; break;
            case(0x1c): ret = dclk1; break;
            case(0x20): ret = dclk2; break;
            case(0x24): ret = dclk3; break;
            case(0x28): ret = clut; break;
            
            default: throw std::runtime_error("SVGA: Read outside regs address space.");
        }

        std::cout << "SVGACTRL read offset:" << std::hex << offset << ", ret: " << ret << std::dec << "\n";
        
        return ret;
    }

    virtual void write(u32 offset, u32 value) {
        std::cout << "SVGACTRL write offset:" << std::hex << offset << ", val: " << value << std::dec << "\n";
        u32* video_mem_ptr = 0;
        switch(offset) {
            case(0x0):
                if(value & 0x2) {
                    reset();
                    break;
                }
                if(value & 0x1) {
                    if(!display.isEnabled())
                        display.enable();
                } else if((value & 0x1) == 0) {
                    if(display.isEnabled())
                        display.disable(false);
                }
                stat = value;
                break;
            case(0x4):
                vlen = value;
                break;
            case(0x8):
                fporch = value;
                break;
            case(0xc):
                synlen = value;
                break;
            case(0x10):
                linlen = value;
                break;
            case(0x14): {
                fbuf = value;
                auto p = mctrl.find_bank_or_null(value);
                if(p) {
                    video_mem_ptr = p->get_ptr();
                    display.set_framebuffer(video_mem_ptr);
                } else
                    std::cout << "Error: Video init: No memory mapped at " << std::hex << value << std::dec << "\n";
                break;
            }
            case(0x28):
                clut = value;
                throw std::runtime_error("SVGA: CLUT writes to be implemented.");
                break;
            default:
                throw std::runtime_error("SVGA: Write outside regs address space or ro value.");


        }
    }

    void reset() {
        // STAT: HPOL (bit 9), VPOL (bit 8) and BDSEL (bit 4,5) not reset
        stat = stat & (0 | (0x3 << 4) | (0x3 << 8));

        vlen = 0;
        fporch = 0;
        synlen = 0;
        linlen = 0;
        fbuf = 0;

        // clocks and CLUT not reset..    
    
        // Start up our display
        if(!display.isRunning())
            display.start();
        
                    
    }

};


#endif