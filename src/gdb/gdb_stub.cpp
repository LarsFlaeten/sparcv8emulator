// SPDX-License-Identifier: MIT
#include "gdb_stub.hpp"

#include "../sparcv8/CPU.h"  // Must define get_gpr(), get_fpr(), get_pc(), etc.
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <assert.h>

#include <pthread.h>

void set_thread_name(const char* name) {
    pthread_setname_np(pthread_self(), name);
}

#include "DebugStopController.hpp"

static std::string to_hex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; i++)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    return oss.str();
}

static std::vector<uint8_t> from_hex(const std::string& hex) {
    std::vector<uint8_t> res;
    for (size_t i = 0; i < hex.size(); i += 2)
        res.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));
    return res;
}

GdbStub::GdbStub(std::vector<std::unique_ptr<CPU>>& cpu_refs) : cpus_(cpu_refs) {
    //std::cout << "[GDBStub] constructed, this=" << this
    //      << ", &cv_=" << &cv_ << ", &mtx_=" << &mtx_ << std::endl;
}

GdbStub::~GdbStub() {
    //std::cout << "[GDB] GdbStub destructor\n";

    shutting_down_ = true;
    active_ = false;

    bool was_run= false;
    // Close sockets to unblock recv() calls
    if (client_fd_ != -1) {
        was_run = true;
        shutdown(client_fd_, SHUT_RDWR);
        close(client_fd_);
        client_fd_ = -1;
    }
    if (server_fd_ != -1) {
        was_run = true;
        close(server_fd_);
        server_fd_ = -1;
    }

    // Wake any CPU threads waiting_ on GDB
    {
        std::lock_guard<std::mutex> lock(mtx_);
        waiting_ = false;
        cv_.notify_all();
    }

    // Join GDB thread_
    if (thread_.joinable()) {
        thread_.join();
    }

    if(was_run) {
#ifdef GDB_DEBUG
        std::cout << "[GDB] Stub shut down.\n";
#endif
    }
}

void GdbStub::start(uint16_t port, bool wait_for_connection) {
    thread_ = std::thread(&GdbStub::gdb_thread, this, port);

    // Wait here until the client has connected:
    if(wait_for_connection) {
        std::unique_lock lock(mtx_);
        cv_.wait(lock, [&]() { return active_.load(std::memory_order_relaxed); });
#ifdef GDB_DEBUG
        std::cout << "[GDB] Client connected, lets go..\n";
#endif
    }
}

void GdbStub::gdb_thread(uint16_t port) {
    set_thread_name("gdb_stub");

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(server_fd_, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd_, 1);
    std::cout << "[GDB] Waiting for connection on port " << port << "\n";
    client_fd_ = accept(server_fd_, nullptr, nullptr);
#ifdef GDB_DEBUG
    std::cout << "[GDB] Connected.\n";
#endif
    active_ = true;

    while (!shutting_down_) {
        auto maybe_pkt = recv_packet();
        if (!maybe_pkt.has_value()) {
#ifdef GDB_DEBUG
            std::cout << "[GDB] Client disconnected or shutdown\n";
#endif
            break;
        }
        handle_packet(*maybe_pkt);
    }
}

std::string make_qfThreadInfo_reply(int total_num_cpus)
{
    if (total_num_cpus <= 0)
        return "l";   // no threads

    std::string r = "m";
    for (int i = 0; i < total_num_cpus; ++i) {
        if (i > 0)
            r += ",";
        r += std::to_string(i + 1);  // GDB thread_ IDs are 1-based
    }
    return r;
}

void GdbStub::handle_packet(const std::string& pkt) {
    
#ifdef GDB_DEBUG
    std::cout << "[GDB] Handle packet: [" << pkt << "]\n";
#endif
    //std::cout << "[GDB] handle_packet: this=" << this << ", &cv_=" << &cv_ << ", &mtx_=" << &mtx_ << std::endl;
    if (pkt == "?") {
        int tid = current_cpu_ + 1; // GDB threads are 1-based
        send_packet("T05thread:" + std::to_string(tid) + ";");
    }
    else if(pkt.size() == 1 && static_cast<unsigned char>(pkt[0]) == 0x03) {
        // Interrupt from the remote!
#ifdef GDB_DEBUG
        std::cout << "[GDB] Received Ctr+C from remote!\n";
        cpus_[0]->get_intc_ref().dump_state();
#endif
            
        if (auto* dsc = DebugStopController::Global()) {
            dsc->request_stop(DebugStopController::StopReason::CtrlC);
            dsc->wait_until_all_stopped();
#ifdef GDB_DEBUG
            std::cout << "[GDB] Stopped the world!\n";
#endif
            //send_packet("S02"); // TODO: Send T01??
            int tid = current_cpu_ + 1; // GDB threads are 1-based
            send_packet("T05thread:" + std::to_string(tid) + ";");
            return;
        }
    }
    else if (pkt.starts_with("qC")) {
        // Current thread_
        int tid = current_cpu_ + 1;
        send_packet("QC" + std::to_string(tid));
    }
    else if (pkt.starts_with("qfThreadInfo")) {
        send_packet(make_qfThreadInfo_reply((int)cpus_.size()));
    }
    else if (pkt.starts_with("qsThreadInfo")) {
        send_packet("l");
    }
    else if (pkt[0] == 'g') {
        if (current_cpu_ < 0 || current_cpu_ >= (int)cpus_.size()) {
            std::cerr << "[GDB] Invalid current_cpu_: " << current_cpu_ << "\n";
            send_packet("E01"); // Error
            return;
        } 
        auto reg_data = read_registers(*cpus_[current_cpu_]);
        assert(reg_data.size() == 576);
        send_packet(reg_data);
        
    }
    else if (pkt[0] == 'G') {
        if (current_cpu_ < 0 || current_cpu_ >= (int)cpus_.size()) {
            std::cerr << "[GDB] Invalid current_cpu_: " << current_cpu_ << "\n";
            send_packet("E01"); // Error
            return;
        } 
        auto data = from_hex(pkt.substr(1));
        write_registers(*cpus_[current_cpu_], data);
        send_packet("OK");
    }
    else if (pkt[0] == 'c') {
        // Do the replaced instruction:
        //std::cout << "[GDB] Continue received\n";
        //std::cout << "[GDB] waiting_ on mtx_ " << &mtx_ << "\n";
        //std::cout << "[GDB]        with cv_ " << &cv_ << "\n";
      
        // Release the cpu:  
        {
            std::lock_guard lock(mtx_);
            waiting_ = false;
        }
        
        cv_.notify_all();     

        if (auto* dsc = DebugStopController::Global())
            dsc->resume();   
    }
    else if (pkt.starts_with("Hg")) {
        int id = std::stoi(pkt.substr(2));
        if( id == 0) {
            send_packet("OK");
        } else if (id > 0 && id <= (int)cpus_.size()) {
            current_cpu_ = id-1;
            send_packet("OK");
        } else {
            send_packet("E01");
        }
    }
    else if (pkt.starts_with("T")) {
        int id = std::stoi(pkt.substr(1));
        // Is thread_ <tid> alive?
        if(id > 0 && id <= (int)cpus_.size())
            send_packet("OK");
        else
            send_packet("E01");
    }
    
    else if (pkt.starts_with("Hc")) {
        int id = std::stoi(pkt.substr(2));

        if (id == -1) {
            // Hc-1 means "continue all threads"
            // You can set a flag or just treat it as valid
            send_packet("OK");
        } else if (id >= 0 && id < (int)cpus_.size()) {
            // You could store a `continue_cpu` if needed
            send_packet("OK");
        } else {
            send_packet("E01");
        }
    }
    else if (pkt.starts_with("Z0,")) {
        
        uint32_t addr;
        sscanf(pkt.c_str(), "Z0,%x", &addr);
        try {
            insert_breakpoint(addr);
            send_packet("OK");
        } catch (const std::exception& e) {
            send_packet("E01");
        }

        
    }
    else if (pkt.starts_with("z0,")) {

        
        uint32_t addr;
        sscanf(pkt.c_str(), "z0,%x", &addr);
        try {
            remove_breakpoint(addr);
            send_packet("OK");
        } catch (const std::exception& e) {
            send_packet("E01");
        }

    }
    else if (pkt[0] == 'm') {
        uint32_t addr, len;
        sscanf(pkt.c_str(), "m%x,%x", &addr, &len);
        
        std::string packet{};
        try {
            packet = handle_memory_read(addr, len);
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            send_packet("E14");
            return;
        }
            
        send_packet(packet);
    } else if (pkt.starts_with("qL12")) {
        send_packet("");
    }
    else {
        send_packet("");
    }
}

void GdbStub::notify_breakpoint(int cpu_id, uint32_t pc) {
    //std::cout << "[GDB] notify_breakpoint: this=" << this << ", &cv_=" << &cv_ << ", &mtx_=" << &mtx_ << std::endl;

    // Halt all other threads/workers
    if (auto* dbg = DebugStopController::Global())
        dbg->request_stop(DebugStopController::StopReason::Breakpoint);

    {
        std::unique_lock lock(mtx_);
        halted_cpu_ = cpu_id;
        waiting_ = true;
        send_packet("T05thread:" + std::to_string(cpu_id+1) + ";");
            
        while (waiting_) {
            assert(lock.owns_lock());
            cv_.wait(lock);
        }
        
        halted_cpu_ = -1;
    }

    // Release all other workers/threads
    if (auto* dbg = DebugStopController::Global())
        dbg->resume();

}

std::optional<std::string> GdbStub::recv_packet() {
    char ch;
    std::string buf;

    /*
    // Wait for '$' — or return if disconnected
    while (read(client_fd_, &ch, 1) == 1 && ch != '$') {
        if (shutting_down_) return std::nullopt;
    }

    if (shutting_down_) return std::nullopt;

    // Read until '#' (start of checksum)
    while (read(client_fd_, &ch, 1) == 1 && ch != '#') {
        buf += ch;
    }
    */

    auto handle_ctrl_c = [&]() -> std::optional<std::string> {
        // Return syntetic pakket to dispatcher:
        return std::string(1, '\x03');
    };

    // Wait for '$' — or return if disconnected
    while (true) {
        ssize_t n = read(client_fd_, &ch, 1);
        if (n == 0) return std::nullopt;          // disconnected
        if (n < 0) {
            if (errno == EINTR) continue;
            return std::nullopt;
        }

        if (shutting_down_) return std::nullopt;

        if (static_cast<unsigned char>(ch) == 0x03) {
            return handle_ctrl_c();
        }

        if (ch == '$') break;

        // Ignore any other noise (e.g. '+' ACKs etc.)
    }

    // Read until '#' (start of checksum)
    while (true) {
        ssize_t n = read(client_fd_, &ch, 1);
        if (n == 0) return std::nullopt;
        if (n < 0) {
            if (errno == EINTR) continue;
            return std::nullopt;
        }

        if (shutting_down_) return std::nullopt;

        if (static_cast<unsigned char>(ch) == 0x03) {
            // Ctrl+C Can come at any time, drop ongoing packet
            return handle_ctrl_c();
        }

        if (ch == '#') break;
        buf += ch;
    }


    // Read checksum (2 bytes)
    char csum1, csum2;
    if (read(client_fd_, &csum1, 1) != 1 || read(client_fd_, &csum2, 1) != 1)
        return std::nullopt;

    // Always ACK the packet
    if (write(client_fd_, "+", 1) != 1)
        return std::nullopt;


    return buf;
}

void GdbStub::send_packet(const std::string& data) {
    uint8_t csum = 0;
    for (auto c : data) csum += c;
    std::ostringstream oss;
    oss << "$" << data << "#" << std::hex << std::setw(2) << std::setfill('0') << (int)csum;
    std::string pkt = oss.str();
    //std::cout << "[GDB] Send packet [" << pkt << "]\n";
    send(client_fd_, pkt.c_str(), pkt.size(), 0);
}

std::string GdbStub::read_registers(CPU& cpu) {
    std::vector<uint8_t> regs;

    for (int i = 0; i < 32; i++) {
        uint32_t val = 0;
        cpu.read_reg(i, &val);
        regs.insert(regs.end(), {(uint8_t)(val >> 24), (uint8_t)(val >> 16),
                        (uint8_t)(val >> 8), (uint8_t)(val)  });
    }
    for (int i = 0; i < 32; i++) {
        uint32_t val = 0; // NOt implemented
        regs.insert(regs.end(), {(uint8_t)(val >> 24), (uint8_t)(val >> 16),
                        (uint8_t)(val >> 8), (uint8_t)(val)  });
    }

    uint32_t special[] = {
        cpu.get_y_reg(),
        cpu.get_psr(),
        cpu.get_wim(),
        cpu.get_tbr(),
        cpu.get_pc(), 
        cpu.get_npc(),
        cpu.get_fsr(),
        0 // CSR
    };

    for (auto val : special)
        regs.insert(regs.end(), {(uint8_t)(val >> 24), (uint8_t)(val >> 16),
                        (uint8_t)(val >> 8), (uint8_t)(val)  });

    auto sz = regs.size();
    return to_hex(regs.data(), sz);
}

void GdbStub::write_registers(CPU& cpu, const std::vector<uint8_t>& data) {
    throw std::runtime_error("Not implemented");
    /*
    if (data.size() < 80 * 4) {
        std::cerr << "[GDB] register write: invalid size\n";
        return;
    }

    auto get32 = [&](int i) {
        return (uint32_t)data[i] |
               ((uint32_t)data[i + 1] << 8) |
               ((uint32_t)data[i + 2] << 16) |
               ((uint32_t)data[i + 3] << 24);
    };

    int idx = 0;

    // General-purpose registers r0–r31
    for (int i = 0; i < 32; ++i) {
        cpu->write_reg(get32(idx), i);
        idx += 4;
    }

    // Floating-point registers f0–f31
    for (int i = 0; i < 32; ++i) {
        //cpu->set_fpr(i, get32(idx)); // Not imlemented
        idx += 4;
    }

    // Special registers
    cpu->s(get32(idx));       idx += 4;
    cpu->set_npc(get32(idx));      idx += 4;
    cpu->set_psr(get32(idx));      idx += 4;
    cpu->set_fsr(get32(idx));      idx += 4;
    cpu->set_y_(get32(idx));        idx += 4;
    cpu->set_wim(get32(idx));      idx += 4;
    cpu->set_tbr(get32(idx));      idx += 4;

    // Done!
    */
}

bool GdbStub::has_breakpoint(uint32_t addr) {
    for (const auto& bp : breakpoints_)
        if (bp.addr == addr) return true;
    return false;
}

uint32_t GdbStub::get_breakpoint_instruction(uint32_t addr) {
    for (const auto& bp : breakpoints_)
        if (bp.addr == addr) return bp.original_instr;
    
    return 0;
}

void GdbStub::insert_breakpoint(uint32_t addr) {
    if (has_breakpoint(addr)) return;

    MMU& mmu = cpus_[current_cpu_]->get_mmu();

    try {
        uint32_t original = read_mem32(addr);
        breakpoints_.push_back({addr, original});
        write_mem32(addr, 0x91d02001); // ta 1
    } catch (const std::exception& e) {
        // Maybe addr is virtual, and MMU not set up yet?
        // Replace with known mappings from linux boot
        // 0x0-0xefffffff -> 0x0-0xefffffff -rwx--- [983040 pages]                                                                                                                                       
        // 0xf0000000-0xfbffffff -> 0x40000000-0x4bffffff crwx--- [49152 pages]                                                                                                                          
        // 0xffd00000-0xffd13fff -> 0x43fec000-0x43ffffff crwx--- [20 pages]  
        if ((!mmu.get_enabled()) && (addr >= 0xf0000000) && (addr < 0xfbffffff)) {
            auto phys_addr = addr - (0xf0000000 - 0x40000000);
            uint32_t original = read_mem32(phys_addr);
            breakpoints_.push_back({addr, original});
            write_mem32(phys_addr, 0x91d02001); // ta 1
        } else if ((!mmu.get_enabled()) && (addr >= 0xffd00000) && (addr < 0xffd13fff)) {
            auto phys_addr = addr - (0xffd00000 - 0x43fec000);
            uint32_t original = read_mem32(phys_addr);
            breakpoints_.push_back({addr, original});
            write_mem32(phys_addr, 0x91d02001); // ta 1
        } else {
            std::cerr << "Insert breakpoint: " << e.what() << '\n';
            throw std::runtime_error("Could not insert breakpoint at given adress."); 
        }
        
        
        
    }
}

void GdbStub::remove_breakpoint(uint32_t addr) {
    MMU& mmu = cpus_[current_cpu_]->get_mmu();

    for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
        if (it->addr == addr) {
            try
            {
                write_mem32(addr, it->original_instr);
                breakpoints_.erase(it);
                return;
            }
            catch(const std::exception& e)
            {
                // Maybe addr is virtual, and MMU not set up yet?
                // Replace with known mappings from linux boot
                // 0x0-0xefffffff -> 0x0-0xefffffff -rwx--- [983040 pages]                                                                                                                                       
                // 0xf0000000-0xfbffffff -> 0x40000000-0x4bffffff crwx--- [49152 pages]                                                                                                                          
                // 0xffd00000-0xffd13fff -> 0x43fec000-0x43ffffff crwx--- [20 pages]  
        
                if ((!mmu.get_enabled()) && (addr >= 0xf0000000) && (addr < 0xfbffffff)) {
                    auto phys_addr = addr - (0xf0000000 - 0x40000000);
                    write_mem32(phys_addr, it->original_instr);
                    breakpoints_.erase(it);
                } else if ((!mmu.get_enabled()) && (addr >= 0xffd00000) && (addr < 0xffd13fff)) {
                    auto phys_addr = addr - (0xffd00000 - 0x43fec000);
                    write_mem32(phys_addr, it->original_instr);
                    breakpoints_.erase(it);
                } else{
                    std::cerr << "Remove breakpoint: " << e.what() << '\n';
                    throw std::runtime_error("Could not insert breakpoint at given adress."); 
                }
            } 
        }
    }

    // Getting here mens no breakpoint was found..
    throw std::runtime_error("Could not insert breakpoint at given adress."); 
}

std::string GdbStub::handle_memory_read(uint32_t addr, size_t len) {
    std::ostringstream result;
    result << std::hex << std::setfill('0');

    uint32_t last_aligned_addr = ~0u;
    uint32_t cached_word = 0;

    for (size_t i = 0; i < len; ++i) {
        uint32_t aligned_addr = (addr + i) & ~3u;
        if (aligned_addr != last_aligned_addr) {
            cached_word = read_mem32(aligned_addr);
            last_aligned_addr = aligned_addr;
        }

        int byte_offset = (addr + i) & 3;
        uint8_t byte = (cached_word >> ((3 - byte_offset) * 8)) & 0xFF;

        result << std::setw(2) << static_cast<int>(byte);
    }

    return result.str();
}

bool GdbStub::handle_memory_write(uint32_t addr, size_t len, const std::string& data) {
    // Sanity check: hex string should be exactly len*2 characters
    if (data.size() != len * 2)
        return false;

    // Decode hex to byte array
    std::vector<uint8_t> bytes(len);
    for (size_t i = 0; i < len; ++i)
        bytes[i] = static_cast<uint8_t>(std::stoi(data.substr(i * 2, 2), nullptr, 16));

    uint32_t last_aligned_addr = ~0u;
    uint32_t cached_word = 0;

    for (size_t i = 0; i < len; ++i) {
        uint32_t target_addr  = addr + i;
        uint32_t aligned_addr = target_addr & ~3u;

        // Load the word if we crossed into a new 32‑bit region
        if (aligned_addr != last_aligned_addr) {
            cached_word = read_mem32(aligned_addr);  // already big‑endian
            last_aligned_addr = aligned_addr;
        }

        int byte_offset = target_addr & 3;
        int shift = (3 - byte_offset) * 8;       // big‑endian byte order within word

        uint32_t mask = 0xFFu << shift;
        cached_word = (cached_word & ~mask) | (uint32_t(bytes[i]) << shift);

        write_mem32(aligned_addr, cached_word);
    }

    return true;
}

// Try to read memory assuming vaddr is available
// If it fails, try the two normal mappings on linux boot
uint32_t GdbStub::read_mem32(uint32_t vaddr) {
    MMU& mmu = cpus_[current_cpu_]->get_mmu();

    uint32_t val = 0;
    if(mmu.MemAccess<intent_load, 4>(vaddr, val, CROSS_ENDIAN, true) == 0)
        return val;
    
    // Try standard mmu linux boot mappings
    if ((!mmu.get_enabled()) && (vaddr >= 0xf0000000) && (vaddr < 0xfbffffff)) {
        auto phys_addr = vaddr - (0xf0000000 - 0x40000000);
        if(mmu.MemAccess<intent_load, 4>(phys_addr, val, CROSS_ENDIAN, true) == 0)
            return val;
    }

    if ((!mmu.get_enabled()) && (vaddr >= 0xffd00000) && (vaddr < 0xffd13fff)) {
        auto phys_addr = vaddr - (0xffd00000 - 0x43fec000);
        if(mmu.MemAccess<intent_load, 4>(phys_addr, val, CROSS_ENDIAN, true) == 0)
            return val;
    }

    throw std::runtime_error("Could not read memory at address");// + vaddr);
}

void GdbStub::write_mem32(uint32_t vaddr, uint32_t value) {
    MMU& mmu = cpus_[current_cpu_]->get_mmu();

    if(mmu.MemAccess<intent_store, 4>(vaddr, value, CROSS_ENDIAN, true) == 0)
        return;
        
    // Try standard mmu linux boot mappings
    if ((!mmu.get_enabled()) && (vaddr >= 0xf0000000) && (vaddr < 0xfbffffff)) {
        auto phys_addr = vaddr - (0xf0000000 - 0x40000000);
        if(mmu.MemAccess<intent_store, 4>(phys_addr, value, CROSS_ENDIAN, true) == 0)
            return;
    }

    if ((!mmu.get_enabled()) && (vaddr >= 0xffd00000) && (vaddr < 0xffd13fff)) {
        auto phys_addr = vaddr - (0xffd00000 - 0x43fec000);
        if(mmu.MemAccess<intent_store, 4>(phys_addr, value, CROSS_ENDIAN, true) == 0)
            return;
    }

    throw std::runtime_error("Could not write memory at address"); // + vaddr);
}