#include <stdio.h>
#include <signal.h>
#include <unistd.h>

void signal_handler(int signal)
{
        printf("Signal %d caught\n", signal);
}

int main(void)
{
        struct sigaction act;
        int ret = 0;

        act.sa_handler =  signal_handler;
        sigaction(SIGCONT, &act, NULL);
        sigaction(SIGINT, &act, NULL);
        ret = pause();
        if (-1 == ret)
                printf("Process exited\n");




        return 0;
}
