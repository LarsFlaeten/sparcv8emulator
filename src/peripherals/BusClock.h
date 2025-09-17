// BusClock.hpp
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>



class BusClock {
public:
    using Clock      = std::chrono::steady_clock;
    using Duration   = Clock::duration;
    using TimePoint  = Clock::time_point;

    // Kalles av BusClock ved hver I/O-poll
    using IoPoller   = std::function<void(uint64_t tick_index, TimePoint now)>;

    // Kalles av BusClock når timer-IRQ skal "emitteres"
    // Du velger selv semantikk: edge (pulse) eller level (set/clear).
    using TimerIrqCb = std::function<void(uint64_t irq_index, TimePoint now)>;

    struct Config {
        // hvor ofte busklokka poller I/O
        double io_poll_hz = 1000.0;      // f.eks. 1 kHz
        // hvor ofte timer-IRQ skal avfyres
        double timer_hz   = 100.0;       // f.eks. 100 Hz (10 ms)
        // maks sovetid per iterasjon for å unngå lang blocking
        std::chrono::milliseconds max_sleep = std::chrono::milliseconds(5);
        // om vi tillater "catch-up" (ta igjen tapte poller) ved etterslep
        bool allow_catch_up = true;
        // valgfrie navn for logging
        std::string name = "BusClock";
        
    };

    


    struct Stats {
        std::atomic<uint64_t> poll_count{0};
        std::atomic<uint64_t> timer_count{0};
        std::atomic<uint64_t> sleep_calls{0};
        std::atomic<uint64_t> overruns{0};     // hvor mange ganger vi kom for sent
        std::atomic<int64_t>  worst_late_ns{0}; // mest negative margin (senest)
        std::atomic<int64_t>  best_early_ns{0}; // mest positive margin (tidligst)
    };

    explicit BusClock(Config cfg)
        : cfg_(cfg)
    {
        set_io_period(cfg_.io_poll_hz);
        set_timer_period(cfg_.timer_hz);
    }

    ~BusClock() { stop(); }

    void register_io_poller(IoPoller cb) {
        std::lock_guard<std::mutex> lk(cb_mu_);
        io_pollers_.push_back(std::move(cb));
    }

    void set_timer_irq_callback(TimerIrqCb cb) {
        std::lock_guard<std::mutex> lk(cb_mu_);
        timer_irq_cb_ = std::move(cb);
    }

    const Stats& stats() const { return stats_; }

    bool start() {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) return false;
        stop_flag_.store(false);
        worker_ = std::thread(&BusClock::run_loop_, this);
        return true;
    }

    void stop() {
        if (!running_.load()) return;
        stop_flag_.store(true);
        {
            std::lock_guard<std::mutex> lk(cv_mu_);
            cv_.notify_all();
        }
        if (worker_.joinable()) worker_.join();
        running_.store(false);
    }

    void print_summary() const {
        auto worst_late  = stats_.worst_late_ns.load();
        auto best_early  = stats_.best_early_ns.load();
        std::cout << "\n[" << cfg_.name << " summary]\n"
                  << "  polls:        " << stats_.poll_count.load()  << "\n"
                  << "  timer_irqs:   " << stats_.timer_count.load() << "\n"
                  << "  sleep_calls:  " << stats_.sleep_calls.load() << "\n"
                  << "  overruns:     " << stats_.overruns.load()    << "\n"
                  << "  worst_late:   " << worst_late << " ns\n"
                  << "  best_early:   " << best_early << " ns\n";
    }

    // Oppdater frekvenser dynamisk (trådsikkert nok for vanlig bruk)
    void update_io_poll_hz(double hz) {
        std::lock_guard<std::mutex> lk(cv_mu_);
        set_io_period(hz);
        cv_.notify_all();
    }
    void update_timer_hz(double hz) {
        std::lock_guard<std::mutex> lk(cv_mu_);
        set_timer_period(hz);
        cv_.notify_all();
    }

private:
    void set_io_period(double hz) {
        io_period_ = (hz > 0.0) ? std::chrono::duration_cast<Duration>(std::chrono::duration<double>(1.0 / hz))
                                : Duration::zero();
    }
    void set_timer_period(double hz) {
        timer_period_ = (hz > 0.0) ? std::chrono::duration_cast<Duration>(std::chrono::duration<double>(1.0 / hz))
                                   : Duration::zero();
    }

    static int64_t to_ns(Duration d) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
    }

    void run_loop_() {
        using namespace std::chrono;

        const TimePoint t0 = Clock::now();
        uint64_t poll_idx  = 0;
        uint64_t timer_idx = 0;

        TimePoint next_poll  = (io_period_   != Duration::zero()) ? (t0 + io_period_)   : TimePoint::max();
        TimePoint next_timer = (timer_period_!= Duration::zero()) ? (t0 + timer_period_): TimePoint::max();

        while (!stop_flag_.load(std::memory_order_relaxed)) {
            // Finn neste deadline (poll vs timer)
            TimePoint next_deadline = std::min(next_poll, next_timer);
            if (next_deadline == TimePoint::max()) {
                // Ingenting å gjøre; sov lett, men avbrytbar.
                std::unique_lock<std::mutex> lk(cv_mu_);
                stats_.sleep_calls.fetch_add(1, std::memory_order_relaxed);
                cv_.wait_for(lk, cfg_.max_sleep, [&]{ return stop_flag_.load(); });
                continue;
            }

            // Sov til neste_deadline (men ikke lenger enn max_sleep, så vi er responsive)
            {
                std::unique_lock<std::mutex> lk(cv_mu_);
                stats_.sleep_calls.fetch_add(1, std::memory_order_relaxed);
                cv_.wait_until(lk, std::min(next_deadline, Clock::now() + cfg_.max_sleep), [&]{ return stop_flag_.load(); });
            }
            if (stop_flag_.load()) break;

            auto now = Clock::now();

            // Håndter I/O-poll, muligens ta igjen flere hvis vi ligger bak og catch_up er på
            if (now >= next_poll && io_period_ != Duration::zero()) {
                size_t polls_done = 0;
                do {
                    do_io_poll_(poll_idx, now);
                    ++poll_idx;
                    ++polls_done;
                    next_poll += io_period_;
                } while (cfg_.allow_catch_up && now >= next_poll);

                // stats/jitter for siste poll-slot
                auto margin = next_poll - now; // >0: vi er tidlig, <0: sent
                record_margin_(margin);
                if (polls_done > 1) stats_.overruns.fetch_add(polls_done - 1, std::memory_order_relaxed);
            }

            // Håndter timer-IRQ, med samme catch-up-logikk
            if (now >= next_timer && timer_period_ != Duration::zero()) {
                size_t timers_done = 0;
                do {
                    emit_timer_irq_(timer_idx, now);
                    ++timer_idx;
                    ++timers_done;
                    next_timer += timer_period_;
                } while (cfg_.allow_catch_up && now >= next_timer);

                auto margin = next_timer - now;
                record_margin_(margin);
                if (timers_done > 1) stats_.overruns.fetch_add(timers_done - 1, std::memory_order_relaxed);
            }
        }
    }

    void record_margin_(Duration margin) {
        // positive => tidlig, negative => sen
        auto ns = to_ns(margin);
        // Oppdater best_early (maks positiv), worst_late (min negativ)
        // (ikke atomisk CAS-loop for enkelhet; racy men tilstrekkelig for statistikk)
        if (ns > 0) {
            auto cur = stats_.best_early_ns.load();
            if (ns > cur) stats_.best_early_ns.store(ns);
        } else {
            auto cur = stats_.worst_late_ns.load();
            if (ns < cur) stats_.worst_late_ns.store(ns);
        }
    }

    void do_io_poll_(uint64_t tick_index, TimePoint now) {
        // Kall alle registrerte pollere
        std::vector<IoPoller> local;
        {
            std::lock_guard<std::mutex> lk(cb_mu_);
            local = io_pollers_; // kopi for å minimere hold av lås
        }
        for (auto& cb : local) {
            // Hver poller bør være lettvekts og non-blocking
            cb(tick_index, now);
        }
        stats_.poll_count.fetch_add(1, std::memory_order_relaxed);
    }

    void emit_timer_irq_(uint64_t irq_index, TimePoint now) {
        TimerIrqCb cb;
        {
            std::lock_guard<std::mutex> lk(cb_mu_);
            cb = timer_irq_cb_;
        }
        if (cb) cb(irq_index, now);
        stats_.timer_count.fetch_add(1, std::memory_order_relaxed);
    }

private:
    Config cfg_;
    
    Duration io_period_{};
    Duration timer_period_{};

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_flag_{false};
    std::thread worker_;

    mutable std::mutex cb_mu_;
    std::vector<IoPoller> io_pollers_;
    TimerIrqCb timer_irq_cb_;

    mutable std::mutex cv_mu_;
    std::condition_variable cv_;

    Stats stats_;
};

std::ostream& operator<<(std::ostream& os, const BusClock::Config& cfg) {
        using namespace std::chrono;

        os << "[BusClock::Config \"" << cfg.name << "\"]\n"
        << "  io_poll_hz    : " << std::fixed << std::setprecision(2) << cfg.io_poll_hz << " Hz\n"
        << "  timer_hz      : " << std::fixed << std::setprecision(2) << cfg.timer_hz   << " Hz\n";

        auto us = duration_cast<microseconds>(cfg.max_sleep).count();
        os << "  max_sleep     : " << us << " us\n";

        os << "  allow_catch_up: " << (cfg.allow_catch_up ? "true" : "false") << "\n";

        return os;
    }