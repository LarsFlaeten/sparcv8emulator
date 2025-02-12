//=============================================================
// 
// Copyright (c) 2004 Simon Southwell
//
// Date: 13th October 2004
//
// This file is part of sparc_iss.
//
// sparc_iss is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// sparc_iss is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with sparc_iss. If not, see <http://www.gnu.org/licenses/>.
//
// $Id: sparc_iss.c,v 1.5 2016-10-18 05:53:31 simon Exp $
// $Source: /home/simon/CVS/src/cpu/sparc/src/sparc_iss.c,v $
//
//=============================================================

#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <thread>

#if !defined(_WIN32) && !defined(_WIN64)
#include <getopt.h>
#else
extern char* optarg;
#endif
#include "sparcv8/CPU.h"
#include "sparcv8/MMU.h"
#include "peripherals/Peripherals.h"
#include "peripherals/Amba.h"
#include "peripherals/APBMST.h"
#include "gdb/gdb_server.h"



#include "readelf.h"
#include "dis.h"
constexpr int FOREVER =           0;
constexpr int ONCE =              1;

constexpr u32 NOERROR =           0;
constexpr u32 USER_ERROR =        1;
constexpr u32 UNIMP_ERROR =       2;
constexpr u32 RUNTIME_ERROR =     3;
constexpr u32 CODE_ERROR =        4;

constexpr u32 SPARC_MEM_CB_RD =   0;
constexpr u32 SPARC_MEM_CB_WR =   1;

constexpr s32 TERMINATE_NORM =    -1;
constexpr s32 TERMINATE_BREAK =    1;
constexpr s32 TERMINATE_STEP =     2;


std::string uart_readline(APBUART& uart) {
    std::string command;
    while(true) {
        uart.Input();

        u32 index = 0;
        u32 r = uart.Read(index);

        if(r == 10) {
            return command;

        } else if(r > 0) {
            command.push_back(r);
            uart.Write(index, r);
        }

    }
                
}

#include "debug.h"

bool ParseCommand(const std::string& cmd, CPU& cpu) {

    if(cmd == "c") {
        cpu.SetSingleStep(false);
        std::cout << std::endl;
        return false;
    } else if(cmd == "n") {
        std::cout << std::endl;
        return false;
    } else if(cmd.starts_with("bp")) {
        if(cmd.length() > 2) {
            u32 bp = std::stoul(cmd.substr(3), nullptr, 16);
            cpu.AddUserBreakpoint(bp);
            std::cout << "\nAdded breakpoint at 0x" << std::hex << bp << std::dec << std::endl;
        } else {
            auto& m = cpu.GetUserBreakpoints();
            std::cout << "\nBp adress | enabled?" << std::endl;
            for(const auto& [key, val] : m) {
                std::cout << std::hex << "0x" << key << " | " << std::dec << val << std::endl;
            }

        }   
    } else if(cmd.starts_with("dis")){
        u32 va = cpu.GetPC();
        u32 opcode;

        if(cmd.length() > 4)
            opcode = std::stoul(cmd.substr(4), nullptr, 16);
        else// Fetch current instruction
            MMU::MemAccess<intent_execute, 4>(va, opcode, CROSS_ENDIAN);
        
        disDecode(va, opcode);
        
    } else if(cmd.starts_with("mem ")) {
        if(cmd.length() > 4) {
            u32 pa = std::stoul(cmd.substr(4), nullptr, 16);
            std::cout << "\n";
            debug_dumpmem(pa);
            
        }
    } else if(cmd.starts_with("memv ")) {
        if(cmd.length() > 5) {
            u32 va = std::stoul(cmd.substr(4), nullptr, 16);
            std::cout << "\n";
            debug_dumpmemv(va);
            
        }
    } else if(cmd == "reg") {
        std::cout << "\n";
        debug_registerdump(cpu);
    } else {
        std::cout << "\nUnkown command" << std::endl;
    }

    return true;
}

int main(int argc, char **argv)
{
    int    PrintCount       = 0;
    bool    Disassemble      = false;
    u32 UserBreakpoint   = NO_USER_BREAK;
    int    NumRunInst       = FOREVER;
    bool    verbose          = false;
    std::string fname = "../image/image.ram";
    std::ofstream   os;
    int    option;

    bool debug_server = false; 
    int debug_port;
    bool write_to_file = false; 
    // Process the command line options 
    while ((option = getopt(argc, argv, "vdn:b:o:i:cg:")) != EOF)
        switch(option) {
        case 'o':
            os.open(optarg, std::ios_base::out); 
            if (!os.is_open()) {
                std::cerr << "*** main(): Unable to open file " << optarg << " for writing\n";
                exit(1);
            }
            write_to_file = true;
            break;
        case 'i':
            fname = optarg;
            break;
        case 'c':
            PrintCount = 1;
            break;
        case 'd':
            Disassemble = true;
            break;
        case 'v':
            verbose = true;
            break;
        case 'n':
            NumRunInst = strtol(optarg, NULL, 0);
            if (NumRunInst < FOREVER)
                NumRunInst = FOREVER;
            break;
        case 'g':
            debug_port = (u32)strtol(optarg, NULL, 0);
            debug_server = true;
            break;
        case 'b':
            UserBreakpoint = (u32)strtol(optarg, NULL, 0);
            std::cerr << "Breaking at 0x" << std::hex << UserBreakpoint << "\n";
            break;
        case 'h':
        case '?':
        default:
            fprintf(stderr, 
                    "Usage: %s [-v] [-d] [-n <num instructions>] [-b <breakpoint addr>] [-o <filename>] \n"
                    "\n"
                    "    -v Turn on Verbose display\n"
                    "    -d Disassemble mode\n"
                    "    -n Specify number of instructions to run (default: run until UNIMP)\n"
                    "    -b Specify address for breakpoint\n"
                    "    -o Output file for Verbose data (default stdout)\n"
                    "    -g (port) Start gdb server on specified port\n"
                    "    -i path/file: Path to the linux buildroot image\n"
                    "\n"
                    , argv[0]);
            exit(NOERROR);
            break;
        }

    // Set up CPU
    CPU cpu(write_to_file ? os : std::cout);
    cpu.SetVerbose(verbose);
    cpu.SetId(0);
    // Set Cache control regs as TSIM does
    MMU::SetCCR(0x00020000);
    MMU::SetICCR(0x10220008);
    MMU::SetDCCR(0x18220008);


    // RAM
    SDRAM<0x02000000> RAM;   // IO: 0x60000000, 32 MB of RAM
    SDRAM<0x00100000> RAM2;  // IO: 0xffd03000, 1 MB of RAM
    SDRAM<0x00800000> RAM3;  // IO: 0x00000000, 8 MB of RAM


    // Set up amba IO area:
    SDRAM2 amba_ahb(0x100000); // AMBA resides from 0xfff00000 -> 0xfffffff0 (+ u32)
    amba_ahb_setup(amba_ahb);
    SDRAM2 amba_apb(0x010000); // AMBA resides from 0x80000000 -> 0x800ffff0 (+ u32)
    amba_apb_setup(amba_apb, 0x800f0000);



    // Set up IO mapping
    // TODO: Move this MMU functions?
    u32 base_ram = 0x60000000;
    u32 size_ram = RAM.getSizeBytes();
    u32 start = base_ram/0x10000;
    u32 end = (base_ram + size_ram)/0x10000;
    for(unsigned a = start; a < end; ++a)
        MMU::IOmap[a] = { [&RAM](u32 i)          { return RAM.Read( (i-0x60000000)/4); },
                          [&RAM](u32 i, u32 v)   {        RAM.Write((i-0x60000000)/4, v);    } };
    
    // This area is only used for a bss section in the ELF. Should really not be needed
    // bu cant be avoided as ELFreader trier to allocate memory at this location
/*    base_ram = 0xffd00000;
    size_ram = RAM2.getSizeBytes();
    start = base_ram/0x10000;
    end = (base_ram + size_ram)/0x10000;
    
    for(unsigned a = start; a < end; ++a)
        MMU::IOmap[a] = { [&RAM2](u32 i)          {  return RAM2.Read((i-0xffd00000)/4); },
                         [&RAM2](u32 i, u32 v)   {   RAM2.Write((i-0xffd00000)/4, v);    } };
 */
    // Test 8MB of low RAM
    base_ram = 0x00000000;
    size_ram = RAM3.getSizeBytes();
    start = base_ram/0x10000;
    end = (base_ram + size_ram)/0x10000;
    
    for(unsigned a = start; a < end; ++a)
        MMU::IOmap[a] = { [&RAM3](u32 i)          { /*std::cout << "READ: " << std::hex << i << "\n";*/ return RAM3.Read(i/4); },
                         [&RAM3](u32 i, u32 v)   {  /*std::cout << "WRITE: " << std::hex << i << ": " << v << "\n";*/ RAM3.Write(i/4, v);    } };
    
    // IO mapping for AMBA AHB IO AREA
    u32 base_amba_ahb_io = 0xfff00000;
    u32 end_amba_ahb_io =  0xffffffff;
    start = base_amba_ahb_io/0x10000;
    end =    end_amba_ahb_io/0x10000;
    
    for(unsigned a = start; a <= end; ++a) {
        //std::cout << "Mapping 0x" << std::hex << a << "0000 to 0x" << a << "ffff\n";
        MMU::IOmap[a] = { [&amba_ahb](u32 i)          { return amba_ahb.Read((i-0xfff00000)/4); } ,
                          [&amba_ahb](u32 i, u32 v)   { amba_ahb.Write((i-0xfff00000)/4, v);    } };
    }

    // IO mapping for AMBA APB pnp IO AREA
    u32 base_amba_apb_io = 0x800f0000;
    u32 end_amba_apb_io =  0x800fffff;
    start = base_amba_apb_io/0x10000;
    end =    end_amba_apb_io/0x10000;
    
    for(unsigned a = start; a <= end; ++a) {
        //std::cout << "Mapping 0x" << std::hex << a << "0000 to 0x" << a << "ffff\n";
        MMU::IOmap[a] = { [&amba_apb](u32 i)          { return amba_apb.Read((i-0x800f0000)/4); } ,
                          [&amba_apb](u32 i, u32 v)   { amba_apb.Write((i-0x800f0000)/4, v);    } };
    }

    APBCTRL apbctrl;
    // IO mapping for AMBA APB bus IO AREA
    u32 base_apbctrl_io = 0x80000000;
    u32 end_apbctrl_io =  0x800effff;
    start = base_apbctrl_io/0x10000;
    end =    end_apbctrl_io/0x10000;
    
    for(unsigned a = start; a <= end; ++a) {
        //std::cout << "Mapping 0x" << std::hex << a << "0000 to 0x" << a << "ffff\n";
        MMU::IOmap[a] = { [&apbctrl](u32 i)          { return apbctrl.Read(i); } ,
                          [&apbctrl](u32 i, u32 v)   { apbctrl.Write(i, v);    } };
    }

    // Set up breakpoint handling (we need uart from APBctrl) 
    auto& uart = apbctrl.GetUART();
    if(UserBreakpoint != NO_USER_BREAK) {
        cpu.AddUserBreakpoint(UserBreakpoint);
    } 
    
    cpu.RegisterBreakpointFunction( [&cpu, &uart]() {
            u32 va = cpu.GetPC();
            cpu.SetSingleStep(true);
            
            while(true) {
                std::cout << "<bp 0x" << std::hex << va << std::dec << "> " << std::flush;
           
            
                std::string cmd = uart_readline(uart);
                if(!ParseCommand(cmd, cpu))
                    break;
           }
       }
    );

   
    // Set up the tick, input polling and interrupt
    cpu.RegisterBusTickFunction( [&apbctrl , &cpu]() 
        {
            GPTIMER& timer = apbctrl.GetTimer();
            IRQMP& intc = apbctrl.GetIntc();
    
            timer.Tick();

            if(timer.CheckInterrupt(true)) intc.TriggerIRQ(8);
        
            u32 IRL = intc.GetNextIRQPending();
            if(IRL>0) {
                cpu.SetIRL(IRL);
                intc.ClearIRQ(IRL);
            } 
        }
    );

    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    u32 word_count = ReadElf(fname, cpu, entry_va); 
    cpu.Reset(entry_va);

    //we want 0xffd03170 to point to 0x60c5c8a0
    // with end of ram it points to 0x61fef170
    // Difference is 0x13928d0
    // OS boot process step 1: Set stack pointer to end of ram
    u32 end_of_ram = 0x60000000 + RAM.getSizeBytes(); 
    //u32 end_of_ram = 0x41fffe80; // Value from TSIM
    //u32 end_of_ram = 0x42000000; // Value from TSIM

    cpu.WriteReg(end_of_ram - 0x180, OUTREG6); // Write stack pointer
    cpu.WriteReg(end_of_ram, INREG6); // Write frame pointer
     
    RunSummary rs;
    if(!Disassemble && !debug_server) {
        // Run the machine in the main thread
       cpu.Run(NumRunInst, &rs);

    } else if(debug_server) {
		int server_fd = create_server_socket(debug_port);
        int client_fd = accept(server_fd, NULL, NULL);
        
        if (client_fd < 0) {
        	perror("Client connection failed");
        	close(server_fd);
        	exit(EXIT_FAILURE);
    	}
   
        // Add a breakpoint at the entry 
        //cpu.AddUserBreakpoint(entry_va);
        //std::cout << "Atomatically addded breakpoint at entry, PC=0x" << std::hex << entry_va << std::dec << "\n"; 
        //std::thread t1(&CPU::Run, &cpu, NumRunInst, &rs);


	    handle_gdb_client(client_fd, cpu); 

        //t1.join();
        close(client_fd);
        close(server_fd);

    } else
    {
        u32 PC = entry_va;
        u32 count = word_count;
        while(count > 0) {
            u32 opcode;
            cpu.IFetch(PC, opcode);
            disDecode(PC, opcode);
            PC += 4;
            --count;
        }
        rs.instr_count = word_count;
    }

    if(rs.reason == TerminateReason::UNIMPLEMENTED) {
        debug_registerdump(cpu); 
        disDecode(cpu.GetPC(), rs.last_opcode);
    } 
    
    
    
    if (PrintCount)
         (write_to_file ? os : std::cout) << "Instruction count = " <<  std::dec << rs.instr_count << "\n";

    os.close();

    return (rs.reason != TerminateReason::NORMAL) ? rs.reason : NOERROR;
}

