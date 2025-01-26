#include "debug.h"
#include "sparcv8/MMU.h"

#include <string>
#include <iomanip>

void debug_dumpmem(u32 pa) {
    for(int i = 0; i < 4; ++i) {
        std::cout << std::hex << "0x" << pa + 4*i*4;
        std::string strrep;
        for(int j = 0; j < 4; ++j) {
            u32 v = MMU::MemAccessBypassRead4(pa + 4*(i*4 + j), CROSS_ENDIAN);
            std::cout <<  "  " << std::setfill('0') << std::setw(8) << v;
            char a = v & 0xff;
            char b = (v & 0xff00) >> 8;
            char c = (v & 0xff0000) >> 16;
            char d = (v & 0xff000000) >> 24;
            if( a >= 33 && a <= 126) strrep.push_back(a); else strrep.push_back('.');
            if( b >= 33 && b <= 126) strrep.push_back(b); else strrep.push_back('.');
            if( c >= 33 && c <= 126) strrep.push_back(c); else strrep.push_back('.');
            if( d >= 33 && d <= 126) strrep.push_back(d); else strrep.push_back('.');
            strrep.push_back(' ');
        }
        std::cout << "  " << strrep << std::endl;
    }
}

void debug_dumpmemv(u32 va) {
    for(int i = 0; i < 4; ++i) {
        std::cout << std::hex << "0x" << va + 4*i*4;
        std::string strrep;
        for(int j = 0; j < 4; ++j) {
            u32 v;
            MMU::MemAccess(va + 4*(i*4 + j), v, CROSS_ENDIAN, true);
            std::cout <<  "  " << std::setfill('0') << std::setw(8) << v;
            char a = v & 0xff;
            char b = (v & 0xff00) >> 8;
            char c = (v & 0xff0000) >> 16;
            char d = (v & 0xff000000) >> 24;
            if( a >= 33 && a <= 126) strrep.push_back(a); else strrep.push_back('.');
            if( b >= 33 && b <= 126) strrep.push_back(b); else strrep.push_back('.');
            if( c >= 33 && c <= 126) strrep.push_back(c); else strrep.push_back('.');
            if( d >= 33 && d <= 126) strrep.push_back(d); else strrep.push_back('.');
            strrep.push_back(' ');
        }
        std::cout << "  " << strrep << std::endl;
    }
}


void debug_registerdump(CPU& cpu) {

    cpu.RegisterDump(true);
    std::cout << "\n";
    std::cout << "   psr: " << std::hex << std::setw(8) << std::setfill('0') << cpu.GetPSR();
    std::cout << "   wim: " << std::hex << std::setw(8) << std::setfill('0') << cpu.GetWIM();
    std::cout << "   tbr: " << std::hex << std::setw(8) << std::setfill('0') << cpu.GetTBR();
    std::cout << "   y: " << std::hex << std::setw(8) << std::setfill('0') << cpu.GetY() << "\n";

}
