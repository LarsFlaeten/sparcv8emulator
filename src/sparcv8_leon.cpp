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
#include "peripherals/ac97.hpp"

#include <pthread.h>

void set_thread_name(const char* name) {
    pthread_setname_np(pthread_self(), name);
}

#include "readelf.h"
#include "dis.h"


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
#include "peripherals/amba_pnp_dump.hpp"

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

    int PrintCount       = 0;
    bool Disassemble      = false;
    bool verbose          = false;
    bool dump_mem    = false;
    bool dump_amba_pnp = false;
    bool exit_early = false;
    std::string fname = "/home/lars/workspace/gaisler-buildroot-2024.02-1.1/output/images/image.ram";
    std::ofstream   os;
    int    option;

    bool debug_server = false; 
    int debug_port = 0; // Supress uninitiliazed warning
    bool write_to_file = false; 

    // Process the command line options 
    while ((option = getopt(argc, argv, "vdchempo:i:g:")) != EOF)
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
        case 'p':
            dump_amba_pnp = true;
            break;
        case 'm':
            dump_mem = true;
            break;
        case 'e':
            exit_early = true;
            break;
        case 'g':
            debug_port = (u32)strtol(optarg, NULL, 0);
            debug_server = true;
            break;
        case 'h':
        case '?':
        default:
            fprintf(stderr, 
                    "Usage: %s [-v] [-d] [-n <num instructions>] [-b <breakpoint addr>] [-o <filename>] \n"
                    "\n"
                    "    -v Turn on Verbose display\n"
                    "    -d Disassemble mode\n"
                    "    -o Output file for Verbose data (default stdout)\n"
                    "    -g (port) Start gdb server on specified port\n"
                    "    -p Dump AMBA PNP area on startup\n"
                    "    -m Dump Main memory banks on startup\n"
                    "    -e Exit before starting the actucal CPU"
                    "    -i path/file: Path to the linux buildroot image\n"
                    "\n"
                    , argv[0]);
            exit(EXIT_SUCCESS);
            break;
        }

    // Set up CPU
    int num_cpus = 1;
    MCtrl mctrl;
    IRQMP intc(num_cpus);

    
    

    // Create the AC'97 PCI peripheral
    // Instead of handing over mctrl, we give it read/write lambdas
    // TODO: Redesign this...
    auto mem_read = [&mctrl](uint32_t pa, void* val, size_t sz) -> bool {
        
        switch(sz) {
            case 1: {
                u8* p = static_cast<u8*>(val);
                *p = mctrl.read8(pa);
                break;
            }
            case 2: {
                u16* p = static_cast<u16*>(val);
                //*p = mctrl.read16(pa);
                *p = std::byteswap(mctrl.read16(pa));
                break;
            }
            case 4: {
                u32* p = static_cast<u32*>(val);
                //*p = mctrl.read32(pa);
                *p = std::byteswap(mctrl.read32(pa));
                break;
            }
            default:
               throw std::runtime_error("memread lambda, wrong size: " + std::to_string(sz));
         
        }
        return true;
    };
    auto mem_write = [&mctrl](uint32_t va, const void* val, size_t sz) -> bool {
        throw std::runtime_error("memwrite lambda");
        return true;
    };
    
    auto ac97pci = std::make_unique<AC97Pci>(0, mem_read, mem_write, mctrl);

    // Main RAM bank
    mctrl.attach_bank<RamBank>(0x40000000, 64 * 1024 * 1024); // Main memory
 

    // Video RAM bank
    //mctrl.attach_bank<RamBank>(0x20000000, 8 * 1024 * 1024); // 8MB video memory
 
    // APB CTRL area
    mctrl.attach_bank<APBCTRL>(0x80000000, mctrl, intc);
    auto& apbctrl= reinterpret_cast<APBCTRL&>(*mctrl.find_bank(0x80000000));
    

    // PCI MMIO BARS
    mctrl.attach_bank<PCIMMIOBank>(*ac97pci, 0x24000800, 0x100);
    mctrl.attach_bank<PCIMMIOBank>(*ac97pci, 0x24000900, 0x100);
    // PCI RAM bank
    mctrl.attach_bank<RamBank>(0x24000000, 0x800);
 
     

    
    // Amba PNP area
    mctrl.attach_bank<RomBank<64 * 1024>>(0xffff0000);
    mctrl.attach_bank<RomBank<4 * 1024>>(0x800ff000);
    
    // PCI io + config 0xfffa0000 - 0xfffbffff, 128 kb
    //mctrl.attach_bank<RamBank>(0xfffa0000, 64 * 1024); // PCI IO
    GRPCI2& grpci2 = apbctrl.GetGRPCI2();
    grpci2.attach_device(std::move(ac97pci));
    mctrl.attach_bank<PCIIOCfgArea>(0xfffa0000, grpci2); // PCI CFG
    
    

    amba_ahb_pnp_setup(mctrl);
    amba_apb_pnp_setup(mctrl);

    // Debug print amba pnp
    if(dump_amba_pnp) {
        debug_print_amba_pnp();
    }


    
    
    //SVGA svga(mctrl);
    
    //apbctrl.add_slave(svga, 0x800ff020, 0x80000400, 9);


    if(dump_mem)
        debug_print_memory_banks();
        

    // Read the ELF and get the entry point, then reset
    u32 entry_va = 0x0; 
    if(ReadElf(fname, mctrl, entry_va, false, std::cout) == 0) {
        std::cerr << "No bytes returned from ELF read.\n";
        return EXIT_FAILURE; 
    }

    // OS boot process step 1: Set stack pointer to end of ram
    u32 end_of_ram = mctrl.find_bank(0x40000000)->get_limit();
    
    // Call back for bus tick:
    static std::function<void()> tick_lambda =
    [&apbctrl]() 
    {
        GPTIMER& timer = apbctrl.GetTimer();
        IRQMP& intc = apbctrl.GetIntc();
        APBUART& uart1 = apbctrl.GetUART();
        APBUART& uart9 = apbctrl.GetUART9();
        GRPCI2& grpci = apbctrl.GetGRPCI2();

        timer.Tick();
        uart1.Input();
        grpci.tick();

        if (timer.check_interrupt(false))
            intc.trigger_irq(8);

        if (uart1.CheckIRQ()) 
            intc.trigger_irq(4);

        if (uart9.CheckIRQ()) 
            intc.trigger_irq(3);
    };

    // Build the vector of CPUs
    std::vector<std::unique_ptr<CPU>> cpus{};
    for(int i = 0; i < num_cpus; ++i) {
        auto& cpu = cpus.emplace_back(std::make_unique<CPU>(mctrl, intc, i, write_to_file ? os : std::cout)); 
        cpu->set_verbose(verbose);
        cpu->register_bus_tick_function( tick_lambda );

        cpu->reset(entry_va);

        cpu->write_reg(end_of_ram - 0x180, OUTREG6); // Write stack pointer
        cpu->write_reg(end_of_ram, INREG6); // Write frame pointer
    
        intc.set_cpu_ptr(cpu.get(), i);
        
    }
    
    
    _cpu_ptr = cpus[0].get();
    debug_set_active_mmu(&(cpus[0].get()->get_mmu()));
    
    
    
    
    

    // Set up timer to the same state as TSIM starts with
    GPTIMER& timer = apbctrl.GetTimer();
    timer.set_LEON_state();
 
    
    

    

    
    // Set up handler for external SIGINTs
    struct sigaction act;

    act.sa_handler =  signal_handler;
    sigaction(SIGINT, &act, NULL);
 

    if(exit_early)
        return EXIT_SUCCESS;

    // Set up gdb stub
    //GdbStub gdb_stub{cpus, mmu};
    auto gdb_stub = std::make_unique<GdbStub>(cpus);  

    RunSummary rs;
    if(!Disassemble) {

        
        if(debug_server) {
            cpus[0]->set_gdb_stub(gdb_stub.get());
            gdb_stub->start(debug_port);
        }

        // Run the machine in the main thread
        // Only run CPU 0 in this sim
        int    NumRunInst       = 0;
        cpus[0]->run(NumRunInst, &rs);
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

    return (rs.reason != TerminateReason::NORMAL) ? rs.reason : EXIT_SUCCESS;
}

