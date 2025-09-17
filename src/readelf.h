#ifndef _readelf_h_
#define _readelf_h_

#include <elf.h>
#include <string>
#include <iostream>

#include "sparcv8/MMU.h"

u32 ReadElf(const std::string& fname, MMU& mmu, u32& entry_va, bool verbose = false, std::ostream& os = std::cout); 









#endif
