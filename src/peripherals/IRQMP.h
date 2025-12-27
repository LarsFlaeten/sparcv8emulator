#ifndef _IRQMP_H_
#define _IRQMP_H_

#include "../common.h"

#include "../sparcv8/CPU.h"

// std
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <iostream>
#include <memory>

#define IRQMP_ILEVEL_OS 0x00
#define IRQMP_IPEND_OS 0x04
#define IRQMP_IFORCE_OS 0x08
#define IRQMP_ICLEAR_OS 0x0C
#define IRQMP_MPSTAT_OS 0x10
#define IRQMP_BRDCST_OS 0x14
#define IRQMP_ERRSTAT_OS 0x18
#define IRQMP_AMPCTRL_OS 0x20

#define LEON3_IRQMPSTATUS_CPUNR     28
#define LEON3_IRQMPSTATUS_BROADCAST 27
#define LEON3_IRQMPAMPCTRL_NCTRL 28

class IRQMP {
    private:
        // Regs mapped into APB address space
        u32 ILEVEL;
        u32 IPEND;
        u32 IFORCE;
        u32 ICLEAR;
        u32 MPSTAT;
        u32 BRDCST;
        u32 ERRSTAT;
        u32 AMPCTRL;
        u32 PIMASK[32]; // Processor n interrupt mask registers

        u32 num_cpus_;

        mutable std::shared_mutex mtx;

        CPU* cpu_ptr_ = nullptr;

    public:
        IRQMP(u8 num_cpus):
            ILEVEL(0),
            IPEND(0),
            IFORCE(0),
            ICLEAR(0),
            MPSTAT(0),
            BRDCST(0),
            ERRSTAT(0),
            AMPCTRL(0),
            num_cpus_(num_cpus)
        {
            for(int i = 0; i < 32; ++i)
                PIMASK[i] = 0;

            if(num_cpus > 1) {
                // Set MPSTAT
                // num cpus -1 and enable broadcast
                MPSTAT = (num_cpus - 1) << LEON3_IRQMPSTATUS_CPUNR | 0x1 << LEON3_IRQMPSTATUS_BROADCAST;
                

            }
        }

        void set_cpu_ptr(CPU* cpu) {cpu_ptr_ = cpu;}

        void trigger_irq(u32 IRL);

        unsigned int get_next_pending_irq(u8 cpu_id) const;

        void ClearIRQ(u32 IRL);

        u32 Read(u32 offset) const {
            std::shared_lock lock(mtx);
            std::cout << "Read IRQ at offset " << std::hex << offset << "\n";
            if(offset >= 0x40 && offset < 0x60) {
                u32 n = offset - 0x40;
                //std::cout << "Read IRQ 0x40 + n*4, PIMASK[" << n << "] = " << std::hex << PIMASK[n] << std::dec << "\n";
                return PIMASK[n];
            }     
            switch(offset) {
                case(IRQMP_ILEVEL_OS):
                    return ILEVEL;
                    break;
                case(IRQMP_IPEND_OS):
                    return IPEND;
                    break;
                case(IRQMP_IFORCE_OS):
                    return IFORCE;
                    break;
                case(IRQMP_ICLEAR_OS):
                    return ICLEAR;
                    break;
                case(IRQMP_MPSTAT_OS):
                    return MPSTAT;
                    break;
                case(IRQMP_BRDCST_OS):
                    return BRDCST;
                    break;
                case(IRQMP_ERRSTAT_OS):
                    return ERRSTAT;
                    break;
                case(IRQMP_AMPCTRL_OS):
                    return AMPCTRL;
                    break;
                default:
                    return 0;
            }
            return 0; 
        }
        void Write(u32 offset, u32 value);


};





#endif

