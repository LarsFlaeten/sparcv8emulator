// Implementeation of a Leon SMP system

// std
#include <iostream>
#include <stdexcept>
#include <thread>

#include <signal.h>

#include "sparcv8/CPU.h"
#include "sparcv8/MMU.h"
#include "peripherals/Peripherals.h"
#include "peripherals/Amba.h"
#include "peripherals/APBCTRL.h"
#include "peripherals/BusClock.hpp"

#include "readelf.h"
#include "debug.h"
#include "mutexprofiler.hpp"

#include "cv_log.hpp"

#include "gdb/DebugStopController.hpp"

#include <pthread.h>
void set_thread_name(const std::string& name) {
    pthread_setname_np(pthread_self(), name.c_str());
}

// Global cpu list for signal handler
std::vector<std::unique_ptr<CPU>>* g_cpus = nullptr;

void signal_handler(int signal)
{
    std::cout << "Signal " << signal << " caught..\n";
    switch(signal) {
        case(SIGINT):
        case(SIGTERM):
            if(auto dsc = DebugStopController::Global()) {
                std::cout << "[MAIN] Releasing threads from stop-the-world\n";
                dsc->resume();
            }
            if(g_cpus) {
                for(auto& cpu : *g_cpus)
                    cpu->interrupt();
            }
            break;
        default:
            std::cout << "Unhandled signal.\n";
            break;
    }
}

struct EmulatorConfig {
    unsigned int num_cpus = 8;
    // System clock frequency reported to Linux via GPTIMER SRELOAD.
    // Must match GPTIMER default SRELOAD (0x31=49) → (49+1)*1MHz = 50 MHz.
    double system_freq_hz = 50'000'000.0;
};

void print_config(const EmulatorConfig& config) {
    std::cout << "\n=== Emulator Configuration ===\n";
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "Number of CPUs:       " << config.num_cpus << "\n";
    std::cout << "System frequency:     " << config.system_freq_hz << " Hz\n";
    std::cout << "==============================\n";
}

#if defined(PROFILE_LOCKS)
static inline double ns_to_ms(u64 ns) { return double(ns) / 1e6; }


void dump_ram_mutex_profile(std::vector<std::unique_ptr<CPU>>& cpus) {
    for (int i = 0; i < static_cast<int>(cpus.size()); ++i) {
        const auto& p = cpus[i]->get_mmu().mtx_profiles_.ram;
        const u64 acq = p.acquisitions.load(std::memory_order_relaxed);
        const u64 w   = p.wait_ns.load(std::memory_order_relaxed);
        const u64 h   = p.hold_ns.load(std::memory_order_relaxed);
        const u64 mw  = p.max_wait_ns.load(std::memory_order_relaxed);
        const u64 mh  = p.max_hold_ns.load(std::memory_order_relaxed);

        printf("[CPU%d][RAM] acq=%llu wait=%.3fms hold=%.3fms avg_wait=%.1fns avg_hold=%.1fns max_wait=%.3fms max_hold=%.3fms\n",
            i,
            (unsigned long long)acq,
            ns_to_ms(w), ns_to_ms(h),
            acq ? double(w)/double(acq) : 0.0,
            acq ? double(h)/double(acq) : 0.0,
            ns_to_ms(mw), ns_to_ms(mh));
    }
}
#endif

void cpu_thread(CPU& cpu) {
    set_thread_name(std::format("cpu{}", cpu.get_cpu_id()));

    std::cout << "[CPU THREAD " << (int)cpu.get_cpu_id() << "] CPU " << (int)cpu.get_cpu_id() << " entering holding pattern.\n";
    cpu.enter_powerdown();
    std::cout << "[CPU THREAD " << (int)cpu.get_cpu_id() << "] CPU " << (int)cpu.get_cpu_id() << " Starting up.\n";

    RunSummary rs{};
    try {
        cpu.run(0, &rs);
    } catch (const std::runtime_error& e) {
        std::cout << e.what() << "\n";
        debug_mmu_tables();
        debug_registerdump(cpu);
        throw std::runtime_error("Aborting...");
    }
}


int main(int argc, char **argv) {
    CVLOG_MUTE();
    
    DebugStopController dbg;
    DebugStopController::InstallGlobal(&dbg);

    set_thread_name("main");
    // Set up handler for external SIGINTs
    struct sigaction act;
    act.sa_handler =  signal_handler;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
 
    EmulatorConfig config{};

    int    option;
    int    num_cpus_requested = 0;
    bool debug_server = false; 
    int debug_port = 0; // Supress uninitiliazed warning
    std::string fname = "/home/lars//workspace/gaisler-buildroot-2024.02-1.1/output/images/image.ram";
    
    while ((option = getopt(argc, argv, "i:n:g:")) != EOF) {
        switch(option) {
            case 'i':
                fname = optarg;
                break;
            case 'n':
                num_cpus_requested = (u32)strtol(optarg, NULL, 0);
                break;
            case 'g':
                debug_port = (u32)strtol(optarg, NULL, 0);
                debug_server = true;
                break;
            default:
            std::cerr << 
                    "Usage: " << argv[0] << "[-i <filename>] \n"
                    "\n"
                     "    -i path/file: Path to the linux buildroot image\n"
                     "    -n [num]: Number of CPUs to emulate\n" 
                     "    -g (port) Start gdb server on specified port\n"
                    "\n";
            exit(EXIT_SUCCESS);
            break;
        }
    }

    if(num_cpus_requested < 1 || num_cpus_requested > 32) {
        std::cerr << "Requested number of cpus: " << num_cpus_requested << " (invalid), reverting to 1.\n";
        num_cpus_requested = 1;
    }



    
    // Set up machine
    MCtrl mctrl;
    

    // Main RAM bank
    mctrl.attach_bank<RamBank>(0x40000000, 64 * 1024 * 1024); // Main memory
 
    // Amba PNP area
    mctrl.attach_bank<RomBank<64 * 1024>>(0xffff0000);
    mctrl.attach_bank<RomBank<4 * 1024>>(0x800ff000);
    amba_ahb_pnp_setup(mctrl);
    amba_apb_pnp_setup(mctrl);

    IRQMP intc(num_cpus_requested);
    mctrl.attach_bank<APBCTRL>(0x80000000, mctrl ,intc);
    auto& apbctrl= reinterpret_cast<APBCTRL&>(*mctrl.find_bank(0x80000000));
    
    mctrl.debug_list_banks();

    // Find end of ram so we can set the stack pointers correctly when we reset the CPUs
    auto end_of_ram = mctrl.find_bank(0x40000000)->get_end_exclusive();

    // Get the devices we need to interact with
    auto& uart = apbctrl.GetUART();
    
    // Set up timer
    auto& timer = apbctrl.GetTimer();
    timer.set_LEON_smp_state();

    // Read the ELF and get the entry point, then reset all cpus.
    u32 entry_va = 0x0;
    std::cout << "** Reading ELF..\n"; 
    u32 word_count = ReadElf(fname, mctrl, entry_va, false, std::cout); 
    std::cout << "** Read " << word_count << " bytes of image, entry point 0x" << std::hex << entry_va << std::dec << ". Resetting CPU(s).\n";
        
    // Create the cpus
    config.num_cpus = num_cpus_requested;
    std::vector<std::unique_ptr<CPU>> cpus{};
    g_cpus = &cpus;
    
    // Set up gdb stub
    auto gdb_stub = std::make_unique<GdbStub>(cpus);  
    
    for(unsigned int i = 0; i < config.num_cpus; ++i) {
        std::cout << "Creating CPU, id=" << i << "\n";
        auto& cpu = cpus.emplace_back(std::make_unique<CPU>(mctrl, intc, i, std::cout));
                
        cpu->reset(entry_va);


        cpu->enable_power_down(true);
        

        intc.set_cpu_ptr(cpu.get(), i);

        
        // OS boot process step 1: Set stack pointer to end of ram
        cpu->write_reg((u32)end_of_ram - 0x180, OUTREG6); // Write stack pointer
        cpu->write_reg((u32)end_of_ram, INREG6); // Write frame pointer
        
        if(debug_server)
            cpu->set_gdb_stub(gdb_stub.get());

    }

    debug_set_active_mmu(&(cpus[0]->get_mmu()));

    
    print_config(config);

    
    std::cout << "Creating bus clock\n";
    auto bus = std::make_unique<BusClock>(intc, timer, uart);
    
    bus->setFrequency(config.system_freq_hz);
    timer.set_system_freq(config.system_freq_hz);
   
    std::cout << "Creating " << (int)config.num_cpus << " cpu threads\n";
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < config.num_cpus; ++i) {
        threads.emplace_back(cpu_thread, std::ref(*cpus[i]));
    }

    

    // Ok, start it up...
    std::cout << "** Starting the machine..\n";

    // Start the gdb stub
    if(debug_server) {
        gdb_stub->start(debug_port);
    }

    // Start bus timer
    bus->start();
    
    // We start the first cpu, everything else will be handled from there
    cpus[0]->wakeup();
    
    // Wait here until all CPUs have exited
    for (auto& t : threads) {
        t.join();
    }
    
    bus->stop();
    auto stats = bus->getStats();
    std::cout << stats << std::endl;

    DebugStopController::Global()->dump_stderr();

    #ifdef PROFILE_MEM_ACCESS
    mctrl.print_profile();
    #endif

    #ifdef PROFILE_LOCKS
    dump_ram_mutex_profile(cpus);
    #endif

    intc.dump_state();

    std::cout << "** Emulation complete.\n";

    DebugStopController::UninstallGlobal(&dbg);
    return 0;
    

}

