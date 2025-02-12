#ifndef _APBUART_H_
#define _APBUART_H_

#include <termio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <cstdio>
#include <unistd.h>



#include "../sparcv8/CPU.h"


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


// UART lite, lite version
class APBUART
{
    bool enabled, ints_enabled, tx_emptied_int_pending, overrun;
    struct { u8 len, pos, fifo[8]; } in;
    // List of supported status and control bits in status REG
    enum { DREADY=1, TS_EMPTY=2, TFIFO_EMPTY=4, OVERRUN=16, RCNT_shift=26};
    enum { RE = 1, TE = 2, RI = 4, TI = 8, FA = (1<< 31) }; 
    u32 data;
    u32 status;
    u32 control;
    u32 scaler; // Use gaisler default of 12 bits

    Console console;
public:
    APBUART(): ints_enabled(false), overrun(false), in{0,0,{0}}, scaler(0xff) { }
    u32 Read(u32 offset)
    {
        // Read data reg
        u32 result = 0;

        switch(offset) { 
            case(0x0):
                if(in.len > 0) result = in.fifo[ (in.pos + 8 - in.len--) % 8 ];
                break;
            case(0x4): // Status reg
                if(in.len>0) result |= DREADY;
                result |= TS_EMPTY;
                result |= TFIFO_EMPTY; 
                if(overrun) {result |= OVERRUN; overrun = false;}
                result |= in.len << RCNT_shift; // RCNT
                break;
            case(0x8): // Control Reg
                if(enabled) result |= RE + TE;
                if(ints_enabled) result |= RI + TI;
                result |= FA; // FIFOs allways on
                break;
             case(0xC): // SCALER Reg
                result |= scaler;
                break;
             default:
                throw not_implemented_leon_exception("UART Read offset > 0xC");
        }
       return result;
    }
    void Write(u32 offset, u32 value)
    {
        switch(offset) {
            case(0x0):
                console.Putc(value);
                tx_emptied_int_pending = true; 
                break;
            case(0x8):
                ints_enabled = (value & (RI|TI));
                enabled = value & (RE|TE);
                break;
            case(0xc):
                scaler = 0;
                scaler = value & 0xfff;
                break;
            default:
                std::cerr << "APBUART write offset " << std::hex << offset << " -> " << value << "\n";
                throw not_implemented_leon_exception("Test - write UART other reg");
        }
    }
    bool CheckIRQ() // TX interrupt is an edge; RX interrupt is a level.
    {
        if(ints_enabled && (in.len || tx_emptied_int_pending))
            { tx_emptied_int_pending=false; return true; }
        return false;
    }
    void Input()
    {
        static unsigned counter = 0;
        if(!counter) counter = scaler & 0xfff; else { --counter; return; }
        if(!console.Hit()) return;
        if(in.len >= 8) { overrun = true; return; }
        in.fifo[ in.pos++ % 8 ] = console.Getc();
        ++in.len;
    }
};

#endif
