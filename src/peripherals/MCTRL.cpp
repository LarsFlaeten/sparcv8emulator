#include "MCTRL.h"

#ifdef __GNUG__
#include <cxxabi.h>
#include <memory>
    std::string demangle(const char* name) {
        int status = 0;
        char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
        std::string result = (status == 0 && demangled != nullptr) ? demangled : name;
        std::free(demangled);
        return result;
    }
#else
    std::string demangle(const char* name) {
        return name;
    }
#endif

void print_mctrl_profile(const MctrlProfile& p)
{
    const uint64_t r_total =
        p.r8 + p.r16 + p.r32 + p.r64;

    const uint64_t w_total =
        p.w8 + p.w16 + p.w32 + p.w64;

    const uint64_t total = r_total + w_total;

    auto pct = [](uint64_t v, uint64_t tot) -> double {
        return tot ? (100.0 * double(v) / double(tot)) : 0.0;
    };

    printf("MCTRL memory access profile\n");
    printf("---------------------------------------------\n");
    printf("Reads : %12lu (%.1f%% of total)\n",
        r_total, pct(r_total, total));
    printf("Writes: %12lu (%.1f%% of total)\n",
        w_total, pct(w_total, total));
    printf("Total : %12lu\n", total);
    printf("\n");

    printf("%-8s %14s %9s %9s\n", "Type", "Count", "% of R/W", "% of All");
    printf("-------------------------------------------------\n");

    printf("%-8s %14lu %8.2f%% %8.2f%%\n",
        "R8", p.r8, pct(p.r8, r_total), pct(p.r8, total));
    printf("%-8s %14lu %8.2f%% %8.2f%%\n",
        "R16", p.r16, pct(p.r16, r_total), pct(p.r16, total));
    printf("%-8s %14lu %8.2f%% %8.2f%%\n",
        "R32", p.r32, pct(p.r32, r_total), pct(p.r32, total));
    printf("%-8s %14lu %8.2f%% %8.2f%%\n",
        "R64", p.r64, pct(p.r64, r_total), pct(p.r64, total));

    printf("\n");

    printf("%-8s %14lu %8.2f%% %8.2f%%\n",
        "W8", p.w8, pct(p.w8, w_total), pct(p.w8, total));
    printf("%-8s %14lu %8.2f%% %8.2f%%\n",
        "W16", p.w16, pct(p.w16, w_total), pct(p.w16, total));
    printf("%-8s %14lu %8.2f%% %8.2f%%\n",
        "W32", p.w32, pct(p.w32, w_total), pct(p.w32, total));
    printf("%-8s %14lu %8.2f%% %8.2f%%\n",
        "W64", p.w64, pct(p.w64, w_total), pct(p.w64, total));
}