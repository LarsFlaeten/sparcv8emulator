#ifndef _GPTIMER_H_
#define _GPTIMER_H_

#include "apb_slave.h"

// std
#include <mutex>


#define NUM_IMPLEMENTED_TIMERS 0x2

class GPTIMER : public apb_slave {
    private:
        u32 SCALER;
        u32 SRELOAD;
        u32 CONFIG;
        u32 CATCHCFG; 

        // Use a simple mutex here, and not a shared reader/writer pattern
        // The timer is mostly ticked from the bus clock, and very occationally
        // read from CPUs
        mutable std::mutex mtx_;

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

            bool is_enabled() const {
                return TCTRL & 0x1;
            }
            
            void reset() {
                // Restart
                TCNTVAL = TRLDVAL;
            }

            bool check_interrupt() {
                return (TCTRL >> 4) & 0x1;
            }
             
            void clear_interrupt() {
               TCTRL = TCTRL & ~(0x1 << 4);
            }

            void tick() {
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

        double freq = 10'000'000.0;
    public:
        u32 vendor_id() const { return 0x01; }
        u32 device_id() const { return 0x011;}
    
        GPTIMER(u32 IRQ = 8, u32 prescaler = 0x31) : SRELOAD(prescaler), CATCHCFG(0) {
            //CONFIG = (0x1 << 16) | (IRQ & 0x1f) << 3 | 0x7; // only one timer enabled, 7 implemented
            CONFIG = (0x1 << 8) | (IRQ & 0x1f) << 3 | NUM_IMPLEMENTED_TIMERS; // none enabled, separate IRQs, 2 implemented
            
            SRELOAD = prescaler & 0xffff;

            freq = 10'000'000.0;

            reset();
        }

        void set_system_freq(double f) {freq = f;}

        void set_LEON_state() {
            std::lock_guard lock(mtx_);
            // Set LEON specific startup state:
            // Timer 1 enabled, timer 2 disabled and all zeroes
            timers[0].TCTRL = 0x3; // RS bit and enable bit
            timers[0].TCNTVAL = 0x270f;
            timers[0].TRLDVAL = 0x270f;
            timers[1].TCTRL = 0;
            timers[1].TCNTVAL = 0;
            timers[1].TRLDVAL = 0;
    
            //SCALER = 0x24;            
            SRELOAD = 0x24;
            SCALER = SRELOAD;
        } 

        void set_LEON_smp_state() {
            std::lock_guard lock(mtx_);
            // Set LEON specific startup state:
            // Timer 1 enabled, timer 2 disabled and all zeroes
            timers[0].TCTRL = 0x3 | 0x8; // RS bit and enable bit + IRQ
            timers[0].TCNTVAL = 0x270f;
            timers[0].TRLDVAL = 0x270f;
            timers[1].TCTRL = 0;
            timers[1].TCNTVAL = 0;
            timers[1].TRLDVAL = 0;
    
            //SCALER = 0x24;            
            SRELOAD = 0x24;
            SCALER = SRELOAD;
        } 

        bool check_interrupt(bool clear = true) {
            std::lock_guard lock(mtx_);
            
            for(auto& timer : timers)
                if(timer.check_interrupt()) {
                    if(clear)
                        timer.clear_interrupt();
                    return true;
                }
            
            return false;
        }

        void interrupt_enable() {
            std::lock_guard lock(mtx_);
            
            for(auto& timer : timers)
                timer.TCTRL = timer.TCTRL | 0x1 << 3; 
        }
 
 
        void reset() {
            std::lock_guard lock(mtx_);
            
            unsigned i = 0;
            for( auto& timer : timers) {
                if ((CONFIG >> (16 + i)) & 0x1)
                     timer.TCTRL = (0x1 << 1) | 0x1; // Only RS bit, no IRQ
    
                ++i;
                timer.reset();
            }

            SCALER = SRELOAD & 0xffff;
        }

        void Tick() {
            std::lock_guard lock(mtx_);
            
            // only propagate tick when at leats one timer is enabled:
            // CONFIG:TIMERN field (bits 16 -22)
            //if(((CONFIG >> 16) & 0x7f) > 0 )
            // FIX Do not use CONFIG field, this is write only
            if(timers[0].is_enabled() || timers[1].is_enabled())
            {
            
                if (SCALER == 0) {
                    for (auto & timer : timers) {
                        if(timer.is_enabled())
                            timer.tick();
                    }   
                    SCALER = SRELOAD & 0xffff; 
                } else {
                    --SCALER;
                }
            }

            
        }

        // This method is a combination of tick() and check_interrupt(),
        // only its is done under one mutex lock
        bool tick_and_check_interrupt(bool clear_interrupt) {
            std::lock_guard lock(mtx_);
            
            // only propagate tick when at leats one timer is enabled:
            // CONFIG:TIMERN field (bits 16 -22)
            //if(((CONFIG >> 16) & 0x7f) > 0 )
            // FIX Do not use CONFIG field, this is write only
            if(timers[0].is_enabled() || timers[1].is_enabled())
            {
            
                if (SCALER == 0) {
                    for (auto & timer : timers) {
                        if(timer.is_enabled())
                            timer.tick();
                    }   
                    SCALER = SRELOAD & 0xffff; 
                } else {
                    --SCALER;
                }
            }

            for(auto& timer : timers)
                if(timer.check_interrupt()) {
                    if(clear_interrupt)
                        timer.clear_interrupt();
                    return true;
                }
            
            return false;


        }
        // This method is a combination of tick() and check_interrupt(),
        // lock must be obtained vie separate methods
        bool tick_and_check_interrupt_unlocked(bool clear_interrupt) {
            
            // only propagate tick when at leats one timer is enabled:
            // CONFIG:TIMERN field (bits 16 -22)
            //if(((CONFIG >> 16) & 0x7f) > 0 )
            // FIX Do not use CONFIG field, this is write only
            if(timers[0].is_enabled() || timers[1].is_enabled())
            {
            
                if (SCALER == 0) {
                    for (auto & timer : timers) {
                        if(timer.is_enabled())
                            timer.tick();
                    }   
                    SCALER = SRELOAD & 0xffff; 
                } else {
                    --SCALER;
                }
            }

            for(auto& timer : timers)
                if(timer.check_interrupt()) {
                    if(clear_interrupt)
                        timer.clear_interrupt();
                    return true;
                }
            
            return false;


        }

        // Advance the timer by n bus ticks without looping n times.
        // Computes the number of prescaler ticks analytically, then applies
        // them to each timer counter. Returns true if any timer IRQ fired.
        // Caller must hold the timer lock.
        bool advance_unlocked(uint64_t n, bool clear_irq) {
            if (n == 0) return false;
            if (!(timers[0].is_enabled() || timers[1].is_enabled()))
                return false;

            const uint32_t sreload = SRELOAD & 0xffff;
            const uint32_t period  = sreload + 1; // bus ticks per prescaler tick
            uint64_t prescaler_ticks = 0;

            // Phase 1: consume the remainder of the current prescaler period.
            if (SCALER == 0) {
                // Prescaler fires on this tick.
                prescaler_ticks++;
                SCALER = sreload;
                n--;
            } else if (n <= SCALER) {
                // All n ticks consumed before the next prescaler tick.
                SCALER -= (uint32_t)n;
                n = 0;
            } else {
                // Advance to end of current prescaler period.
                n -= SCALER + 1;
                prescaler_ticks++;
                SCALER = sreload;
            }

            // Phase 2: handle any remaining full prescaler periods.
            if (n > 0) {
                prescaler_ticks += n / period;
                uint32_t leftover = (uint32_t)(n % period);
                // leftover==0 means last tick was a prescaler tick, SCALER reloads.
                SCALER = sreload - leftover;
            }

            // Apply prescaler_ticks to each enabled timer counter.
            bool any_irq = false;
            for (auto& timer : timers) {
                if (!timer.is_enabled()) continue;
                for (uint64_t p = 0; p < prescaler_ticks; ++p)
                    timer.tick();
                if (timer.check_interrupt()) {
                    any_irq = true;
                    if (clear_irq) timer.clear_interrupt();
                }
            }
            return any_irq;
        }

        // Explicit locking and unlocking
        void lock() {mtx_.lock();}
        void unlock() {mtx_.unlock();}

        u32 read(u32 offset) const {
            std::lock_guard lock(mtx_);
            
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

        void write(u32 offset, u32 regvalue) {
            std::lock_guard lock(mtx_);
            
            //std::cout << "write GPTIMER at offset " << std::hex << offset << ", value= " << regvalue << "\n";
            
            switch(offset) {
                case(0x0): SCALER = regvalue; return;
                case(0x4): 
                    SRELOAD = regvalue; 
                    for(int i = 0; i < NUM_IMPLEMENTED_TIMERS; ++i) {
                        std::cout << "Timer " << i << " now ticks at " << SRELOAD << "x" << timers[i].TRLDVAL << " = " << SRELOAD * timers[i].TRLDVAL << " ticks, ";
                        std::cout << std::to_string(freq / (SRELOAD * timers[i].TRLDVAL)) << " Hz\n";
                    }
                    return;
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
                            case(0x4): 
                                timers[n].TRLDVAL = regvalue;
                                std::cout << "Timer " << n << " now ticks at " << SRELOAD << "x" << timers[n].TRLDVAL << " = " << SRELOAD * timers[n].TRLDVAL << " ticks, ";
                                std::cout << std::to_string(freq / (SRELOAD * timers[n].TRLDVAL)) << " Hz\n";
                                
                                return;
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
                                    timers[n].reset();
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
