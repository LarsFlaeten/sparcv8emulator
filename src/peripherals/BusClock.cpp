#include <iomanip>   // for std::setw, std::setprecision

#include "BusClock.hpp"
using namespace std::chrono;

#include <pthread.h>

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
    using clock = high_resolution_clock;
    auto next = clock::now();

    double freq;
    {
        //std::lock_guard<std::mutex> lock(mtx_);
        cvlog::LockGuard lock(mtx_);
        freq = clock_freq_hz_;
    }

    const double tick_period_ns = 1e9 / freq;

    // performance tracking
    uint64_t loop_count = 0;
    auto last_measure = clock::now();
    double min_ns = std::numeric_limits<double>::max();
    double max_ns = 0.0;
    double sum_ns = 0.0;
    double wait_ns = 0.0;
    double sum_wait_ns = 0.0;

    while (running_) {
        auto start = clock::now();
        
        
        // Handle tick and check timer interrupt in one go    
        timer_.lock();
        if(timer_.tick_and_check_interrupt_unlocked(true)) {
            irqmp_.TriggerIRQ(8);
            {
                std::lock_guard lock(mtx_);
                tick_count_.fetch_add(1, std::memory_order_relaxed);
            }
            cv_.notify_all();
        }
        timer_.unlock();
        
        // 50,000,000 Hz / 5000 = 10,000 Hz  (10 kHz UART tick)
        uint32_t div = freq/10'000;
        
        static uint32_t uart_div = 0;
        if (++uart_div >= div) {  // every div bus-clock ticks
            uart_div = 0;

            uart_.tick_scheduled();   // RX polling + TX retrigger
            if (uart_.CheckIRQ())
                irqmp_.TriggerIRQ(4);
        }

        auto end = clock::now();
        double loop_time_ns = duration_cast<nanoseconds>(end - start).count();

        // stats collection
        sum_ns += loop_time_ns;
        if (loop_time_ns < min_ns) min_ns = loop_time_ns;
        if (loop_time_ns > max_ns) max_ns = loop_time_ns;
        loop_count += 1;

        auto elapsed = duration_cast<nanoseconds>(end - last_measure).count();
        if (elapsed >= 100'000'000) { // 100 ms window
            double avg_ns = sum_ns / (loop_count);
            double rate_hz = loop_count * 1e9 / elapsed;
            double avg_wait_ns = sum_wait_ns / (loop_count);

            {
                cvlog::LockGuard lock(stats_mtx_);
                stats_.avg_loop_time_ns = avg_ns;
                stats_.min_loop_time_ns = min_ns;
                stats_.max_loop_time_ns = max_ns;
                stats_.measured_rate_hz = rate_hz;
                stats_.avg_wait_time_ns = avg_wait_ns;
                stats_.total_loops += loop_count;
            }

            loop_count = 0;
            sum_ns = 0.0;
            min_ns = std::numeric_limits<double>::max();
            max_ns = 0.0;
            sum_wait_ns = 0.0;
            last_measure = end;
        }

        // precise pacing
        
        wait_ns = tick_period_ns - loop_time_ns;
        sum_wait_ns += wait_ns;
        next += nanoseconds((long)wait_ns);
        std::this_thread::sleep_until(next);
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