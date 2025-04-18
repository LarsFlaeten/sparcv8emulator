#pragma once

#include <chrono>
#include <limits>
#include <cmath>
#include <iostream>

class LoopTimer {
public:
    using Clock = std::chrono::high_resolution_clock;

    void start() {
        startTime = Clock::now();
    }

    void stop() {
        auto endTime = Clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        updateStats(duration);
    }

    void printStats() const {
        double stddev = (count > 1) ? std::sqrt(M2 / count) : 0.0;
		double hz = (mean > 0.0) ? 1e9 / mean : 0.0;
        std::cout << "Iterations: " << count << "\n";
        std::cout << "Min:       " << minDuration << " ns\n";
        std::cout << "Max:       " << maxDuration << " ns\n";
        std::cout << "Mean:      " << mean       << " ns\n";
        std::cout << "Mean rate: " << hz/1000000 << " MHz\n";
		std::cout << "Stddev:    " << stddev     << " ns\n";
    }

private:
    Clock::time_point startTime;

    long long minDuration = std::numeric_limits<long long>::max();
    long long maxDuration = std::numeric_limits<long long>::min();

    int count = 0;
    double mean = 0.0;
    double M2 = 0.0;

    void updateStats(long long duration) {
        if (duration < minDuration) minDuration = duration;
        if (duration > maxDuration) maxDuration = duration;

        ++count;
        double delta = duration - mean;
        mean += delta / count;
        M2 += delta * (duration - mean);
    }
};

