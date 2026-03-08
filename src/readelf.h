// SPDX-License-Identifier: MIT
#pragma once

#include <elf.h>
#include <string>
#include <iostream>

#include "peripherals/MCTRL.h"

u32 ReadElf(const std::string& fname, MCtrl& mctrl, u32& entry_va, bool verbose = false, std::ostream& os = std::cout); 









