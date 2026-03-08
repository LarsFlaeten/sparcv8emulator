// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <limits>
#include <cmath>
#include <iostream>

#include <vector>

#include "common.h"
#include "dis.h"


class LoopTimer {
public:
    using Clock = std::chrono::high_resolution_clock;

    void start() {
        startTime = Clock::now();
    }

    void stop(u32 instr) {
        auto endTime = Clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        updateStats(duration, instr);
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
        std::cout << "Max duration instructions (" << maxTimeInstructions.size() << "):\n";
        for(auto& m : maxTimeInstructions) {
            std::cout << disGetInstr(m.first) << ": " << m.second << " ns\n"; 
        }
        std::cout << "Min duration instructions (" << minTimeInstructions.size() << "):\n";
        for(auto& m : minTimeInstructions) {
            std::cout << disGetInstr(m.first) << ": " << m.second << " ns\n"; 
        }

    }

private:


    Clock::time_point startTime;
    std::vector<std::pair<u32, long long>> maxTimeInstructions{};
    std::vector<std::pair<u32, long long>> minTimeInstructions{};

    long long minDuration = std::numeric_limits<long long>::max();
    long long maxDuration = std::numeric_limits<long long>::min();

    int count = 0;
    double mean = 0.0;
    double M2 = 0.0;

    void updateStats(long long duration, u32 instr) {
        if (duration < minDuration) {
            minDuration = duration;
            minTimeInstructions.emplace_back(std::pair<u32, long long>{instr, duration});
        }
        if (duration > maxDuration) {
            maxDuration = duration;
            maxTimeInstructions.emplace_back(std::pair<u32, long long>{instr, duration});
        }
        ++count;
        double delta = duration - mean;
        mean += delta / count;
        M2 += delta * (duration - mean);
    }

    static std::string disGetInstr(u32 opcode) {
        u32 fmt_bits = (opcode >> FMTSTARTBIT) & LOBITS2;
        u32 op2      = (opcode >> OP2STARTBIT) & LOBITS3;
        u32 op3      = (opcode >> OP3STARTBIT) & LOBITS6;
        u32 rd       = (opcode >> RDSTARTBIT)  & LOBITS5;
        std::string instr;

        switch (fmt_bits) {
            case 1: 
                instr = format1_str;
                break;
            case 0:
                instr = format2_str[op2];
                if ((op2 == 4) && opcode == NOP) {
                    instr = "nop     ";
                } else if (op2 == 2) {
                    instr += cond_byte[rd & 0xf];
                }
                break;
            case 3:
                instr = format3_str[op3 + ((fmt_bits & 1) << 6)];
                break;
            case 2:
                instr = format3_str[op3 + ((fmt_bits & 1) << 6)];
                break;

            default:
                instr = "NOT FOUND";
        }
        
        return instr;
    }
};

