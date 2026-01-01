#include "IRQMP.h"


void IRQMP::reset() {

    if(num_cpus_ > 8)
        throw std::runtime_error("ncpu > 8 not implemented");

    for(int i = 0; i < 8; ++i) {
        PIMASK[i] = 0;
        PIFORCE[i] = 0;
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

void IRQMP::trigger_irq(u32 IRL) {
    u8 irl = IRL & 0xFU;

    {
        std::unique_lock lock(mtx);
        
        // First, check if the irl is marked as broadcast
        if((BRDCST >> irl) & 0x1U) {
            // Set irl to force register of all cpus
            if(num_cpus_ > 1) {
                for(u8 i = 0; i < num_cpus_; ++i)
                    PIFORCE[i] |= 0x1U << irl;
            } else {
                PIFORCE[0] |= 0x1U << irl;
                IFORCE |= 0x1U << irl;
            }
        } else { // If not broadcast, the set to normal IPEND
            IPEND = IPEND | (0x1U << irl);
        }
    } // <-- Release mtx here

    // Wake up any CPUS that have unmasked this IRL
    for (unsigned int i = 0; i < num_cpus_; ++i)
    {
        if((0x1 << irl) & PIMASK[i]) {
            cpu_ptrs_[i]->wakeup();
        }

    }
    
}

unsigned IRQMP::get_next_pending_irq(u8 cpu_id) const {
    u8 cpu = cpu_id & 0x7U;
    std::shared_lock lock(mtx);

    // Check forced first
    for(unsigned int i = 15; i >= 1; --i) {
        if( ((PIFORCE[cpu] >> i) & 0x1U) && ((PIMASK[cpu] >> i) & 0x1U  ) ) {
                return i;
        }
    }
    
    // ..Then pending irls
    for(unsigned int i = 15; i >= 1; --i) {
        if( ((IPEND >> i) & 0x1U) && ((PIMASK[cpu] >> i) & 0x1U  ) ) {
            return i;
        }
    }
    
    return 0;
}

void IRQMP::clear_irq(u32 IRL, u8 cpu_id) {
    std::unique_lock lock(mtx);
    u8 irl = IRL & 0xf;
    u8 cpu = cpu_id & 0x7U;

    /*
    if((PIFORCE[cpu] >> irl) & 0x1U)
        PIFORCE[cpu] &= ~(0x1U << irl);
    else
        IPEND = IPEND & ~(0x1U << irl);
    */
   // Just clear both...
   u32 mask = ~(0x1U << irl);
   PIFORCE[cpu] &= mask;
   IPEND &= mask;
}

void IRQMP::write(u32 offset, u32 value) {
    std::unique_lock lock(mtx);
    std::cout << "write IRQ at offset " << std::hex << offset << ", value= " << value << std::dec << "\n";
    
    if(offset >= 0x40 && offset < 0x60) {
        u32 n = (offset - 0x40)/4;
        std::cout << "Write IRQ 0x40 + n*4, PIMASK[" << n << "] = " << std::hex << value << std::dec << "\n";

        // Check if this is an unmasking of global barrier IRQ (8) and the cpu is
        // powered up:
        if ((((PIMASK[n] >> barrier_irl) & 0x1) != 0x1U) && (((value >> barrier_irl) & 0x1U) == 0x1U) && (((MPSTAT >> n) & 0x1U) == 0x0U) )
            ++num_active_cpus_;
        
        PIMASK[n] = value;
        return;
    }     
    
    switch(offset) {
        case(IRQMP_ILEVEL_OS):
            ILEVEL = value;
            break;
        case(IRQMP_IPEND_OS):
            IPEND = value;
            break;
        case(IRQMP_IFORCE_OS):
            IFORCE = value;
            break;
        case(IRQMP_ICLEAR_OS):
            ICLEAR = value;
            break;
        case(IRQMP_MPSTAT_OS): {
            u32 val = value & 0xffff; // Only bits 0-15 are writable
            for (u8 i = 0; i < num_cpus_; ++i) {
                
                // if write 1 to bit i, and MPSTAT[i] is 1 (powerdown), then power up
                if ( ((val >> i) & 0x1) & ((MPSTAT >> i) & 0x1) )
                {
                    // power up cpu i
                    if(cpu_ptrs_[i] != nullptr) {
                        std::cout << "[IRQMP] Waking up cpu " << int(i) << "\n";
                        cpu_ptrs_[i]->wakeup();
                        
                        // we only increase active cpus if the barrier irq (8)
                        // is unmasked for this cpu
                        if( (PIMASK[i] >> barrier_irl) & 0x1U )
                            ++num_active_cpus_;

                        MPSTAT &= ~(1 << i); // Only write 0 to mpstat if we actually woke up
                    }
                }
            }
            
            break;
        }
        case(IRQMP_BRDCST_OS): {
            // Only allow writing to bits 1-15. 0 and 16-31 are RO
            u32 mask = 0xfffe;
            BRDCST = value & mask;
            break;
        }
        case(IRQMP_ERRSTAT_OS):
            ERRSTAT = value;
            break;
        default:
            throw std::runtime_error("IRQMP::write offset not implemented: " + to_hex(offset));
            return ;
    }
    return; 
}




