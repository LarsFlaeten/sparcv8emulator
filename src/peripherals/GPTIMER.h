#ifndef _GPTIMER_H_
#define _GPTIMER_H_


class GPTIMER {
    private:
        u32 SCALER;
        u32 SRELOAD;
        u32 CONFIG;
        u32 CATCHCFG; 


        struct timer_impl {
            // Timer n:
            u32 TCNTVAL;
            u32 TRLDVAL;
            u32 TCTRL;
            u32 TLATCH;

            timer_impl() : TRLDVAL(0xffffffff) {
                // Default is RS bit set, not IRQ, disabled
                TCTRL = (0x1 << 1);
            }

            bool IsEnabled() const {
                return TCTRL & 0x1;
            }
            
            void Reset() {
                // Restart
                TCNTVAL = TRLDVAL;
            }

            bool CheckInterrupt() {
                return (TCTRL >> 4) & 0x1;
            }
             
            void ClearInterrupt() {
               TCTRL = TCTRL & ~(0x1 << 4);
            }

            void Tick() {
                if(TCNTVAL == 0) {
                    // Signal interrupt if interrupts enabled:
                    if((TCTRL >> 3) & 0x1) // EI bit set
                        TCTRL |= 0x1 << 4; // IP bit set;
                    // RELOAD if RS bit:
                    if((TCTRL >> 1) & 0x1)
                        TCNTVAL = TRLDVAL;
                } else {
                    --TCNTVAL;
                }
            }
        };

        timer_impl timers[7];
    public:
        u32 VendorId() const { return 0x01; }
        u32 DeviceId() const { return 0x011;}
    
        GPTIMER(u32 IRQ = 8, u32 prescaler = 0xff) : SRELOAD(prescaler) {
            CONFIG = (0x1 << 16) | (IRQ & 0x1f) << 3 | 0x7; // only one timer enabled, 7 implemented
            SRELOAD = prescaler & 0xffff;

            Reset();
        } 

        bool CheckInterrupt(bool clear = true) {
            for(auto& timer : timers)
                if(timer.CheckInterrupt()) {
                    if(clear)
                        timer.ClearInterrupt();
                    return true;
                }
            
            return false;
        }

        void InterruptEnable() {
            for(auto& timer : timers)
                timer.TCTRL = timer.TCTRL | 0x1 << 3; 
        }
 
 
        void Reset() {
            unsigned i = 0;
            for( auto& timer : timers) {
                if ((CONFIG >> (16 + i)) & 0x1)
                     timer.TCTRL = (0x1 << 1) | 0x1; // Only RS bit, no IRQ
    
                ++i;
                timer.Reset();
            }

            SCALER = SRELOAD & 0xffff;
        }

        void Tick() {
            if (SCALER == 0) {
                for (auto & timer : timers) {
                    if(timer.IsEnabled())
                        timer.Tick();
                }   
                SCALER = SRELOAD & 0xffff; 
            } else {
                --SCALER;
            }
        }

        u32 Read(u32 offset) const {
            switch(offset) {
                case(0x0): return SCALER;
                case(0x4): return SRELOAD;
                case(0x8): return CONFIG;
                case(0xc): return CATCHCFG;
                default:
                    if(offset <= 0x7c) {
                        int n = (offset - 0x10) / 0x10;
                        int o = offset % 0x10;
                        switch(o) {
                            case(0x0): return timers[n].TCNTVAL;
                            case(0x4): return timers[n].TRLDVAL;
                            case(0x8): return timers[n].TCTRL;
                            case(0xc): return timers[n].TLATCH;
                            default: return 0;
                        }
                    } else {
                        return 0x0;
                    }
            }
        }

        void Write(u32 offset, u32 regvalue) {
            switch(offset) {
                case(0x0): SCALER = regvalue; return;
                case(0x4): SRELOAD = regvalue; return;
                case(0x8): CONFIG = regvalue; return;
                case(0xc): CATCHCFG = regvalue; return;
                default:
                    if(offset <= 0x7c) {
                        int n = (offset - 0x10) / 0x10;
                        int o = offset % 0x10;
                        switch(o) {
                            case(0x0): timers[n].TCNTVAL = regvalue; return;
                            case(0x4): timers[n].TRLDVAL = regvalue; return;
                            case(0x8): 
                                // If IP bit is set, clear it in the reg, GRIP 36.3.7:
                                if( (regvalue >> 4) & 0x1 )
                                    regvalue = regvalue & ~(0x1 << 4);
                                // IF LD bit is set, reload cnt and clear the bit
                                if( (regvalue >> 2) & 0x1 ) {
                                    regvalue = regvalue & ~(0x1 << 2);
                                    timers[n].Reset();
                                }
                                
                                timers[n].TCTRL = regvalue; 
                                return;
                            case(0xc): timers[n].TLATCH = regvalue; return;
                            default: return;
                        }
                    } else {
                        return;
                    }
            }
        }
};





#endif
