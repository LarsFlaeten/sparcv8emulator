// SPDX-License-Identifier: MIT
#pragma once

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

    bool display_started = false;

public:
    SVGA(MCtrl& mctrl) : display(640, 480, 32, 60), mctrl(mctrl) {
        stat = 0;
        dclk0 = 40000;
        dclk1 = 20000;
        dclk2 = 15385;
        dclk3 = 0;
        clut = 0;
        vlen = 0;
        fporch = 0;
        synlen = 0;
        linlen = 0;
        fbuf = 0;

        // Start the display thread and open a blank window immediately.
        // Resolution and framebuffer will be updated when Linux configures the SVGA.
        display.start();
        display_started = true;
        display.enable();
    }

    u32 vendor_id() const {return VENDOR_GAISLER;}
    u32 device_id() const {return GAISLER_SVGACTRL;}

    void reset() override {
        stat = stat & ((0x3 << 4) | (0x3 << 8)); // preserve BDSEL/polarity
        vlen = fporch = synlen = linlen = fbuf = 0;
        if (display.isEnabled())
            display.disable(true);
    }


    u32 read(u32 offset) const {
        switch(offset) {
            case(0x0):  return stat;
            case(0x4):  return vlen;
            case(0x8):  return fporch;
            case(0xc):  return synlen;
            case(0x10): return linlen;
            case(0x14): return fbuf;
            case(0x18): return dclk0;
            case(0x1c): return dclk1;
            case(0x20): return dclk2;
            case(0x24): return dclk3;
            case(0x28): return clut;
            default:    return 0;
        }
    }

    virtual void write(u32 offset, u32 value) {
        switch(offset) {
            case(0x0): {
                if (value & 0x2) {
                    reset();
                    break;
                }
                bool enable_req = (value & 0x1) != 0;
                bool was_enabled = (stat & 0x1) != 0;
                stat = value;
                if (enable_req && !was_enabled) {
                    // Derive resolution from vlen register: [(yres-1)<<16 | (xres-1)]
                    // grvga driver doesn't write timing registers (leaves PROM defaults),
                    // so only call set_resolution if vlen was explicitly programmed.
                    if (vlen != 0) {
                        int xres = (vlen & 0xFFFF) + 1;
                        int yres = ((vlen >> 16) & 0xFFFF) + 1;
                        display.set_resolution(xres, yres);
                    }
                    if (!display_started) {
                        display.start();
                        display_started = true;
                    }
                    display.enable();
                } else if (!enable_req && was_enabled) {
                    display.disable(false);
                }
                break;
            }
            case(0x4):  vlen   = value; break;
            case(0x8):  fporch = value; break;
            case(0xc):  synlen = value; break;
            case(0x10): linlen = value; break;
            case(0x14): {
                fbuf = value;
                auto* p = mctrl.find_bank_or_null(value);
                if (p) {
                    // Compute pointer at exact physical address, not just bank start
                    u8* bank_start = reinterpret_cast<u8*>(p->get_ptr());
                    u8* fb_ptr = bank_start + (value - p->get_base());
                    display.set_framebuffer(fb_ptr);
                }
                break;
            }
            case(0x18): dclk0 = value; break;
            case(0x1c): dclk1 = value; break;
            case(0x20): dclk2 = value; break;
            case(0x24): dclk3 = value; break;
            case(0x28): clut  = value; break;
            default: break;
        }
    }
};
