// SPDX-License-Identifier: MIT
#include "IRQMP.h"


void IRQMP::reset() {

    if(num_cpus_ > 8)
        throw std::runtime_error("ncpu > 8 not implemented");

    for(int i = 0; i < 8; ++i) {
        PIMASK[i] = 0;
        PIFORCE[i] = 0;
        irq_hint_[i].store(0, std::memory_order_relaxed);
    }

    num_active_cpus_ = 0;

    if(num_cpus_ > 1) {
        // Set MPSTAT
        // num cpus -1 and enable broadcast
        MPSTAT = (num_cpus_ - 1) << LEON3_IRQMPSTATUS_CPUNR | 0x1 << LEON3_IRQMPSTATUS_BROADCAST;
        
        // Set cpu 0 to running, all others in powerdown
        MPSTAT |= 0xfffe; // Bit 0 = 0, bit 1-15 = 1

    }

    // Resize the cpu pointer vector. The pointers are set externally
    // with the method set_cpu_ptr()
    cpu_ptrs_.resize(num_cpus_);
    for(u8 i = 0; i < num_cpus_; ++i)
        cpu_ptrs_[i] = nullptr;
}

// Recompute irq_hint_[c] for all CPUs from current IPEND/PIFORCE/PIMASK state.
// Must be called while holding a unique_lock on mtx.
void IRQMP::update_hints_locked() {
    for (u8 c = 0; c < num_cpus_; ++c) {
        u32 h = 0;
        for (int i = 15; i >= 1; --i) {
            if (((PIFORCE[c] >> i) & 1u) && ((PIMASK[c] >> i) & 1u)) { h = i; break; }
        }
        if (!h) {
            for (int i = 15; i >= 1; --i) {
                if (((IPEND >> i) & 1u) && ((PIMASK[c] >> i) & 1u)) { h = i; break; }
            }
        }
        irq_hint_[c].store(h, std::memory_order_release);
    }
}

void IRQMP::trigger_irq(u32 IRL) {
    u8 irl = IRL & 0xFU;

    {
#ifdef IRQMP_DEBUG
        if(irl == 13)
            std::cout << "[IRQMP] Trigger irl 13 from trigger_irq\n";
#endif
        std::unique_lock lock(mtx);
        
        // First, check if the irl is marked as broadcast
        if((BRDCST >> irl) & 0x1U) {
            // Set irl to force register of all cpus
            if(num_cpus_ > 1) {
                for(u8 i = 0; i < num_cpus_; ++i)
                    if((PIMASK[i] >> irl) & 0x1U)
                        PIFORCE[i] |= 0x1U << irl;
            } else {
                if((PIMASK[0] >> irl) & 0x1U) {
                    PIFORCE[0] |= 0x1U << irl;
                    IFORCE |= 0x1U << irl;
                }
            }
        } else { // If not broadcast, the set to normal IPEND
            IPEND = IPEND | (0x1U << irl);
        }
        update_hints_locked();
    } // <-- Release mtx here

    // Wake up any CPUS that have unmasked this IRL
    for (unsigned int i = 0; i < num_cpus_; ++i)
    {
        if((0x1 << irl) & PIMASK[i]) {
            cpu_ptrs_[i]->wakeup();
        }

    }
    
}


void IRQMP::clear_irq(uint32_t IRL, uint8_t cpu_id) {
#ifdef IRQMP_DEBUG
    if (IRL == 13) {
        std::cout
            << "[IRQMP] clear_irq: cpu=" << int(cpu_id)
            << " IRL=13"
            << " PIFORCE_before=0x" << std::hex << PIFORCE[cpu_id]
            << " IPEND_before=0x" << IPEND
            << std::dec
            << "\n";
    }
#endif

    std::unique_lock lock(mtx);
    uint8_t irl = IRL & 0xf;
    uint8_t cpu = cpu_id & 0x7;
    uint32_t bit = 1u << irl;

    if (PIFORCE[cpu] & bit) {
        PIFORCE[cpu] &= ~bit;
    } else {
        IPEND &= ~bit;
    }
    update_hints_locked();

#ifdef IRQMP_DEBUG
    if (IRL == 13) {
        std::cout
            << "[IRQMP] clear_irq: cpu=" << int(cpu_id)
            << " PIFORCE_after=0x" << std::hex << PIFORCE[cpu_id]
            << " IPEND_after=0x" << IPEND
            << std::dec
            << "\n";
    }
#endif
}

u32 IRQMP::read(u32 offset) const {
    std::shared_lock lock(mtx);
    //std::cout << "Read IRQ at offset " << std::hex << offset << std::dec << "\n";
    if(offset >= 0x40 && offset < 0x60) {
        u32 n = (offset - 0x40)/4;
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
    throw std::runtime_error("IRQMP::read offset not implemented: " + to_hex(offset));
    return 0; 
}
        

void IRQMP::write(u32 offset, u32 value) {
    
    
    std::unique_lock lock(mtx);
    //std::cout << "write IRQ at offset " << std::hex << offset << ", value= " << value << std::dec << "\n";
    
    if(offset >= 0x40 && offset < 0x60) {
        u32 n = (offset - 0x40)/4;
        //std::cout << "Write IRQ 0x40 + n*4, PIMASK[" << n << "] = " << std::hex << value << std::dec << "\n";

        // Check if this is an unmasking of global barrier IRQ (8) and the cpu is
        // powered up:
        if ((((PIMASK[n] >> barrier_irl) & 0x1) != 0x1U) && (((value >> barrier_irl) & 0x1U) == 0x1U) && (((MPSTAT >> n) & 0x1U) == 0x0U) )
            ++num_active_cpus_;
        
        PIMASK[n] = value;
        update_hints_locked();
        return;
    } else if(offset >= 0x80 && offset < 0xA0) {
        u32 n = (offset - 0x80)/4;

        PIFORCE[n] |= value;
        update_hints_locked();
#ifdef IRQMP_DEBUG
        if (value & (1u << 13)) {
            std::cout
                << "[IRQMP] PIFORCE write: cpu=" << n
                << " value=0x" << std::hex << value
                << " old=0x" << (PIFORCE[n] & ~value)
                << " mask=0x" << PIMASK[n]
                << " new=0x" << PIFORCE[n]
                << std::dec
                << "\n";
        }
#endif
        // Wake up the CPU if it has unmasked this IRL
        if((value) & PIMASK[n]) {
            //std::cout << "[IRQMP] Waking up CPU" << (int)n << "\n";
            lock.unlock();
            cpu_ptrs_[n]->wakeup();
        }

        return;
    }    
    
    switch(offset) {
        case(IRQMP_ILEVEL_OS):
            ILEVEL = value;
#ifdef IRQMP_DEBUG
            std::cout << "[IRQMP] Write ILEVEL=0x" << std::hex << ILEVEL << std::dec << "\n";
#endif
            return;
        case(IRQMP_IPEND_OS):
#ifdef IRQMP_DEBUG
            if((value >> 13) & 0x1U)
                std::cout << "[IRQMP] IRQ 13 -> IPEND\n";
#endif
            IPEND = value;
            update_hints_locked();
            return;
        case(IRQMP_IFORCE_OS):
#ifdef IRQMP_DEBUG
            if((value >> 13) & 0x1U)
                std::cout << "[IRQMP] IRQ 13 -> IFORCE 0x8\n";
#endif
            IFORCE |= value;
            update_hints_locked();
            return;
        case(IRQMP_ICLEAR_OS): {
#ifdef IRQMP_DEBUG
            if (value) {
                std::cout << "[IRQMP] ICLEAR write=0x" << std::hex << value
                        << " (IPEND before=0x" << IPEND << ")"
                        << std::dec << "\n";
            }
#endif
            ICLEAR = value;
            const u32 mask = value & 0x0000FFFEu;
            IPEND &= ~mask;
            update_hints_locked();
            return;
        }
        case(IRQMP_MPSTAT_OS): {
            u32 val = value & 0xffff; // Only bits 0-15 are writable
            for (u8 i = 0; i < num_cpus_; ++i) {
                
                // if write 1 to bit i, and MPSTAT[i] is 1 (powerdown), then power up
                if ( ((val >> i) & 0x1) & ((MPSTAT >> i) & 0x1) )
                {
                    // power up cpu i
                    if(cpu_ptrs_[i] != nullptr) {
                        
                        // we only increase active cpus if the barrier irq (8)
                        // is unmasked for this cpu
                        if( (PIMASK[i] >> barrier_irl) & 0x1U )
                            ++num_active_cpus_;

                        MPSTAT &= ~(1 << i); // Only write 0 to mpstat if we actually woke up
                        
                        //std::cout << "[IRQMP] Waking up cpu " << int(i) << "\n";
                        cpu_ptrs_[i]->wakeup();
                        
                    }
                }
            }
            
            return;
        }
        case(IRQMP_BRDCST_OS): {
            // Only allow writing to bits 1-15. 0 and 16-31 are RO
            u32 mask = 0xfffe;
            BRDCST = value & mask;
            return;
        }
        case(IRQMP_ERRSTAT_OS):
            ERRSTAT = value;
            return;
        default:
            throw std::runtime_error("IRQMP::write offset not implemented: " + to_hex(offset));
            return ;
    }
    return; 
}




