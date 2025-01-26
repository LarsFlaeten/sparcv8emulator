#ifndef _readelf_h_
#define _readelf_h_

#include <elf.h>
#include <string>

#include "sparcv8/CPU.h"

u32 ReadElf(const std::string& fname, CPU& cpu, u32& entry_va); 









#endif
