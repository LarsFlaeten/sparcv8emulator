#ifndef _LIBSPARC_H_
#define _LIBSPARC_H_

#define stdin   0
#define stdout  1
#define stderr  2

#define BITS_PER_UNIT 8

#define EOF		    -1

typedef unsigned int FILE;
typedef int          DItype    __attribute__ ((mode (DI)));
typedef int          word_type __attribute__ ((mode (__word__)));
typedef int          SItype    __attribute__ ((mode (SI)));
typedef unsigned int USItype   __attribute__ ((mode (SI)));

#if LIBGCC2_WORDS_BIG_ENDIAN
  struct DIstruct {SItype high, low;};
#else
  struct DIstruct {SItype low, high;};
#endif

typedef union
{
  struct DIstruct s;
  DItype ll;
} DIunion;

DItype __lshrdi3 (DItype u, word_type b);
void startup();
//int fprintf (int stream, char *format, ...);
void exit (int status);


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
