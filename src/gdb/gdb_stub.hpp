// SPDX-License-Identifier: MIT
#pragma once
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <optional>

class CPU;
class MMU;


class GdbStub {
public:
    GdbStub(std::vector<std::unique_ptr<CPU>>& cpus_);
    ~GdbStub();

    GdbStub(const GdbStub&) = delete;
    GdbStub& operator=(const GdbStub&) = delete;

    void start(uint16_t port, bool wait_for_connection = true);
    void notify_breakpoint(int cpu_id, uint32_t pc);
    bool is_active() const { return active_; }

    bool has_breakpoint(uint32_t addr);
    void insert_breakpoint(uint32_t addr);
    void remove_breakpoint(uint32_t addr);
    uint32_t get_breakpoint_instruction(uint32_t addr);

    void print_breakpoints(std::ostream& os = std::cout) const {
        os << "Breakpoints (" << breakpoints_.size() << "):";
        if (breakpoints_.empty()) {
            os << " [none]\n";
            return;
        }

        for (size_t i = 0; i < breakpoints_.size(); ++i) {
            os << "\n  [" << i << "] " << breakpoints_[i];
        }
        os << "\n";
    }

private:
    void gdb_thread(uint16_t port);
    void handle_packet(const std::string& pkt);
    void send_packet(const std::string& data);
    std::optional<std::string> recv_packet();
    std::string read_registers(CPU& cpu);
    void write_registers(CPU& cpu, const std::vector<uint8_t>& data);
    void reinsert_breakpoint(uint32_t addr);


    std::string handle_memory_read(uint32_t addr, size_t len);
    bool handle_memory_write(uint32_t addr, size_t len, const std::string& data);

    uint32_t read_mem32(uint32_t vaddr);
    void write_mem32(uint32_t vaddr, uint32_t value);
    

    std::vector<std::unique_ptr<CPU>>& cpus_;
    
 public:
    std::mutex mtx_;
    std::condition_variable cv_;
 private:
    std::atomic<bool> active_ = false;
    std::atomic<bool> waiting_ = false;
    int current_cpu_ = 0;
    int client_fd_ = -1;
    int halted_cpu_ = -1;
    int server_fd_ = -1;
 
    std::thread thread_;
    std::atomic<bool> shutting_down_ = false;
    
    struct Breakpoint {
        uint32_t addr;
        uint32_t original_instr;

        friend std::ostream& operator<<(std::ostream& os, const Breakpoint& bp) {
            os << "Breakpoint { addr = 0x"
               << std::hex << std::setw(8) << std::setfill('0') << bp.addr
               << ", instr = 0x"
               << std::hex << std::setw(8) << std::setfill('0') << bp.original_instr
               << " }" << std::dec;
            return os;
        }
    };
    std::vector<Breakpoint> breakpoints_;
};