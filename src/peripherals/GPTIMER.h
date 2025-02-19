#ifndef _GPTIMER_H_
#define _GPTIMER_H_

#define NUM_IMPLEMENTED_TIMERS 0x2

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

            timer_impl() : TCNTVAL(0x270f), TRLDVAL(0x270f), TCTRL(0), TLATCH(0) {
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
                    else {
                        // No relaod, stop at -1 and disable
                        --TCNTVAL;
                        TCTRL = TCTRL & ~0x1;
                    }
                } else {
                    --TCNTVAL;
                }
            }
        };

        timer_impl timers[NUM_IMPLEMENTED_TIMERS];
    public:
        u32 VendorId() const { return 0x01; }
        u32 DeviceId() const { return 0x011;}
    
        GPTIMER(u32 IRQ = 8, u32 prescaler = 0x31) : SRELOAD(prescaler), CATCHCFG(0) {
            //CONFIG = (0x1 << 16) | (IRQ & 0x1f) << 3 | 0x7; // only one timer enabled, 7 implemented
            CONFIG = (0x1 << 8) | (IRQ & 0x1f) << 3 | NUM_IMPLEMENTED_TIMERS; // none enabled, separate IRQs, 2 implemented
            
            SRELOAD = prescaler & 0xffff;

            Reset();
        }

        void SetLEONState() {
            // Set LEON specific startup state:
            // Timer 1 enabled, timer 2 disabled and all zeroes
            timers[0].TCTRL = 0x3; // RS bit and enable bit
            timers[0].TCNTVAL = 0xffffffff;
            timers[0].TRLDVAL = 0xffffffff;
            timers[1].TCTRL = 0;
            timers[1].TCNTVAL = 0;
            timers[1].TRLDVAL = 0;
    
            SCALER = 0x24;            

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
            // only propagate tick when at leats one timer is enabled:
            // CONFIG:TIMERN field (bits 16 -22)
            //if(((CONFIG >> 16) & 0x7f) > 0 )
            // FIX Do not use CONFIG field, this is write only
            if(timers[0].IsEnabled() || timers[1].IsEnabled())
            {
            
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
        }

        u32 Read(u32 offset) const {
            u32 ret;
            switch(offset) {
                case(0x0): ret = SCALER; break;
                case(0x4): ret = SRELOAD; break;
                case(0x8): ret = CONFIG & ~(0x7f << 16); break;
                case(0xc): ret = CATCHCFG; break;
                default:
                    if(offset <= 0x7c) {
                        int n = (offset - 0x10) / 0x10;
                        
                        if(n > NUM_IMPLEMENTED_TIMERS - 1) {
                            ret = 0;
                            break;
                        }
                        
                        int o = offset % 0x10;
                        switch(o) {
                            case(0x0): ret = timers[n].TCNTVAL; break;
                            case(0x4): ret = timers[n].TRLDVAL; break;
                            case(0x8): ret = timers[n].TCTRL; break;
                            case(0xc): ret = timers[n].TLATCH; break;
                            default: ret = 0; break;
                        }
                    } else {
                        ret = 0x0;
                    }
                    break;
            }
            
            //std::cout << "read GPTIMER at offset " << std::hex << offset << ", value= " << ret << "\n";
            return ret;
            
        }

        void Write(u32 offset, u32 regvalue) {
            //std::cout << "write GPTIMER at offset " << std::hex << offset << ", value= " << regvalue << "\n";
            
            switch(offset) {
                case(0x0): SCALER = regvalue; return;
                case(0x4): SRELOAD = regvalue; return;
                case(0x8): CONFIG = regvalue; return;
                case(0xc): CATCHCFG = regvalue; return;
                default:
                    if(offset <= 0x7c) {
                        int n = (offset - 0x10) / 0x10;
                        if(n > NUM_IMPLEMENTED_TIMERS - 1) {
                            return; // Writes beyond implemented timers have no effect
                        }
                        int o = offset % 0x10;
                        switch(o) {
                            case(0x0): timers[n].TCNTVAL = regvalue; return;
                            case(0x4): timers[n].TRLDVAL = regvalue; return;
                            case(0x8): 
                                // adjustment of timers[n].TCTRL
                                // If EN bit is set, also set correspongin bit in CONTROL:
                                if( (regvalue) & 0x1 )
                                    CONFIG = CONFIG | (0x1 << (16 + n));
                               if( ((regvalue >> 4) & 0x1) == 0x1){
                                    // If IP bit is set, clear it in the reg, GRIP 36.3.7:
                                    regvalue = regvalue & ~(0x1 << 4);
                                } else {
                                    // Writes of '0' has no effect, GRIP 36.3.7:
                                    // I.e. keep bit in TCTRL
                                    regvalue = regvalue | (timers[n].TCTRL & (0x1 << 4));
                                }
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
