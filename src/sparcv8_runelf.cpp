//=============================================================
// 
// Copyright (c) 2015 Lars Flæten
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
#include "peripherals/MCTRL.h"
#include "peripherals/APBCTRL.h"
#include "readelf.h"
#include "dis.h"
#include "debug.h"
#include "gdb/gdb_stub.hpp"

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
                    "    -f Specify executable ELF file (default main.out)\n"
                    "    -o Output file for Verbose data (default stdout)\n"
                    "\n"
                    , argv[0]);
            exit(NOERROR);
            break;
        }

    // Set up CPU
    MCtrl mctrl;
    
    // Setup RAM
    mctrl.attach_bank<RamBank>(0x00000000, 1 * 1024 * 1024);
    mctrl.debug_list_banks();

    // We only instanciate a lone interruptc controller, since the CPU needs a reference
    // to it. It is likely not used by any bare bone ELFs.
    IRQMP intc(1);


    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    u32 word_count = ReadElf(fname, mctrl, entry_va, true, std::cout); 
    
    u32 end_of_ram = mctrl.find_bank(0x00000000)->get_end_exclusive();

    

    std::vector<std::unique_ptr<CPU>> cpus{};
    int num_cpus = 1;

    for(int i = 0; i < num_cpus; ++i) {
        std::cout << "Creating CPU, id=" << i << "\n";
        auto& cpu = cpus.emplace_back(std::make_unique<CPU>(mctrl, intc, i, write_to_file ? os : std::cout));
        cpu->set_verbose(verbose);
        cpu->reset(entry_va);
        // OS boot process step 1: Set stack pointer to end of ram
        cpu->write_reg(end_of_ram - 0x180, OUTREG6); // Write stack pointer
        cpu->write_reg(end_of_ram, INREG6); // Write frame pointer
    
    }    
    
    

    
     
    RunSummary rs;
    if(!Disassemble) {
        
        GdbStub gdb_stub{cpus};
        
        if(debug_server) {
            cpus[0]->set_gdb_stub(&gdb_stub);
            //gdb_stub.insert_breakpoint(entry_va);
            gdb_stub.start(debug_port);
        }
        // Run the machine, only first cpu in this emulator
        cpus[0]->run(NumRunInst, &rs);
    } else 
    {
        u32 PC = entry_va;
        u32 count = word_count;
        struct DecodeStruct Dec, *d=&Dec;
 
        while(count > 0) {
            cpus[0]->instr_fetch(PC, d);

            disDecodePrint(PC, d->opcode);
            PC += 4;
            --count;
        }
        rs.instr_count = word_count;
    }

    
    if(rs.reason == TerminateReason::UNIMPLEMENTED) {
        debug_registerdump(*cpus[0]); 
        disDecodePrint(cpus[0]->get_pc(), rs.last_opcode);
    } 
    

    
    
    
    if (PrintCount)
         (write_to_file ? os : std::cout) << "Instruction count = " <<  std::dec << rs.instr_count << "\n";

    os.close();

    #ifdef PROFILE_MEM_ACCESS
    mctrl.print_profile();
    #endif

    return (rs.reason != TerminateReason::NORMAL) ? rs.reason : NOERROR;
}

