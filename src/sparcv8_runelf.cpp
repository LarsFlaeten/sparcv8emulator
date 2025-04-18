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
#include <time.h>



#if !defined(_WIN32) && !defined(_WIN64)
#include <getopt.h>
#else
extern char* optarg;
#endif
#include "sparcv8/CPU.h"
#include "sparcv8/MMU.h"
#include "readelf.h"
#include "dis.h"
#include "debug.h"
#include "gdb/gdb_server.h"

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

 

int main(int argc, char **argv)
{
    std::string  fname = "main.aout";
    int    PrintCount       = 0;
    bool    Disassemble      = false;
    u32 UserBreakpoint   = NO_USER_BREAK;
    int    NumRunInst       = FOREVER;
    bool    verbose          = false;

    std::ofstream   os;
    int    option;

    bool debug_server = false; 
    int debug_port = 0; // To avoid warning
    bool write_to_file = false; 
    // Process the command line options 
    while ((option = getopt(argc, argv, "f:vdn:b:g:o:c")) != EOF)
        switch(option) {
        case 'o':
            os.open(optarg, std::ios_base::out); 
            if (!os.is_open()) {
                std::cerr << "*** main(): Unable to open file " << optarg << " for writing\n";
                exit(1);
            }
            write_to_file = true;
            break;
        case 'f':
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
        case 'b':
            UserBreakpoint = (u32)strtol(optarg, NULL, 0);
            break;
        case 'g':
            debug_port = (u32)strtol(optarg, NULL, 0);
            debug_server = true;
            break;
        case 'h':
        case '?':
        default:
            fprintf(stderr, 
                    "Usage: %s [-v] [-d] [-n <num instructions>] [-b <breakpoint addr>] [-f <filename>] [-o <filename>] \n"
                    "\n"
                    "    -v Turn on Verbose display\n"
                    "    -d Disassemble mode\n"
                    "    -n Specify number of instructions to run (default: run until UNIMP)\n"
                    "    -b Specify address for breakpoint\n"
                    "    -f Specify executable ELF file (default main.out)\n"
                    "    -o Output file for Verbose data (default stdout)\n"
                    "\n"
                    , argv[0]);
            exit(NOERROR);
            break;
        }

    // Set up CPU
    CPU cpu(write_to_file ? os : std::cout);
    cpu.set_verbose(verbose);
    if(UserBreakpoint != NO_USER_BREAK) {
        cpu.add_user_breakpoint(UserBreakpoint);
        // TODO: add bp handler here    
    }
    // RAM
    SDRAM<0x01000000> RAM;  // IO: 0x0, 16 MB of RAM

    // Set up IO mapping
    // TODO: Move this MMU functions?
    for(unsigned a = 0x0; a < 0x100; ++a)
        MMU::IOmap[a] = { [&RAM](u32 i)          { return RAM.Read(i/4); },
                         [&RAM](u32 i, u32 v)   { RAM.Write(i/4, v);    } };

    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    u32 word_count = ReadElf(fname, cpu, entry_va); 
    cpu.reset(entry_va);
    
     
    RunSummary rs;
    if(!Disassemble && !debug_server) {
        // Run the machine
        cpu.run(NumRunInst, &rs);
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
        struct DecodeStruct Dec, *d=&Dec;
 
        while(count > 0) {
            cpu.instr_fetch(PC, d);

            disDecode(PC, d->opcode);
            PC += 4;
            --count;
        }
        rs.instr_count = word_count;
    }

    
    if(rs.reason == TerminateReason::UNIMPLEMENTED) {
        debug_registerdump(cpu); 
        disDecode(cpu.get_pc(), rs.last_opcode);
    } 
    

    
    
    
    if (PrintCount)
         (write_to_file ? os : std::cout) << "Instruction count = " <<  std::dec << rs.instr_count << "\n";

    os.close();

    return (rs.reason != TerminateReason::NORMAL) ? rs.reason : NOERROR;
}

