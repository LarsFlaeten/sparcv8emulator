#include "IRQMP.h"

void IRQMP::TriggerIRQ(u32 IRL) {
    IRL = IRL & 0xf;

    std::vector<int> target_cpus;
    {
        std::unique_lock lock(mtx);
        
        for (unsigned int i = 0; i < num_cpus; ++i)
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

unsigned IRQMP::GetNextIRQPending() const {
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



