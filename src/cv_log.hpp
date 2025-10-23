#pragma once
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstdio>
#include <thread>
#include <atomic>
#include <string>

#include <string_view>

//#define CVLOG_DISABLE
// ================================================================
// Global logging control
// ================================================================
// Define CVLOG_DISABLE before including this header to compile it out entirely
//   #define CVLOG_DISABLE
//
// Or toggle at runtime with:
//   CVLOG_MUTE();
//   CVLOG_UNMUTE();
// ================================================================

#ifndef CVLOG_DISABLE
namespace cvlog {

inline std::atomic<bool> enabled{true};
inline std::atomic<uint64_t> global_progress{0};

inline uint64_t tid() {
    return static_cast<uint64_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

inline uint64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

inline void log(const char* evt, std::string_view name) {
    if (!enabled.load(std::memory_order_relaxed))
        return;
    std::fprintf(stderr, "[%llu] %8llu µs %-14s %s\n",
        (unsigned long long)tid(),
        (unsigned long long)now_us(),
        evt,
        name.data());
    std::fflush(stderr);
    ++global_progress;
}

// ----------------------------------------------------------
// Wrappers
// ----------------------------------------------------------

struct Mutex {
    std::mutex m;
    std::string name;
    Mutex(std::string_view n) : name(n) {}
    void lock()    { log("lock", name);    m.lock(); }
    void unlock()  { log("unlock", name);  m.unlock(); }
    bool try_lock(){ log("try_lock", name);return m.try_lock(); }
};

struct UniqueLock {
    std::unique_lock<std::mutex> lk;
    std::string name;
    UniqueLock(Mutex& mm) : lk(mm.m), name(mm.name) { log("ulock", name); }
    void unlock() { log("ulock_un", name); lk.unlock(); }
    void lock() { 
        log("ulock_lock", name);
        lk.lock();
    }
};

struct LockGuard {
    Mutex& mm;
    std::string name;

    explicit LockGuard(Mutex& m_) : mm(m_), name(m_.name) {
        log("lguard_lock", name);
        mm.m.lock();
    }

    ~LockGuard() {
        log("lguard_unlk", name);
        mm.m.unlock();
    }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};

struct CV {
    std::condition_variable cv;
    std::string name;
    CV(std::string_view n) : name(n) {}

    template<typename Lock, typename Pred>
    void wait_for_debug(Lock& lk, Pred pred, std::chrono::milliseconds timeout=std::chrono::milliseconds(2000)) {
        using namespace std::chrono;
        auto start = steady_clock::now();
        log("wait", name);
        bool ok = cv.wait_for(lk, timeout, pred);
        auto dur = duration_cast<milliseconds>(steady_clock::now()-start).count();
        if (!ok) {
            std::fprintf(stderr, "[%llu] WAIT TIMEOUT on %s (%lld ms)\n",
                (unsigned long long)tid(), name.c_str(), (long long)dur);
        } else {
            log("wait_done", name);
        }
    }

    // Fully instrumented version of std::condition_variable::wait()
    template<typename Lock>
    void wait_debug(Lock& lk) {
        log("wait", name);
        cv.wait(lk);
        log("wait_done", name);
    }

    // Predicated wait (also matches std::cv API)
    template<typename Lock, typename Pred>
    void wait_debug(Lock& lk, Pred pred) {
        log("wait", name);
        cv.wait(lk, pred);
        log("wait_done", name);
    }

    void notify_one() { log("notify_one", name); cv.notify_one(); }
    void notify_all() { log("notify_all", name); cv.notify_all(); }
};

// ----------------------------------------------------------
// Mute/unmute macros
// ----------------------------------------------------------

#define CVLOG_MUTE()   do { cvlog::enabled.store(false, std::memory_order_relaxed); } while(0)
#define CVLOG_UNMUTE() do { cvlog::enabled.store(true,  std::memory_order_relaxed); } while(0)

} // namespace cvlog

#else  // CVLOG_DISABLE defined
// Completely empty definitions for zero-overhead build
namespace cvlog {
    struct Mutex { 
        std::mutex m;
        Mutex(const char*){}
        void lock(){m.lock();}
        void unlock(){m.unlock();}
        bool try_lock()
        {
            return m.try_lock();
        }
    };

    struct UniqueLock {
        std::unique_lock<std::mutex> lk;
        UniqueLock(Mutex& mm):lk(mm.m){}
        void unlock(){lk.unlock();}
        void lock(){lk.lock();}
    };

    struct LockGuard {
        std::lock_guard<std::mutex> g;
        explicit LockGuard(Mutex& mm) : g(mm.m) {}
        ~LockGuard() = default;
    };

    struct CV { 
        std::condition_variable cv;
        CV(const char*){}
        
        template<typename Lock, typename Pred>
        void wait_for_debug(Lock& lk, Pred pred, std::chrono::milliseconds timeout=std::chrono::milliseconds(2000)) 
        {
            cv.wait(lk, pred);
        } 

        template<typename Lock, typename Pred>
        void wait_debug(Lock& lk, Pred pred) 
        {
            cv.wait(lk, pred);
        } 

        template<typename Lock>
        void wait_debug(Lock& lk) 
        {
            cv.wait(lk);
        } 

        void notify_one(){cv.notify_one();}
        void notify_all(){cv.notify_all();}
    };
    
    inline void log(const char*,std::string_view){}
    inline std::atomic<uint64_t> global_progress{0};
}
#define CVLOG_MUTE()   do{}while(0)
#define CVLOG_UNMUTE() do{}while(0)
#endif