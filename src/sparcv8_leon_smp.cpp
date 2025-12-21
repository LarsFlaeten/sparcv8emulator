// Implementeation of a Leon SMP system
// No gdb stub support for multiple threads implemented yet

// std
#include <iostream>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include <signal.h>


#include "sparcv8/CPU.h"
#include "sparcv8/MMU.h"
#include "peripherals/Peripherals.h"
#include "peripherals/Amba.h"
#include "peripherals/APBCTRL.h"
#include "peripherals/BusClock.hpp"

#include "readelf.h"
#include "debug.h"

#include "cv_log.hpp"

#include <pthread.h>

void set_thread_name(const std::string& name) {
    pthread_setname_np(pthread_self(), name.c_str());
}

// Shared state
/*struct SyncState {
    std::mutex mtx;
    std::condition_variable cv_start;
    std::condition_variable cv_done;
    bool start_tick = false;
    int active_cpus = 0;
    std::atomic<bool> shutdown_cpus = false;
    std::atomic<bool> global_shutdown = false;
    
};*/

struct SyncState {
    cvlog::Mutex mtx{"SynxState.mtx"};
    cvlog::CV cv_start{"cv_start"};
    cvlog::CV cv_done{"cv_done"};
    bool start_tick = false;
    int active_cpus = 0;
    std::atomic<bool> shutdown_cpus = false;
    std::atomic<bool> global_shutdown = false;
};

SyncState* global_syncstate = nullptr;

void signal_handler(int signal) 
{
    std::cout << "Signal " << signal << " caught..\n";
    switch(signal) {
        case(SIGINT):
            if(global_syncstate != nullptr) {
                std::cout << "Setting global state to shutdown\n";
                //std::unique_lock<std::mutex> lock(global_syncstate->mtx);
                //cvlog::UniqueLock lock(global_syncstate->mtx);
                global_syncstate->shutdown_cpus = true;
                global_syncstate->global_shutdown = true;
            }
            //throw std::runtime_error("SIGINT");
            break;
        case(SIGTERM):
            if(global_syncstate != nullptr) {
                std::cout << "Setting global state to shutdown\n";
                //std::unique_lock<std::mutex> lock(global_syncstate->mtx);
                //cvlog::UniqueLock lock(global_syncstate->mtx);
                global_syncstate->shutdown_cpus = true;
                global_syncstate->global_shutdown = true;
            }
            //throw std::runtime_error("SIGINT");
            break;
        default:
            std::cout << "Unhandle signal.\n";
            break;       
    }
}

struct EmulatorConfig {
    unsigned int num_cpus = 8;
    double tickrate_hz = 1000.0;       // 1 ms tick
    double emulated_freq_hz = 10e6;    // 10 MHz CPU
    bool realtime_pacing = true;
};

void print_config(const EmulatorConfig& config) {
    std::cout << "\n=== Emulator Configuration ===\n";
    std::cout << std::fixed << std::setprecision(2);

    std::cout << "Number of CPUs:       " << config.num_cpus << "\n";
    std::cout << "CPU Frequency:        " << config.emulated_freq_hz << " Hz\n";
    std::cout << "Tick Rate:            " << config.tickrate_hz << " Hz\n";
    std::cout << "Instructions per Tick:"
              << " " << static_cast<int>(config.emulated_freq_hz / config.tickrate_hz) << "\n";
    std::cout << "Tick Duration:        "
              << (1e6 / config.tickrate_hz) << " µs\n";
    std::cout << "Real-time pacing:     "
              << (config.realtime_pacing ? "enabled" : "disabled") << "\n";
    std::cout << "==============================\n";
}

struct thread_tick_summary {
    int target_instructions = 0;
    int actual_instructions = 0;
    TerminateReason reason = TerminateReason::UNIMPLEMENTED;
};

void cpu_thread(CPU& cpu, SyncState& state, APBCTRL& apbctrl) {

#ifdef PROFILE_CPU_THREAD
    std::vector<thread_tick_summary> tick_summaries;
#endif

    set_thread_name(std::format("cpu{}",cpu.get_cpu_id()));

    while (true) {
    
        //std::unique_lock<std::mutex> lock(state.mtx);
        cvlog::UniqueLock lock(state.mtx);

        state.cv_start.wait_debug(lock.lk, [&] { return state.start_tick || state.shutdown_cpus; });

        if (state.shutdown_cpus)
            break;

        lock.unlock();

        // Run one tick worth of instructions
        // Get number of instructions from timer:
        auto& timer = apbctrl.GetTimer();
        auto srld = timer.read(0x4); // SRELOAD
        auto trld = timer.read(0x14); // TRLDVAL
        auto instructions_per_tick = srld * trld;
        RunSummary rs{};
        
        try {
            cpu.run(instructions_per_tick, &rs);
#ifdef PROFILE_CPU_THREAD
            tick_summaries.emplace_back(instructions_per_tick, rs.instr_count, rs.reason);
#endif
        } catch (const std::runtime_error& e) {
            std::cout << e.what() << "\n";
            //debug_dumpmemv(cpu.get_pc());
            debug_mmu_tables();
            debug_registerdump(cpu);
            throw std::runtime_error("Aborting...");
        }

        lock.lock();
        if (--state.active_cpus == 0)
            state.cv_done.notify_one();
    }
/*
    {
        std::lock_guard<std::mutex>(state.mtx);
        if (--state.active_cpus == 0)
            state.cv_done.notify_one();
    }
*/
#ifdef PROFILE_CPU_THREAD
    std::cout << "Stats from thread " << cpu.get_cpu_id() << ", " << tick_summaries.size() << " ticks\n";
    int max_tick_inst = tick_summaries[0].actual_instructions;
    int min_tick_inst = tick_summaries[0].actual_instructions;
    int i = 0;
    for(const auto& t : tick_summaries) {
        if(t.actual_instructions > max_tick_inst)
            max_tick_inst = t.actual_instructions;
        if(t.actual_instructions < min_tick_inst)
            min_tick_inst = t.actual_instructions;
        std::cout << "Tick :" << ++i << "\t " << t.actual_instructions << "/" << t.target_instructions << " - " << rs_reason_str(t.reason) << "\n";
    }
    std::cout << "Max: " << max_tick_inst << "\n";
    std::cout << "Min: " << min_tick_inst << "\n";
 #endif   

}


void main_loop(BusClock& clock, std::vector<std::unique_ptr<CPU>>& cpus, SyncState& state, APBCTRL& apbctrl) {
    //auto& intc = apbctrl.GetIntc();
    //auto& uart = apbctrl.GetUART();
    uint64_t local_tick = 0;
    
    
    while(true) {
        
        // We wait here until the next timer tick boundary
        clock.wait_for_tick(local_tick);

        //std::unique_lock<std::mutex> lock(state.mtx);
        cvlog::UniqueLock lock(state.mtx);
        if(state.global_shutdown)
            break;


        state.active_cpus = cpus.size();
        state.start_tick = true;

        // CPUs start here. They will run the number of cycles as specified in the timer regs
        //LOG("Main loop, start.notify_all. CV=%p MtxUsedForThisWait=%p", (void*)&state.cv_start, (void*)&state.mtx);
        state.cv_start.notify_all();


        
        // Wait until all CPUs finish their tick
        
        //LOG("Main loop, done.wait. CV=%p MtxUsedForThisWait=%p", (void*)&state.cv_done, (void*)&state.mtx);
        state.cv_done.wait_debug(lock.lk, [&] { return (state.active_cpus == 0); });

        state.start_tick = false;

        
    }

    // Shutdown
    //std::unique_lock<std::mutex> lock(state.mtx);
    cvlog::UniqueLock lock(state.mtx);
    state.shutdown_cpus = true;
    state.cv_start.notify_all();
}

int main(int argc, char **argv) {
    CVLOG_MUTE();
    
    
    //set_thread_name("main");
    // Set up handler for external SIGINTs
    struct sigaction act;
    act.sa_handler =  signal_handler;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
 
    EmulatorConfig config{};

    int    option;
    int    num_cpus_requested = 0;
    std::string fname = "/home/lars//workspace/gaisler-buildroot-2024.02-1.1/output/images/image.ram";
    
    while ((option = getopt(argc, argv, "i:n:")) != EOF) {
        switch(option) {
            case 'i':
                fname = optarg;
                break;
            case 'n':
                num_cpus_requested = (u32)strtol(optarg, NULL, 0);
            default:
            std::cerr << 
                    "Usage: " << argv[0] << "[-i <filename>] \n"
                    "\n"
                     "    -i path/file: Path to the linux buildroot image\n"
                     "    -n [num]: Number of CPUs to emulate\n" 
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
    auto end_of_ram = mctrl.find_bank(0x40000000)->get_limit();

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
    for(unsigned int i = 0; i < config.num_cpus; ++i) {
        std::cout << "Creating CPU, id=" << i << "\n";
        auto& cpu = cpus.emplace_back(std::make_unique<CPU>(mctrl, intc, std::cout));
        cpu->set_cpu_id(i);

        cpu->set_break_on_timer_interrupt(true);
        cpu->enable_power_down(true);
        // hack:
        intc.set_cpu_ptr(cpu.get());

        cpu->reset(entry_va);

        // OS boot process step 1: Set stack pointer to end of ram
        cpu->write_reg(end_of_ram - 0x180, OUTREG6); // Write stack pointer
        cpu->write_reg(end_of_ram, INREG6); // Write frame pointer
        
    }

    debug_set_active_mmu(&(cpus[0]->get_mmu()));

    
    print_config(config);

    

    auto bus = std::make_unique<BusClock>(intc, timer, uart);
   
    


    
    
    
    
    

    // Syncronization and threads
    SyncState state{};
    global_syncstate = &state;
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < config.num_cpus; ++i) {
        threads.emplace_back(cpu_thread, std::ref(*cpus[i]), std::ref(state), std::ref(apbctrl));
    }

    

    // Ok, start it up...
    std::cout << "** Starting the machine..\n";

    bus->start();

    main_loop(*bus, cpus, state, apbctrl);
    
    bus->stop();
    auto stats = bus->getStats();
    std::cout << stats << std::endl;

    
    for (auto& t : threads) {
        t.join();
    }
    

    std::cout << "** Emulation complete.\n";
    return 0;
    

}
