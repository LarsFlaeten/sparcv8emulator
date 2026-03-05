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
        u32 PIFORCE[8]; // Processor n interrupt force register (when ncpu > 1)
        u32 PIMASK[8]; // Processor n interrupt mask registers

        u32 num_cpus_;
        u32 num_active_cpus_;
        u8  barrier_irl;

        mutable std::shared_mutex mtx;

        std::vector<CPU*> cpu_ptrs_;

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
            num_cpus_(num_cpus),
            num_active_cpus_(0),
            barrier_irl(8)
        {
            reset();
        }

        void reset();

        void set_cpu_ptr(CPU* cpu, u8 cpu_id) {
            if(cpu_id >= cpu_ptrs_.size())
                throw std::runtime_error("Cannot assign cpu larger than requested number of CPUs");
            cpu_ptrs_[cpu_id] = cpu;
        }

        u32 get_number_active_cpus() const {
            std::shared_lock lock(mtx);
            return num_active_cpus_;
        }

        void trigger_irq(u32 IRL);
        
        unsigned int get_next_pending_irq(u8 cpu_id) const;

        void clear_irq(u32 IRL, u8 cpu_id);

        u32 read(u32 offset) const;
        void write(u32 offset, u32 value);


        static std::string to_hex(u32 val) {
            char buf[11];
            snprintf(buf, sizeof(buf), "%08X", val);
            return buf;
        }

        void dump_state() const {
            std::shared_lock lock(mtx);
            std::cout << "[IRQMP] ILEVEL=0x" << std::hex << ILEVEL
                    << " IPEND=0x" << IPEND
                    << " BRDCST=0x" << BRDCST
                    << std::dec << "\n";
            for (u8 c = 0; c < num_cpus_; ++c) {
                std::cout << "  cpu" << int(c)
                        << " MASK=0x" << std::hex << PIMASK[c]
                        << " PIFORCE=0x" << PIFORCE[c]
                        << std::dec << "\n";
            }
        }

};





#endif

