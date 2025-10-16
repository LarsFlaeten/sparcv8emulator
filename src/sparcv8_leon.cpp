//=============================================================
// 
//
//
//=============================================================

#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <thread>
#include <signal.h>

#if !defined(_WIN32) && !defined(_WIN64)
#include <getopt.h>
#else
extern char* optarg;
#endif

#include "sparcv8/CPU.h"
#include "sparcv8/MMU.h"
#include "peripherals/Peripherals.h"
#include "peripherals/Amba.h"
#include "peripherals/APBCTRL.h"

#include <pthread.h>

void set_thread_name(const char* name) {
    pthread_setname_np(pthread_self(), name);
}

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

CPU* _cpu_ptr = nullptr;


std::string uart_readline(APBUART& uart) {
    std::string command;
    while(true) {
        uart.Input();

        u32 index = 0;
        u32 r = uart.read(index);

        if(r == 10) {
            return command;

        } else if(r > 0) {
            command.push_back(r);
            uart.write(index, r);
        }

    }
                
}

#include "debug.h"


void signal_handler(int signal) 
{
    std::cout << "Signal " << signal << " caught..\n";
    if((signal == SIGINT) && _cpu_ptr) {
        std::cout << "SIGINT!\n";
        _cpu_ptr->interrupt();

    }

}


int main(int argc, char **argv)
{
    set_thread_name("main_entry");

    int    PrintCount       = 0;
    bool    Disassemble      = false;
    u32 UserBreakpoint   = NO_USER_BREAK;
    int    NumRunInst       = FOREVER;
    bool    verbose          = false;
    std::string fname = "../image/image.ram";
    std::ofstream   os;
    int    option;

    bool debug_server = false; 
    int debug_port = 0; // Supress uninitiliazed warning
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
    MCtrl mctrl;
    MMU mmu(mctrl);
    debug_set_active_mmu(&mmu);
    
    // Set Cache control regs as TSIM does
    mmu.SetCCR(0x00020000);
    mmu.SetICCR(0x10220008);
    mmu.SetDCCR(0x18220008);

    // Video RAM bank
    mctrl.attach_bank<RamBank>(0x20000000, 8 * 1024 * 1024); // 8MB video memory
 

    // Main RAM bank
    mctrl.attach_bank<RamBank>(0x40000000, 64 * 1024 * 1024); // Main memory
 
    // Amba PNP area
    mctrl.attach_bank<RomBank<64 * 1024>>(0xffff0000);
    mctrl.attach_bank<RomBank<4 * 1024>>(0x800ff000);
    
    amba_ahb_pnp_setup(mctrl);
    amba_apb_pnp_setup(mctrl);

    
    mctrl.attach_bank<APBCTRL>(0x80000000, mctrl);
    auto& apbctrl= reinterpret_cast<APBCTRL&>(*mctrl.find_bank(0x80000000));
    IRQMP& intc = apbctrl.GetIntc();
    
    //SVGA svga(mctrl);
    
    //apbctrl.add_slave(svga, 0x800ff020, 0x80000400, 9);



    mctrl.debug_list_banks();

    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    if(ReadElf(fname, mmu, entry_va, false, std::cout) == 0) {
        std::cerr << "No bytes returned from ELF read.\n";
        return EXIT_FAILURE; 
    }

    // OS boot process step 1: Set stack pointer to end of ram
    u32 end_of_ram = mctrl.find_bank(0x40000000)->get_limit();

    // Build the vector of CPUs
    std::vector<CPU> cpus{};
    int num_cpus = 1;
    for(int i = 0; i < num_cpus; ++i) {
        auto& cpu = cpus.emplace_back(CPU{mmu, intc, write_to_file ? os : std::cout}); 
        cpu.set_verbose(verbose);
        cpu.set_cpu_id(i);

        cpu.register_bus_tick_function( [&apbctrl]() 
            {
                GPTIMER& timer = apbctrl.GetTimer();
                IRQMP& intc = apbctrl.GetIntc();
                APBUART& uart1 = apbctrl.GetUART();
                APBUART& uart9 = apbctrl.GetUART9();
                timer.Tick();
                uart1.Input();

                if(timer.check_interrupt(false))
                    intc.TriggerIRQ(8);
                
                if(uart1.CheckIRQ()) 
                    intc.TriggerIRQ(2);
                if(uart9.CheckIRQ()) 
                    intc.TriggerIRQ(3);  
            }
        );

        cpu.reset(entry_va);

        cpu.write_reg(end_of_ram - 0x180, OUTREG6); // Write stack pointer
        cpu.write_reg(end_of_ram, INREG6); // Write frame pointer
    }
    
    
    _cpu_ptr = cpus.data();
    
    
    
    

    // Set up timer to the same state as TSIM starts with
    GPTIMER& timer = apbctrl.GetTimer();
    timer.set_LEON_state();
 
    
    

    

    
    // Set up handler for external SIGINTs
    struct sigaction act;

    act.sa_handler =  signal_handler;
    sigaction(SIGINT, &act, NULL);
 
    // Set up gdb stub
    //GdbStub gdb_stub{cpus, mmu};
    auto gdb_stub = std::make_unique<GdbStub>(cpus, mmu);  

    RunSummary rs;
    if(!Disassemble) {

        
        if(debug_server) {
            cpus[0].set_gdb_stub(gdb_stub.get());
            gdb_stub->start(debug_port);
        }

        // Run the machine in the main thread
        // Only run CPU 0 in this sim
        cpus[0].run(NumRunInst, &rs);
    }

    if(rs.reason == TerminateReason::UNIMPLEMENTED) {
        debug_registerdump(cpus[0]); 
        disDecodePrint(cpus[0].get_pc(), rs.last_opcode);
    } 
    
    
    
    if (PrintCount)
         (write_to_file ? os : std::cout) << "Instruction count = " <<  std::dec << rs.instr_count << "\n";

    os.close();

    return (rs.reason != TerminateReason::NORMAL) ? rs.reason : NOERROR;
}

