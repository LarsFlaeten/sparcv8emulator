#ifndef _PERIPHERALS_H_
#define _PERIPHERALS_H_

#include <map>
#include <iostream> 

class AMBA_IO {
    public:

        AMBA_IO() {
            // Set device id for high address, used by Leon to query amba at boot
            m[0xfffffff0] = 0x07401039;
        }
        
        u32 Read(u32 va) {
            switch(va) {
                case(0xfffffff0):
                    std::cout << "AMBA: queried on 0xfffffff0 (device_ID)\n";
                    break;
                default:
                    std::cout << "AMBA: queried on 0x"<< std::hex << va << "\n";
 

            }

             return m[va];

        }

        void Write(u32 va, u32 value) {
            throw std::runtime_error("AMBA IO is R/O");
            m[va] = value;

        }
    private:
        std::map<u32, u32> m;


};





#endif
