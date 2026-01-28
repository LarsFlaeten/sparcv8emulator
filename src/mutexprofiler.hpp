#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <chrono>
#include <algorithm>

using u64 = uint64_t;

static inline u64 now_ns() {
    using namespace std::chrono;
    return (u64)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

struct MutexProfile {
    std::atomic<u64> acquisitions{0};
    std::atomic<u64> wait_ns{0};
    std::atomic<u64> hold_ns{0};
    std::atomic<u64> max_wait_ns{0};
    std::atomic<u64> max_hold_ns{0};

    void add_wait(u64 ns) {
        wait_ns.fetch_add(ns, std::memory_order_relaxed);
        // relaxed max update (fine for stats)
        u64 cur = max_wait_ns.load(std::memory_order_relaxed);
        while (ns > cur && !max_wait_ns.compare_exchange_weak(cur, ns, std::memory_order_relaxed)) {}
    }
    void add_hold(u64 ns) {
        hold_ns.fetch_add(ns, std::memory_order_relaxed);
        u64 cur = max_hold_ns.load(std::memory_order_relaxed);
        while (ns > cur && !max_hold_ns.compare_exchange_weak(cur, ns, std::memory_order_relaxed)) {}
    }
};

struct CpuMutexProfiles {
    MutexProfile ram;
    // add others: mmu, irqmp, etc.
};

struct ProfiledLock {
    std::mutex& mtx;
    MutexProfile& prof;

    std::unique_lock<std::mutex> lock;
    u64 t_acquire_start = 0;
    u64 t_acquired = 0;

    ProfiledLock(std::mutex& m, MutexProfile& p)
        : mtx(m), prof(p)
    {
        t_acquire_start = now_ns();
        lock = std::unique_lock<std::mutex>(mtx);      // blocks here
        t_acquired = now_ns();

        const u64 wait = t_acquired - t_acquire_start;
        prof.acquisitions.fetch_add(1, std::memory_order_relaxed);
        prof.add_wait(wait);
    }

    ~ProfiledLock() {
        const u64 t_release = now_ns();
        const u64 hold = t_release - t_acquired;
        prof.add_hold(hold);
        // lock releases automatically
    }

    ProfiledLock(const ProfiledLock&) = delete;
    ProfiledLock& operator=(const ProfiledLock&) = delete;
};


