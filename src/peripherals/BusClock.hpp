// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <vector>
#include <memory>
#include <cstdint>
#include <iostream>
#include <limits>

#include "Tickable.hpp"

#include "IRQMP.h"
#include "APBUART.h"
#include "GPTIMER.h"

#include "../cv_log.hpp"

#include "../gdb/DebugStopController.hpp"

class BusClock {
public:
    struct Stats {
        double avg_loop_time_ns = 0.0;
        double min_loop_time_ns = 0.0;
        double max_loop_time_ns = 0.0;
        double avg_wait_time_ns = 0.0;
        double measured_rate_hz = 0.0;

        uint64_t total_loops = 0;

        friend std::ostream& operator<<(std::ostream& os, const Stats& s);
    };

    BusClock(IRQMP& intc, GPTIMER& timer, APBUART& uart);
    ~BusClock();

    void start();
    void stop();
    void setFrequency(double freq_hz);
    double getFrequency() const;

    void addDevice(std::shared_ptr<Tickable> dev);
    void clearDevices();

    Stats getStats() const;

    void wait_for_tick(uint64_t& local_tick);
    uint64_t current_tick() const noexcept { return tick_count_.load(); }

private:
    void run();

    //mutable std::mutex mtx_;
    mutable cvlog::Mutex mtx_{"busclock mtx"};
    std::vector<std::shared_ptr<Tickable>> devices_;
    double clock_freq_hz_ = 10'000'000.0;

    std::atomic<bool> running_{false};
    std::thread thread_;

    std::atomic<uint64_t> tick_count_{0};
    //std::condition_variable cv_;
    cvlog::CV cv_{"busclock cv"};

    // performance stats
    //mutable std::mutex stats_mtx_;
    mutable cvlog::Mutex stats_mtx_{"busclock stats mtx"};
    Stats stats_;

    IRQMP& irqmp_;
    GPTIMER& timer_;
    APBUART& uart_;

    DebugStopController::WorkerToken wtoken;
    
};