// SPDX-License-Identifier: MIT
// Implementeation of a Leon SMP system

// std
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <iomanip>

#include <signal.h>
#include <cstring>

#include "sparcv8/CPU.h"
#include "sparcv8/MMU.h"
#include "peripherals/Peripherals.h"
#include "peripherals/Amba.h"
#include "peripherals/APBCTRL.h"
#include "peripherals/BusClock.hpp"
#include "peripherals/ac97.hpp"
#include "peripherals/GRPCI2.hpp"

#include "readelf.h"
#include "debug.h"
#include "mutexprofiler.hpp"

#include "cv_log.hpp"
#include "peripherals/amba_pnp_dump.hpp"

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

#if defined(PERF_STATS)
void dump_perf_stats(std::vector<std::unique_ptr<CPU>>& cpus, MCtrl& mctrl) {
    // TLB stats (aggregate across all CPUs)
    uint64_t total_i_hits = 0, total_i_misses = 0;
    uint64_t total_d_hits = 0, total_d_misses = 0;
    uint64_t total_dc_hits = 0, total_dc_misses = 0;

    for (auto& cpu : cpus) {
        const auto& itlb = cpu->get_mmu().get_itlb().get_stats();
        const auto& dtlb = cpu->get_mmu().get_dtlb().get_stats();
        total_i_hits   += itlb.hits  .load(std::memory_order_relaxed);
        total_i_misses += itlb.misses.load(std::memory_order_relaxed);
        total_d_hits   += dtlb.hits  .load(std::memory_order_relaxed);
        total_d_misses += dtlb.misses.load(std::memory_order_relaxed);
        auto dc = cpu->get_mmu().get_dc_stats();
        total_dc_hits   += dc.hits  .load(std::memory_order_relaxed);
        total_dc_misses += dc.misses.load(std::memory_order_relaxed);
    }

    auto hit_rate = [](uint64_t h, uint64_t m) -> double {
        return (h + m) ? 100.0 * double(h) / double(h + m) : 0.0;
    };

    printf("[PERF] ITLB: hits=%llu misses=%llu hit_rate=%.2f%%\n",
        (unsigned long long)total_i_hits, (unsigned long long)total_i_misses,
        hit_rate(total_i_hits, total_i_misses));
    printf("[PERF] DTLB: hits=%llu misses=%llu hit_rate=%.2f%%\n",
        (unsigned long long)total_d_hits, (unsigned long long)total_d_misses,
        hit_rate(total_d_hits, total_d_misses));
    printf("[PERF] L0DC: hits=%llu misses=%llu hit_rate=%.2f%%\n",
        (unsigned long long)total_dc_hits, (unsigned long long)total_dc_misses,
        hit_rate(total_dc_hits, total_dc_misses));

    // RAM mutex contention
    auto* rb = dynamic_cast<RamBank*>(mctrl.get_bank(0x40000000));
    if (rb) {
        const auto& rs = rb->get_ram_stats();
        const uint64_t acq  = rs.lock_acquired .load(std::memory_order_relaxed);
        const uint64_t cont = rs.lock_contended.load(std::memory_order_relaxed);
        printf("[PERF] RAM lock: acquired=%llu contended=%llu contention_rate=%.2f%%\n",
            (unsigned long long)acq, (unsigned long long)cont,
            acq ? 100.0 * double(cont) / double(acq) : 0.0);
    }
}
#endif

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

// Scan one bank for the PROM bootargs string and patch it.
// append=false: replace entire cmdline with new_cmd.
// append=true:  append new_cmd (with a space separator) to existing cmdline.
// min_len: skip matches shorter than this (avoids kernel code false positives).
static bool try_patch_cmdline_in_bank(MCtrl& mctrl, uint32_t bank_base, const std::string& new_cmd,
                                      bool append, uint32_t min_len = 0) {
    auto* bank = mctrl.find_bank_or_null(bank_base);
    if (!bank || !bank->get_ptr()) return false;

    auto* mem = reinterpret_cast<uint8_t*>(bank->get_ptr());
    uint32_t sz = bank->get_size();

    const char needle[] = "console=";
    for (uint32_t i = 0; i + sizeof(needle) - 1 < sz; ++i) {
        if (memcmp(mem + i, needle, sizeof(needle) - 1) != 0) continue;

        // Walk forward to end of string, then count trailing zeros (buffer capacity).
        // Do NOT walk backward — bytes before 'i' belong to the containing PROM structure.
        uint32_t end = i;
        while (end < sz && mem[end] != '\0') ++end;
        uint32_t str_len = end - i;

        // Skip short strings — likely kernel code/format strings, not the real bootargs
        if (str_len < min_len) continue;

        uint32_t cap_end = end;
        uint32_t max_scan = std::min(sz, i + 512u);
        while (cap_end < max_scan && mem[cap_end] == '\0') ++cap_end;
        uint32_t capacity = cap_end - i;

        // Build the string to write
        std::string existing(reinterpret_cast<char*>(mem + i), str_len);
        std::cout << "[INFO] Found cmdline at 0x" << std::hex << (bank_base + i)
                  << ": \"" << existing << "\"\n" << std::dec;

        std::string write_cmd = append ? (existing + " " + new_cmd) : new_cmd;

        if (write_cmd.size() >= capacity) {
            std::cerr << "[WARN] cmdline too long (" << write_cmd.size()
                      << " >= " << capacity << " bytes capacity), truncating\n";
        }
        size_t write_len = std::min(write_cmd.size(), (size_t)(capacity - 1));
        memset(mem + i, 0, capacity);
        memcpy(mem + i, write_cmd.c_str(), write_len);

        std::cout << "[INFO] Patched cmdline at 0x" << std::hex << (bank_base + i)
                  << " (capacity=" << std::dec << capacity << "): \""
                  << write_cmd << "\"\n";
        return true;
    }
    return false;
}

static void patch_cmdline(MCtrl& mctrl, const std::string& new_cmd, bool append) {
    // Try ROM first — the Gaisler PROM bootargs live at 0xffff0000
    if (try_patch_cmdline_in_bank(mctrl, 0xffff0000, new_cmd, append)) return;
    // Fall back to main RAM, but require a long string to avoid kernel code false positives
    if (try_patch_cmdline_in_bank(mctrl, 0x40000000, new_cmd, append, 40)) return;
    std::cerr << "[WARN] kernel cmdline string not found, override ignored\n";
}

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
    setbuf(stdout, nullptr);  // unbuffered stdout so debug printfs are never lost
    const auto t_start = std::chrono::steady_clock::now();
    CVLOG_MUTE();
    
    DebugStopController dbg;
    DebugStopController::install_global(&dbg);

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
    std::string cmdline_override;
    bool cmdline_append = false;
    bool enable_vga = true;
    bool fullscreen_vga = false;
    bool dump_amba_pnp = false;
    while ((option = getopt(argc, argv, "i:n:g:ac:C:Vf")) != EOF) {
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
            case 'a':
                dump_amba_pnp = true;
                break;
            case 'c':
                cmdline_override = optarg;
                cmdline_append = false;
                break;
            case 'C':
                cmdline_override = optarg;
                cmdline_append = true;
                break;
            case 'V':
                enable_vga = false;
                break;
            case 'f':
                fullscreen_vga = true;
                break;
            default:
            std::cerr <<
                    "Usage: " << argv[0] << "[-i <filename>] \n"
                    "\n"
                     "    -i path/file: Path to the linux buildroot image\n"
                     "    -n [num]: Number of CPUs to emulate\n"
                     "    -g (port) Start gdb server on specified port\n"
                     "    -c <cmdline>: Replace kernel command line\n"
                     "    -C <extra>:   Append to kernel command line\n"
                     "    -V:           Disable GRVGA display and PS/2 keyboard\n"
                     "    -f:           Run GRVGA display in fullscreen\n"
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

    // Video RAM bank (5 MB at 0x20000000 — must be >= cmdline size:0x4b0000 = 4.69 MB)
    if (enable_vga)
        mctrl.attach_bank<RamBank>(0x20000000, 5 * 1024 * 1024);

    // Amba PNP area
    mctrl.attach_bank<RomBank<64 * 1024>>(0xffff0000);
    mctrl.attach_bank<RomBank<4 * 1024>>(0x800ff000);
    amba_ahb_pnp_setup(mctrl);
    amba_apb_pnp_setup(mctrl, enable_vga);
    if(dump_amba_pnp) {
        print_amba_pnp([&mctrl](uint32_t addr) -> uint32_t { return mctrl.read32(addr); });
    }


    IRQMP intc(num_cpus_requested);
    mctrl.attach_bank<APBCTRL>(0x80000000, mctrl, intc, enable_vga, fullscreen_vga);
    auto& apbctrl= reinterpret_cast<APBCTRL&>(*mctrl.find_bank(0x80000000));

    // PCI memory space: 2KB RAM for DMA descriptors/PCM data, then AC97 BAR windows
    mctrl.attach_bank<RamBank>(0x24000000, 0x800);
    auto ac97pci = std::make_unique<AC97Pci>(0, mctrl);
    mctrl.attach_bank<PCIMMIOBank>(*ac97pci, 0x24000800, 0x100); // NAM (BAR0)
    mctrl.attach_bank<PCIMMIOBank>(*ac97pci, 0x24000900, 0x100); // NABM (BAR1)
    ac97pci->set_phys_bases(0x24000800, 0x24000900); // must match PCIMMIOBank addresses above
    GRPCI2& grpci2 = apbctrl.get_grpci2();
    grpci2.attach_device(std::move(ac97pci));
    mctrl.attach_bank<PCIIOCfgArea>(0xfffa0000, grpci2); // PCI config space

    mctrl.debug_list_banks();

    // Find end of ram so we can set the stack pointers correctly when we reset the CPUs
    auto end_of_ram = mctrl.find_bank(0x40000000)->get_end_exclusive();

    // Get the devices we need to interact with
    auto& uart = apbctrl.get_uart();
    
    // Set up timer
    auto& timer = apbctrl.get_timer();
    timer.set_LEON_smp_state();

    // Read the ELF and get the entry point, then reset all cpus.
    u32 entry_va = 0x0;
    std::cout << "** Reading ELF..\n"; 
    u32 word_count = ReadElf(fname, mctrl, entry_va, false, std::cout); 
    std::cout << "** Read " << word_count << " bytes of image, entry point 0x" << std::hex << entry_va << std::dec << ". Resetting CPU(s).\n";

    if (!cmdline_override.empty())
        patch_cmdline(mctrl, cmdline_override, cmdline_append);
        
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
    bus->add_device(std::shared_ptr<Tickable>(&grpci2, [](Tickable*){}));

    bus->set_frequency(config.system_freq_hz);
    timer.set_system_freq(config.system_freq_hz);
   
    std::cout << "Creating " << (int)config.num_cpus << " cpu threads\n";
    std::cout.flush(); // flush before threads start to avoid interleaved output
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
    auto stats = bus->get_stats();
    std::cout << stats << std::endl;

    DebugStopController::Global()->dump_stderr();

    #ifdef PERF_STATS
    dump_perf_stats(cpus, mctrl);
    #endif

    #ifdef PROFILE_MEM_ACCESS
    mctrl.print_profile();
    #endif

    #ifdef PROFILE_LOCKS
    dump_ram_mutex_profile(cpus);
    #endif

#ifdef IRQMP_DEBUG
    intc.dump_state();
#endif

    const auto t_end = std::chrono::steady_clock::now();
    const double elapsed_s = std::chrono::duration<double>(t_end - t_start).count();
    std::cout << std::fixed << std::setprecision(2)
              << "** Wall time: " << elapsed_s << " s\n"
              << "** Emulation complete.\n";

    DebugStopController::uninstall_global(&dbg);
    return 0;
    

}

