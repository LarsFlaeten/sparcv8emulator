// Implementeation of a Leon SMP system

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
#include "mutexprofiler.hpp"

#include "cv_log.hpp"

#include "gdb/DebugStopController.hpp"

#include <pthread.h>
void set_thread_name(const std::string& name) {
    pthread_setname_np(pthread_self(), name.c_str());
}

struct ShutdownControl{
    /*cvlog::Mutex mtx{"SynxState.mtx"};
    cvlog::CV cv_start{"cv_start"};
    cvlog::CV cv_done{"cv_done"};
    bool start_tick = false;
    int active_cpus = 0;*/
    std::atomic<bool> shutdown_cpus = false;
};



// Its ugly, but we need a few globals for the signal handlers
// TODO: Make this better....
ShutdownControl* global_shutdown = nullptr;
GlobalIRQBarrier* g_irq_b = nullptr;
std::vector<std::unique_ptr<CPU>>* g_cpus = nullptr;

void signal_handler(int signal) 
{
    std::cout << "Signal " << signal << " caught..\n";
    switch(signal) {
        case(SIGINT):
            if(global_shutdown != nullptr) {
                if(auto dsc = DebugStopController::Global()) {
                    std::cout << "[MAIN] Releasing threads from stop-the-world\n";
                    dsc->resume();
                }
                std::cout << "[MAIN] Setting global state to shutdown\n";
                //std::unique_lock<std::mutex> lock(global_syncstate->mtx);
                //cvlog::UniqueLock lock(global_syncstate->mtx);
                global_shutdown->shutdown_cpus = true;
                
                // Set interrupt on all cpus, as the timer will not be able to
                // stop them anymore
                if(g_cpus) {
                    for(auto& cpu : *g_cpus)
                        cpu->interrupt();
                }
            }

            // Notify cpus hanging on the barrier:
            g_irq_b->cv_exit.notify_all();
            break;
        case(SIGTERM):
            if(global_shutdown != nullptr) {
                std::cout << "Setting global state to shutdown\n";
                //std::unique_lock<std::mutex> lock(global_syncstate->mtx);
                //cvlog::UniqueLock lock(global_syncstate->mtx);
                global_shutdown->shutdown_cpus = true;
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

struct thread_tick_summary {
    int target_instructions = 0;
    int actual_instructions = 0;
    TerminateReason reason = TerminateReason::UNIMPLEMENTED;
};

void cpu_thread(CPU& cpu, GlobalIRQBarrier& barrier, APBCTRL& apbctrl, ShutdownControl& sdc) {
    
    set_thread_name(std::format("cpu{}",cpu.get_cpu_id()));
    IRQMP& intc = apbctrl.GetIntc();
#ifdef PROFILE_CPU_THREAD
    std::vector<thread_tick_summary> tick_summaries;
#endif

    
    // We start in powerdown for all cpus:
    std::cout << "[CPU THREAD " << (int)cpu.get_cpu_id() << "] CPU " << (int)cpu.get_cpu_id() << " entering holding pattern.\n";
    cpu.enter_powerdown();
    std::cout << "[CPU THREAD " << (int)cpu.get_cpu_id() << "] CPU " << (int)cpu.get_cpu_id() << " Starting up.\n";

    RunSummary rs{};

    while(true) {

        if (sdc.shutdown_cpus)
            break;
        
        
        
        try {
            cpu.run(0, &rs);
#ifdef PROFILE_CPU_THREAD
            tick_summaries.emplace_back(0, rs.instr_count, rs.reason);
#endif
        } catch (const std::runtime_error& e) {
            std::cout << e.what() << "\n";
            //debug_dumpmemv(cpu.get_pc());
            debug_mmu_tables();
            debug_registerdump(cpu);
            throw std::runtime_error("Aborting...");
        }

        if((rs.reason == TerminateReason::NORMAL) 
            || (rs.reason == TerminateReason::RECV_SIGINT)
            || (rs.reason == TerminateReason::UNIMPLEMENTED))
            break;

        // If we came here, the cpu got a timer interrupt and exited its run loop
        // If global interrupt barrier is active, enter it:
        if (barrier.active) {
            std::unique_lock lock(barrier.mtx);

            barrier.arrived++;

            // If this CPU is the last:
            int active_cpus = intc.get_number_active_cpus();
            if (barrier.arrived == active_cpus) {
                barrier.release = true;
                barrier.active = false;
                barrier.arrived = 0;
                // Wake ALL waiting CPUs
                barrier.cv_exit.notify_all();
            } else {
                // Wait until release = true
                barrier.cv_exit.wait(lock, [&]{ return barrier.release || sdc.shutdown_cpus; });
            }
        } else {
            std::cerr << "Error - exited cpu run cycle without barrier active\n";
            print_run_summary(rs, cpu.get_cpu_id());
            cpu.get_intc_ref().dump_state();
            cpu.dump_regs();
            throw std::runtime_error("Should not ever be here");
        }


    }

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
        //std::cout << "Tick :" << ++i << "\t " << t.actual_instructions << "/" << t.target_instructions << " - " << rs_reason_str(t.reason) << "\n";
    }
    std::cout << "Max: " << max_tick_inst << "\n";
    std::cout << "Min: " << min_tick_inst << "\n";
 #endif   

}

static void wake_cpu_barrier(void* ctx) {
    auto* b = static_cast<GlobalIRQBarrier*>(ctx);
    if(b->arrived > 0) {
        b->release = true;
        b->active = false;
        b->arrived = 0;
    }
    b->cv_exit.notify_all();
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


        cpu->set_break_on_timer_interrupt(true);
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

    
    // Syncronization and threads
    ShutdownControl sdc{};
    global_shutdown = &sdc;
    
    std::cout << "Creating global irq barrier with " << (int)config.num_cpus << " cpus\n";
    GlobalIRQBarrier irq_barrier{};
    irq_barrier.total_cpus = config.num_cpus;
    g_irq_b = &irq_barrier;

    // Debugstop controller needs to be able to wake irq barrier
    dbg.add_wake_hook(&wake_cpu_barrier, &irq_barrier);

    std::cout << "Creating bus clock\n";
    auto bus = std::make_unique<BusClock>(intc, timer, uart, irq_barrier);
    
    // Set up frequencies:
    double f = 5'000'000.0;
    bus->setFrequency(f);
    timer.set_system_freq(f);
   
    std::cout << "Creating " << (int)config.num_cpus << " cpu threads\n";
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < config.num_cpus; ++i) {
        threads.emplace_back(cpu_thread, std::ref(*cpus[i]), std::ref(irq_barrier), std::ref(apbctrl), std::ref(sdc));
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

