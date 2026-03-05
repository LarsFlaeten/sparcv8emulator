#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class DebugStopController {
public:

    using WakeFn = void(*)(void*);

    struct WakeHook {
        WakeFn fn;
        void*  ctx;
    };

    // --- Global optional install ---
    static void InstallGlobal(DebugStopController* c) {
        global_.store(c, std::memory_order_release);
    }
    static void UninstallGlobal(DebugStopController* c) {
        DebugStopController* expected = c;
        global_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }
    static DebugStopController* Global() {
        return global_.load(std::memory_order_acquire);
    }


    enum class StopReason : uint8_t {
        Breakpoint,
        SingleStep,
        CtrlC,
        Watchpoint,
        UserRequest,
        Fatal,
    };

    // Token each participating thread holds.
    struct WorkerToken {
        uint32_t id = 0;
        std::string name;
    };

    DebugStopController() = default;

    void add_wake_hook(WakeFn fn, void* ctx) {
        std::lock_guard lk(mtx_);
        wake_hooks_.push_back(WakeHook{fn, ctx});
    }

    // Register a worker thread that must participate in stop-the-world.
    // Call once per CPU thread + bus thread (and any other mutating thread).
    WorkerToken register_worker(std::string name) {
        std::lock_guard lk(mtx_);
        const uint32_t id = next_id_++;
        workers_[id] = WorkerState{.name = std::move(name)};
        expected_workers_ = static_cast<uint32_t>(workers_.size());
        return WorkerToken{id, workers_[id].name};
    }

    // Request a global stop. Safe to call from any thread (CPU/bus/debugger).
    // Returns true if this call transitioned Running->StopRequested.
    bool request_stop(StopReason reason) {
        StopState expected = StopState::Running;
        if (!state_.compare_exchange_strong(expected, StopState::StopRequested,
                                            std::memory_order_acq_rel)) {
            // already stopping/stopped
            reason_.store(static_cast<uint8_t>(reason), std::memory_order_release);
            return false;
        }

        reason_.store(static_cast<uint8_t>(reason), std::memory_order_release);

        std::vector<WakeHook> hooks_copy;

        // Wake sleepers so they can observe stop.
        {
            std::lock_guard lk(mtx_);
            stop_epoch_++;
            hooks_copy = wake_hooks_;     // copy under lock
        }

        // Wake *all* potential sleepers (outside lock!)
        for (auto& h : hooks_copy) {
            h.fn(h.ctx);
        }
        
        cv_.notify_all();
        return true;
    }

    // Resume execution.
    void resume() {
        {
            std::lock_guard lk(mtx_);
            // Clear per-worker "stopped" flags
            for (auto& [id, ws] : workers_) {
                ws.stopped = false;
            }
            stopped_count_ = 0;
            state_.store(StopState::Running, std::memory_order_release);
            stop_epoch_++;
        }
        cv_.notify_all();
    }

    bool is_stop_requested_or_stopped() const {
        auto s = state_.load(std::memory_order_acquire);
        return s != StopState::Running;
    }

    StopReason stop_reason() const {
        return static_cast<StopReason>(reason_.load(std::memory_order_acquire));
    }

    // Called frequently from worker threads (CPU execute loop, bus tick loop).
    // If stop is requested, the worker parks here until resumed.
    void checkpoint(const WorkerToken& tok) {
        if (!is_stop_requested_or_stopped()) return;

        std::unique_lock lk(mtx_);
        // Mark this worker stopped once per stop epoch.
        auto it = workers_.find(tok.id);
        if (it == workers_.end()) return; // not registered (or shutting down)
        WorkerState& ws = it->second;

        // If stop requested, transition to Stopped when first worker arrives
        // and count all workers that park.
        if (!ws.stopped) {
            ws.stopped = true;
            stopped_count_++;
            if (stopped_count_ == expected_workers_) {
                state_.store(StopState::Stopped, std::memory_order_release);
                cv_.notify_all(); // wake debugger waiting for all stopped
            }
        }

        const uint64_t my_epoch = stop_epoch_;
        cv_.wait(lk, [&] {
            // Wait until resume changes epoch and state becomes Running
            return state_.load(std::memory_order_acquire) == StopState::Running
                   && stop_epoch_ != my_epoch;
        });
    }

    // Debugger-side: wait until all workers have parked.
    // Use this before you answer GDB "Txx..." stop reply if you want strict coherence.
    void wait_until_all_stopped() {
        std::unique_lock lk(mtx_);
        cv_.wait(lk, [&] {
            return state_.load(std::memory_order_acquire) == StopState::Stopped
                   && stopped_count_ == expected_workers_;
        });
    }

    // A stop-aware wait helper for "CPU sleeps until started".
    // Use instead of plain cv.wait(...) so stop requests can wake the sleeping CPU.
    template <class Pred>
    void wait_for_or_stop(const WorkerToken& tok,
                          std::condition_variable& cv_user,
                          std::unique_lock<std::mutex>& lk_user,
                          Pred pred) {
        for (;;) {
            // First, respect global stop.
            lk_user.unlock();
            checkpoint(tok); // will return immediately if not stopping
            lk_user.lock();

            // Then, do the user's wait in small chunks, so stop can be observed.
            if (pred()) return;

            // Timed wait avoids “lost wakeups” wrt debug-stop if the user-cv isn't notified.
            cv_user.wait_for(lk_user, std::chrono::milliseconds(5));
        }
    }

private:
    enum class StopState : uint8_t { Running, StopRequested, Stopped };

    struct WorkerState {
        std::string name;
        bool stopped = false;
    };

    mutable std::mutex mtx_;
    std::condition_variable cv_;

    std::unordered_map<uint32_t, WorkerState> workers_;
    uint32_t next_id_ = 1;

    uint32_t expected_workers_ = 0;
    uint32_t stopped_count_ = 0;

    std::atomic<StopState> state_{StopState::Running};
    std::atomic<uint8_t> reason_{static_cast<uint8_t>(StopReason::UserRequest)};

    static inline std::atomic<DebugStopController*> global_{nullptr};

    // Used to break waits cleanly across resume/stop cycles.
    uint64_t stop_epoch_ = 0;

    std::vector<WakeHook> wake_hooks_;

public:

    static const char* StopStateToString(uint8_t s) {
        switch (static_cast<StopState>(s)) {
            case StopState::Running:       return "Running";
            case StopState::StopRequested: return "StopRequested";
            case StopState::Stopped:       return "Stopped";
        }
        return "Unknown";
    }

    static const char* StopReasonToString(StopReason r) {
        switch (r) {
            case StopReason::Breakpoint:  return "Breakpoint";
            case StopReason::SingleStep:  return "SingleStep";
            case StopReason::CtrlC:       return "CtrlC";
            case StopReason::Watchpoint:  return "Watchpoint";
            case StopReason::UserRequest: return "UserRequest";
            case StopReason::Fatal:       return "Fatal";
        }
        return "Unknown";
    }

    // Returnerer en tekst-dump (lett å bruke i logger/tester).
    std::string dump() const {
        std::ostringstream os;
        dump_to(os);
        return os.str();
    }

    // Skriver til ostream (lett å sende til logg-systemer).
    void dump_to(std::ostream& os) const {
        // Ta en konsistent snapshot under lock.
        std::unique_lock lk(mtx_);

        const auto s = state_.load(std::memory_order_acquire);
        const auto r = stop_reason(); // leser atomisk internt
        const uint64_t epoch = stop_epoch_;

        os << "DebugStopController@" << this
           << " global=" << (Global() == this ? "yes" : (Global() ? "other" : "null"))
           << "\n  state=" << StopStateToString(static_cast<uint8_t>(s))
           << " reason=" << StopReasonToString(r)
           << " epoch=" << epoch
           << "\n  expected_workers=" << expected_workers_
           << " stopped_count=" << stopped_count_
           << " workers.size=" << workers_.size()
           << "\n  workers:\n";

        // Stabil og “pen” rekkefølge: sorter ids.
        std::vector<uint32_t> ids;
        ids.reserve(workers_.size());
        for (auto& kv : workers_) ids.push_back(kv.first);
        std::sort(ids.begin(), ids.end());

        for (uint32_t id : ids) {
            const auto& ws = workers_.at(id);
            os << "    - id=" << id
               << " stopped=" << (ws.stopped ? "yes" : "no")
               << " name=\"" << ws.name << "\"\n";
        }
    }

    // Direkte til FILE* (praktisk i "call" fra GDB uten å måtte fange string).
    void dump_to(FILE* f) const {
        if (!f) return;
        std::ostringstream os;
        dump_to(os);
        std::fwrite(os.str().data(), 1, os.str().size(), f);
        std::fflush(f);
    }

    // Enda mer GDB-vennlig: alltid til stderr.
    void dump_stderr() const {
        dump_to(stderr);
    }
};
