# SPARC V8 / LEON3 SMP Emulator

[![CI](https://github.com/LarsFlaeten/sparcv8emulator/actions/workflows/ci.yml/badge.svg)](https://github.com/YOUR_USERNAME/YOUR_REPO/actions/workflows/ci.yml)

A cycle-approximate emulator for the **SPARC V8** ISA targeting the **Gaisler LEON3** SMP processor family. It boots an unmodified Linux kernel (Gaisler Buildroot image) with up to 4 CPUs and includes a full GDB remote debugging stub.

## Features

- **Full SPARC V8 ISA** — integer, FPU, and LEON3 ASI extensions
- **Symmetric Multi-Processing** — up to 4 CPUs with shared memory and IRQMP broadcast interrupts
- **SRMMU** — Sparc Reference MMU with 16-entry ITLB + DTLB, L0 instruction/data translation caches
- **GDB remote debugging** — multi-CPU stop-the-world via TCP (port 1234), works with `sparc-linux-gdb`
- **LEON3 peripherals** — IRQMP, GPTIMER, APBUART, GRPCI2, MCTRL, SVGA framebuffer, AC97 audio
- **ELF loader** — loads Gaisler Buildroot ELF images directly

## Requirements

- CMake ≥ 3.22
- GCC or Clang with C++23 support
- SDL2 (for display/audio peripherals in tests)
- SPARC cross-compiler toolchain (for building test assembly, optional)

On Ubuntu/Debian:
```sh
sudo apt-get install cmake g++ libsdl2-dev
```

## Building

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Binaries are placed in `bin/`:

| Binary | Description |
|---|---|
| `bin/sparcv8_leon_smp` | SMP emulator (main) |
| `bin/sparcv8_runelf` | Single-CPU ELF runner |
| `bin/sparcv8_tests` | Unit test suite |

### Build options

| Option | Default | Description |
|---|---|---|
| `PERF_STATS` | OFF | Enable TLB hit/miss and RAM mutex contention counters |

```sh
cmake .. -DPERF_STATS=ON
```

## Running

You need a Gaisler Buildroot Linux ELF image (e.g. `vmlinux`). Place it in `image/` or pass the path explicitly.

```sh
# 4-CPU SMP boot
bin/sparcv8_leon_smp -n 4

# Specify image path
bin/sparcv8_leon_smp -i path/to/vmlinux -n 2

# Start with GDB server on port 1234
bin/sparcv8_leon_smp -n 4 -g 1234
```

The emulator prints Linux boot output to stdout. Press **Ctrl+C** to stop.

### Timing a boot

The `run_until.sh` helper stops the emulator when a pattern appears in the output:

```sh
./run_until.sh "Welcome to Buildroot" bin/sparcv8_leon_smp -n 2
```

## GDB Debugging

Start the emulator with `-g <port>`, then connect from a SPARC cross-GDB:

```sh
sparc-linux-gdb vmlinux
(gdb) target remote localhost:1234
(gdb) set scheduler-locking off
(gdb) continue
```

All CPUs halt simultaneously on breakpoints. `info threads` shows all CPU states.

## Running Tests

```sh
bin/sparcv8_tests
```

Or via CTest:

```sh
cd build && ctest --output-on-failure
```

> **Note:** A small number of tests (APBUART, APBCTRL, AC97) require a real TTY or audio device and are skipped in headless/CI environments.

## Architecture

```
src/
├── sparcv8/
│   ├── CPU.cpp / CPU.h               # SPARC V8 run loop, traps, interrupt handling
│   ├── CPU_instructions*.cpp         # Integer, LEON3, and extension instructions
│   ├── FPU_instructions.cpp          # IEEE 754 floating-point
│   └── MMU.h / MMU.cpp               # SRMMU + 16-entry TLB + L0 caches
├── peripherals/
│   ├── IRQMP.cpp / IRQMP.h           # Interrupt controller (PIFORCE, IPEND, PIMASK, broadcast)
│   ├── BusClock.cpp / BusClock.hpp   # System timer, fires IRQ8
│   ├── MCTRL.cpp / MCTRL.h           # Memory controller + RAM banks
│   ├── GPTIMER.h                     # General-purpose timers
│   ├── APBUART.h                     # APB UART
│   ├── GRPCI2.cpp / GRPCI2.hpp       # PCI bridge
│   ├── SVGA.h                        # SVGA framebuffer
│   └── ac97.cpp / ac97.hpp           # AC97 audio
├── gdb/
│   ├── gdb_stub.cpp / gdb_stub.hpp   # GDB remote protocol (TCP)
│   └── DebugStopController.hpp       # SMP stop-the-world coordination
├── sparcv8_leon_smp.cpp              # Main SMP entry point
└── sparcv8_runelf.cpp                # Single-CPU entry point
```

## Credits

This project started as a fork of [wyvernSemi/sparc](https://github.com/wyvernSemi/sparc)
by Simon Southwell. The following files derive from that project and retain their
GPL-3.0 license:

- `src/sparcv8/CPU.h` / `CPU.cpp` — SPARC V8 CPU core
- `src/dis.h` / `dis.cpp` — instruction disassembler
- `test/asm/` — assembly test suite

The `src/peripherals/gaisler/` headers are copyright Frontgrade Gaisler, GPL-2.0-or-later.

All other source files are original work and licensed under MIT.

## License

This project uses a dual license:

| Files | License |
|---|---|
| `src/sparcv8/CPU.*`, `src/dis.*`, `test/asm/` | [GPL-3.0-or-later](LICENSE) |
| `src/peripherals/gaisler/` | GPL-2.0-or-later (Frontgrade Gaisler) |
| Everything else | [MIT](LICENSE-MIT) |

Each source file carries an `SPDX-License-Identifier` header identifying its license.
