// SPDX-License-Identifier: MIT



#include "LoopTimer.h"


int main(int argc, char **argv)
{
        LoopTimer lt;
        lt.start(); 
        
        int i = 0;
        struct timespec req = {0, 10000};  // 10 ms = 10,000,000 ns
        
       
        while(i < 1000) {
            lt.start();
            //nanosleep(&req, NULL);
            ++i;
            lt.stop(0);
        }

        lt.printStats();





}

