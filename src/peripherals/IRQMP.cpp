#include "IRQMP.h"

void IRQMP::TriggerIRQ(u32 IRL) {
    IRL = IRL & 0xf;

    std::vector<int> target_cpus;
    {
        std::unique_lock lock(mtx);
        
        for (unsigned int i = 0; i < num_cpus_; ++i)
        {
            if((0x1 << IRL) & PIMASK[i]) {
                IPEND = IPEND | (0x1 << IRL);
        
                // Record CPUs that should be awakened
                target_cpus.push_back(i);
            }

            
        }
    } // <-- release IRQMP mutex here

    std::unique_lock lock(mtx);
    if(target_cpus.size() > 1)
                throw std::runtime_error("num cpus > 1 not implemented");
    
    for (int cpu_id : target_cpus)
    {
        if(cpu_ptr_) {
        //CPU* cpu = cpus[cpu_id];   // however your CPUs are stored

            cpu_ptr_->wakeup();
        }
    }
}

unsigned IRQMP::GetNextIRQPending(u8 cpu_id) const {
    std::shared_lock lock(mtx);
    for(unsigned int i = 15; i >= 1; --i)
        if(IPEND & (0x1 << i)) 
            return i;
    return 0;
}

void IRQMP::ClearIRQ(u32 IRL) {
    std::unique_lock lock(mtx);
    IRL = IRL & 0xf;
    IPEND = IPEND & ~(0x1 << IRL);
}

void IRQMP::Write(u32 offset, u32 value) {
    std::unique_lock lock(mtx);
    std::cout << "write IRQ at offset " << std::hex << offset << ", value= " << value << "\n";
    
    if(offset >= 0x40 && offset < 0x60) {
        u32 n = offset - 0x40;
        //std::cout << "Write IRQ 0x40 + n*4, PIMASK[" << n << "] = " << std::hex << value << std::dec << "\n";
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
        case(IRQMP_MPSTAT_OS):
            MPSTAT = value;
            break;
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
            throw not_implemented_exception();  
            return ;
    }
    return; 
}




