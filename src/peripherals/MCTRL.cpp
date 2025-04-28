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