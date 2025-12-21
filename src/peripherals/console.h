#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdexcept>

class Console
{
    struct termios orig_termios;
    uint8_t pending;
    bool has_pending;

public:
    Console() : pending(0), has_pending(false)
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
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    }

    void enable_raw_mode()
    {
        if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
            throw std::runtime_error("Failed to get terminal attributes");

        struct termios raw = orig_termios;

        // Disable echo and canonical mode
        raw.c_lflag &= ~(ECHO | ICANON);

        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
            throw std::runtime_error("Failed to set raw mode");
    }

    virtual bool Hit()
    {
        if (has_pending) return true;

        uint8_t c;
        int r = ::read(STDIN_FILENO, &c, 1);

        if (r == 1) {
            pending = c;
            has_pending = true;
            return true;
        }

        return false;
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

#endif