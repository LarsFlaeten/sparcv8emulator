#ifndef _IRQMP_H_
#define _IRQMP_H_

#define IRQMP_ILEVEL_OS 0x00
#define IRQMP_IPEND_OS 0x04
#define IRQMP_IFORCE_OS 0x08
#define IRQMP_ICLEAR_OS 0x0C
#define IRQMP_MPSTAT_OS 0x10
#define IRQMP_BRDCST_OS 0x14
#define IRQMP_ERRSTAT_OS 0x18
#define IRQMP_AMPCTRL_OS 0x20



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


    public:
        IRQMP() {}

        void TriggerIRQ(u32 IRL) {
            IRL = IRL & 0xf;
            IPEND = IPEND | (0x1 << IRL);
        }

        unsigned int GetNextIRQPending() const {
            for(unsigned int i = 15; i >= 1; --i)
                if(IPEND & (0x1 << i)) 
                    return i;
            return 0;
        }

        void ClearIRQ(u32 IRL) {
            IRL = IRL & 0xf;
            IPEND = IPEND & ~(0x1 << IRL);
        }





        u32 Read(u32 offset) const {
            std::cout << "Read IRQ at offset " << std::hex << offset << "\n";
            if(offset >= 0x40 && offset < 0x60) {
                u32 n = offset - 0x40;
                std::cout << "Read IRQ 0x40 + n*4, PIMASK[" << n << "] = " << std::hex << PIMASK[n] << std::dec << "\n";
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
        void Write(u32 offset, u32 value) {
            std::cout << "write IRQ at offset " << std::hex << offset << ", value= " << value << "\n";
            
            if(offset >= 0x40 && offset < 0x60) {
                u32 n = offset - 0x40;
                std::cout << "Write IRQ 0x40 + n*4, PIMASK[" << n << "] = " << std::hex << value << std::dec << "\n";
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
                case(IRQMP_BRDCST_OS):
                    BRDCST = value;
                    break;
                case(IRQMP_ERRSTAT_OS):
                    ERRSTAT = value;
                    break;
                default:
 					throw not_implemented_exception();  
                    return ;
            }
            return; 
        }



};





#endif

