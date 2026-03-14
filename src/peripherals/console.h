// SPDX-License-Identifier: MIT
#pragma once

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdexcept>

class Console
{
    struct termios orig_termios;
    uint8_t pending;
    bool has_pending;
    bool escape_pending;  // true when last byte was Ctrl+] (escape char)

public:
    Console() : pending(0), has_pending(false), escape_pending(false)
    {
        enable_raw_mode();

        // Set stdin to non-blocking
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    ~Console()
    {
        disable_raw_mode();
    }

    void disable_raw_mode()
    {
        if (isatty(STDIN_FILENO))
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    }

    void enable_raw_mode()
    {
        if (!isatty(STDIN_FILENO))
            return;  // not a terminal (e.g. test environment) — skip raw mode

        if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
            return;  // silently ignore if we can't read terminal attrs

        struct termios raw = orig_termios;

        // Disable echo, canonical mode, and signal generation.
        // ISIG must be off so Ctrl+C (0x03) passes to the guest instead of
        // raising SIGINT on the emulator process.  Use Ctrl+] X to quit.
        raw.c_lflag &= ~(ECHO | ICANON | ISIG);

        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;

        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);  // best-effort, ignore failure
    }

    virtual bool Hit()
    {
        if (has_pending) return true;

        uint8_t c;
        int r = ::read(STDIN_FILENO, &c, 1);
        if (r != 1)
            return false;

        if (escape_pending) {
            escape_pending = false;
            if (c == 'x' || c == 'X') {
                // Ctrl+A X → quit emulator
                disable_raw_mode();
                raise(SIGTERM);
                return false;
            }
            if (c == 0x1D) {
                // Ctrl+] Ctrl+] → send one Ctrl+] to guest
                pending = 0x1D;
                has_pending = true;
                return true;
            }
            // Unrecognised sequence — silently drop both bytes.
            return false;
        }

        if (c == 0x1D) {
            // Ctrl+] — start escape sequence, don't forward yet
            escape_pending = true;
            return false;
        }

        pending = c;
        has_pending = true;
        return true;
    }

    virtual uint8_t Getc()
    {
        has_pending = false;
        return pending;
    }

    // Non-blocking output
    virtual void Putc(unsigned c)
    {
        uint8_t ch = c & 0xFF;
        auto t = ::write(STDOUT_FILENO, &ch, 1); // never blocks
        (void)t;
    }
};

