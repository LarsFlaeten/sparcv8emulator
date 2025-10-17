#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>


class Console
{
    struct termios orig_termios;
    int pending;
    public:
    Console() : pending(0)
    {
      enable_raw_mode();
    }
    ~Console()
    {
      disable_raw_mode();
    }

    void disable_raw_mode() {
      // Restore original terminal settings
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    }

    void enable_raw_mode() {
      // Get current terminal attributes
      if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        throw std::runtime_error("Failed to get terminal attributes");
      }
      
      struct termios raw = orig_termios;
      
      // Input modes - disable break, CR to NL, parity check, strip char, and XON/XOFF
      //raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
      //raw.c_iflag &= ~(INPCK | ISTRIP | IXON);
      
      // Output modes - disable post processing
      //raw.c_oflag &= ~(OPOST);
      
      // Control modes - set 8 bit chars
      //raw.c_cflag |= (CS8);
      
      // Local modes - disable echo, canonical mode, extended functions and signals
      raw.c_lflag &= ~(ECHO | ICANON); // | IEXTEN | ISIG);
      
      // Control chars - read returns after each byte, no timeout
      raw.c_cc[VMIN] = 0;
      raw.c_cc[VTIME] = 0;

      if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        throw std::runtime_error("Failed to set raw mode");
      }
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

    unsigned Getc() { 
      int r = pending;
      pending = 0; 
      return r;
    }
    
    void Putc(unsigned c) { putchar(c); fflush(stdout); }
    //void Putc(unsigned c) { if(DEBUG_FORCED) putchar(':'); putchar(c); fflush(stdout); }
};



#endif
