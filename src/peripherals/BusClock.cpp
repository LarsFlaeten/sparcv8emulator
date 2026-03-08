// SPDX-License-Identifier: MIT
#include <iomanip>   // for std::setw, std::setprecision

#include "BusClock.hpp"
using namespace std::chrono;

#include <pthread.h>
#include <cmath>

void set_thread_name(const std::string& name) {
    pthread_setname_np(pthread_self(), name.c_str());
}

std::ostream& operator<<(std::ostream& os, const BusClock::Stats& s)
{
    os << std::fixed << std::setprecision(2);
    auto r = s.measured_rate_hz;

    std::string rate = r > 1'000'000.0 ? (std::to_string(r/1'000'000) + " MHz")
         : ( r>1'000.0 ? (std::to_string(r/1'000) + " kHz") : std::to_string(r) + " Hz"); 
    os << "BusClock Stats:\n"
       << "  Rate:        " << rate << "\n"
       << "  Loop time:   avg " << s.avg_loop_time_ns
       << " ns  (min " << s.min_loop_time_ns
       << " ns, max " << s.max_loop_time_ns << " ns)\n"
       << "  Wait time:   avg " << s.avg_wait_time_ns << " ns)\n"
       
       << "  Total loops: " << s.total_loops << "\n";
    return os;
}

BusClock::BusClock(IRQMP& irqmp, GPTIMER& timer, APBUART& uart)
    : irqmp_(irqmp), timer_(timer), uart_(uart)
{
    // Register worker
    if (auto* dbg = DebugStopController::Global())
        wtoken = dbg->register_worker("BusClock");
}

BusClock::~BusClock() {
    stop();
}

void BusClock::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&BusClock::run, this);
}

void BusClock::stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable())
        thread_.join();
    

    cv_.notify_all(); // Wake any waiting cpus
}

void BusClock::setFrequency(double freq_hz) {
    //std::lock_guard<std::mutex> lock(mtx_);
    cvlog::LockGuard lock(mtx_);
    clock_freq_hz_ = freq_hz;
}

double BusClock::getFrequency() const {
    //std::lock_guard<std::mutex> lock(mtx_);
    cvlog::LockGuard lock(mtx_);
    return clock_freq_hz_;
}

void BusClock::addDevice(std::shared_ptr<Tickable> dev) {
    //std::lock_guard<std::mutex> lock(mtx_);
    cvlog::LockGuard lock(mtx_);
    devices_.push_back(std::move(dev));
}

void BusClock::clearDevices() {
    //std::lock_guard<std::mutex> lock(mtx_);
    cvlog::LockGuard lock(mtx_);    
    devices_.clear();
}

BusClock::Stats BusClock::getStats() const {
    //std::lock_guard<std::mutex> lock(stats_mtx_);
    cvlog::LockGuard lock(mtx_);
    return stats_;
}

void BusClock::run() {
    set_thread_name("bus_clock");
    using clock = std::chrono::steady_clock;
    
    double freq;
    {
        //std::lock_guard<std::mutex> lock(mtx_);
        cvlog::LockGuard lock(mtx_);
        freq = clock_freq_hz_;
    }

    std::cout << "[BUS CLOCK] Starting at " << std::fixed << std::setprecision(3)
              << freq << " hz\n";

    const double ticks_per_ns = freq / 1e9;

    // UART divider (compute once)
    const uint32_t uart_div_target = std::max<uint32_t>(1, (uint32_t)(freq / 10'000.0));
    uint32_t uart_div = 0;

    // host pacing quantum (tune)
    const auto host_quantum = 1ms;

    // batching clamp: max ticks per host wakeup — cap at 20ms worth of ticks
    const uint64_t max_due_ticks = (uint64_t)(freq * 0.020);

    // perf stats
    uint64_t loop_count = 0;
    auto last_measure = clock::now();
    double min_ns = std::numeric_limits<double>::max();
    double max_ns = 0.0;
    double sum_ns = 0.0;

    auto last = clock::now();

        while (running_) {
        auto iter_start = clock::now();

        if (auto* dbg = DebugStopController::Global()) dbg->checkpoint(wtoken);

        // compute due ticks from wall time
        auto now = clock::now();
        auto dt_ns = duration_cast<nanoseconds>(now - last).count();
        last = now;

        uint64_t due = (uint64_t)std::llround((double)dt_ns * ticks_per_ns);
        if (due > max_due_ticks) due = max_due_ticks;

        // Advance the timer in batches delimited by UART tick events,
        // using analytic prescaler math instead of a per-bus-tick loop.
        timer_.lock();
        uint64_t remaining = due;
        while (remaining > 0) {
            // Advance up to the next UART tick boundary.
            uint64_t to_uart = uart_div_target - uart_div;
            uint64_t batch   = (remaining < to_uart) ? remaining : to_uart;

            const bool fire_timer_irq = timer_.advance_unlocked(batch, true);
            uart_div  += (uint32_t)batch;
            remaining -= batch;

            if (fire_timer_irq) {
                timer_.unlock();
                irqmp_.trigger_irq(8);
                tick_count_.fetch_add(1, std::memory_order_relaxed);
                cv_.notify_all();
                timer_.lock();
            }

            if (uart_div >= uart_div_target) {
                uart_div = 0;
                timer_.unlock();
                uart_.tick_scheduled();
                if (uart_.CheckIRQ())
                    irqmp_.trigger_irq(4);
                timer_.lock();
            }
        }
        timer_.unlock();

        // stats measure "work" per host iteration
        auto iter_end = clock::now();
        double work_ns = duration_cast<nanoseconds>(iter_end - iter_start).count();
        sum_ns += work_ns;
        min_ns = std::min(min_ns, work_ns);
        max_ns = std::max(max_ns, work_ns);
        loop_count++;

        auto elapsed = duration_cast<nanoseconds>(iter_end - last_measure).count();
        if (elapsed >= 100'000'000) {
            double avg_ns = sum_ns / loop_count;
            double rate_hz = loop_count * 1e9 / elapsed;

            { cvlog::LockGuard lock(stats_mtx_);
              stats_.avg_loop_time_ns = avg_ns;
              stats_.min_loop_time_ns = min_ns;
              stats_.max_loop_time_ns = max_ns;
              stats_.measured_rate_hz = rate_hz;
              stats_.total_loops += loop_count;
            }

            loop_count = 0;
            sum_ns = 0.0;
            min_ns = std::numeric_limits<double>::max();
            max_ns = 0.0;
            last_measure = iter_end;
        }

        // yield to OS (coarse pacing)
        std::this_thread::sleep_for(host_quantum);

    }
}

void BusClock::wait_for_tick(uint64_t& local_tick) {
    //std::unique_lock<std::mutex> lock(mtx_);
    cvlog::UniqueLock lock(mtx_);
    //LOG("Main loop, wait for tick. CV=%p MtxUsedForThisWait=%p", (void*)&cv_, (void*)&mtx_);
        
    cv_.wait_debug(lock.lk, [&] {
        return tick_count_.load(std::memory_order_relaxed) > local_tick || !running_;
    });
    if (running_)
        local_tick = tick_count_.load(std::memory_order_relaxed);
}