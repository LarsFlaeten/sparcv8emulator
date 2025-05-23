#ifndef _TEST_H_
#define _TEST_H_

// Use these macros in c-code main() to put expected values fro failure and success in g7
// The macros will abort and report sucess/faulre if used in main()
#define FAIL_TEST()            	\
    do {                       	\
     	int regval = 0xbad;    	\
    	asm volatile ("mov %0, %%g7\n\t" : : "r" (regval) : "memory"); \
		return 1;					\
    } while (0)

#define SUCEED_TEST()           \
    do {                       	\
     	int regval = 0x900d;   	\
    	asm volatile ("mov %0, %%g7\n\t" : : "r" (regval) : "memory"); \
		return 0;					\
    } while (0)


#endif