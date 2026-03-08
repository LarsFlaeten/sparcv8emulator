// SPDX-License-Identifier: MIT
#include "peripherals/APBUART.h"

int main() {
    APBUART uart;
    std::vector<char> command;

    while(true) {
        uart.input();

        u32 index = 0;
        u32 r = uart.read(index);

        if(r == 10) {
            std::cout << "\n>";
            for(auto i : command)
                std::cout << i;
            std::cout << std::endl;
            command.clear();

        } else if(r > 0) {
            command.push_back(r);
            uart.write(index, r);
        }

    }
    
}
