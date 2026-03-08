// SPDX-License-Identifier: MIT
#pragma once

#include "../common.h"

#include "../sparcv8/CPU.h"

// std
#include <atomic>
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
        u32 ilevel_;
        u32 ipend_;
        u32 iforce_;
        u32 iclear_;
        u32 mpstat_;
        u32 brdcst_;
        u32 errstat_;
        u32 ampctrl_;
        u32 piforce_[8]; // Processor n interrupt force register (when ncpu > 1)
        u32 pimask_[8]; // Processor n interrupt mask registers

        u32 num_cpus_;
        u32 num_active_cpus_;
        u8  barrier_irl_;

        // Lock-free IRQ hint per CPU: highest pending+unmasked IRL, or 0.
        // Updated under unique_lock; read from CPU hot-path without any lock.
        std::atomic<u32> irq_hint_[8]{};

        mutable std::shared_mutex mtx_;

        void update_hints_locked(); // must hold unique_lock on mtx_

        std::vector<CPU*> cpu_ptrs_;

    public:
        IRQMP(u8 num_cpus):
            ilevel_(0),
            ipend_(0),
            iforce_(0),
            iclear_(0),
            mpstat_(0),
            brdcst_(0),
            errstat_(0),
            ampctrl_(0),
            num_cpus_(num_cpus),
            num_active_cpus_(0),
            barrier_irl_(8)
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
            std::shared_lock lock(mtx_);
            return num_active_cpus_;
        }

        void trigger_irq(u32 IRL);

        // Lock-free hot-path: returns highest pending+unmasked IRL for this CPU (0 = none).
        u32 get_irq_hint(u8 cpu_id) const {
            return irq_hint_[cpu_id & 7].load(std::memory_order_acquire);
        }

        void clear_irq(u32 IRL, u8 cpu_id);

        u32 read(u32 offset) const;
        void write(u32 offset, u32 value);


        static std::string to_hex(u32 val) {
            char buf[11];
            snprintf(buf, sizeof(buf), "%08X", val);
            return buf;
        }

        void dump_state() const {
            std::shared_lock lock(mtx_);
            std::cout << "[IRQMP] ilevel_=0x" << std::hex << ilevel_
                    << " ipend_=0x" << ipend_
                    << " brdcst_=0x" << brdcst_
                    << std::dec << "\n";
            for (u8 c = 0; c < num_cpus_; ++c) {
                std::cout << "  cpu" << int(c)
                        << " MASK=0x" << std::hex << pimask_[c]
                        << " piforce_=0x" << piforce_[c]
                        << std::dec << "\n";
            }
        }

};






