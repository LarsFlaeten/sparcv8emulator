#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include <termio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>



class Console
{
    struct termio back;
    int pending;
    bool DEBUG_FORCED;
    public:
    Console() : pending(0), DEBUG_FORCED(false)
    {
        ioctl(0, TCGETA, &back);
        struct termio term = back;
        // Disable linebuffer and echoing
        term.c_lflag &= ~(ICANON|ECHO);
        term.c_cc[VMIN] = 0; // 0=no block, 1=do block
        if(ioctl(0, TCSETA, &term) < 0)
            fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    }
    ~Console()
    {
      if(ioctl(0, TCSETA, &back) < 0)
            fcntl(0, F_SETFL, fcntl(0, F_GETFL) &~ O_NONBLOCK);
    }
    bool Hit()
    {
        if(pending) return true;
        char c;
        int r = read(0, &c, 1);
        if(r > 0) { 
			pending = c; 
			return true; 
        }
        return false;
    }
    unsigned Getc() { int r = pending;
      if(r=='%')
      {
        DEBUG_FORCED = !DEBUG_FORCED;
        fprintf(stdout, "DEBUG SET TO %d\n", DEBUG_FORCED);
        fprintf(stderr, "DEBUG SET TO %d\n", DEBUG_FORCED);
      }
      pending = 0; return r; }
    void Putc(unsigned c) { if(DEBUG_FORCED) putchar(':'); putchar(c); fflush(stdout); }
};



#endif
