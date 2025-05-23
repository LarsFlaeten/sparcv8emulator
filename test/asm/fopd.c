#include "libsparc.h"
#include "math.h"
#include "test.h"

#define SUM1 4.3416

int main()
{
    
    double a = 3.1415;
    double b = 1.2001;

    float sum = a + b;

    //iif(sum != SUM1)
    //    FAIL_TEST();

    if( !allmost_eqd(sum, SUM1))
        FAIL_TEST();

    double aa = 1.46766;
    double naa = -1.46766;
    if( (-aa) != naa)
        FAIL_TEST();

    


    float correct_ans = a * b + 1.0;


    float d = a - 1.01;
    float d2 = d + 1.01;


    float e = b + 5.6;
    float e2 = e - 5.6;

    float f = d2 * e2 + 1.0;

    if( !allmost_eqd(f, correct_ans))
       FAIL_TEST();

    if(!allmost_eqd(1.0-2.0, -1.0))
        FAIL_TEST();

    if(!allmost_eqd(2.0-1.0, 1.0))
        FAIL_TEST();

    if(!allmost_eqd(1.0/2.0, 0.5))
        FAIL_TEST();

    if(!allmost_eqd(1.0/0.5, 2.0))
        FAIL_TEST();

    if(!allmost_eqd(2.0*2.0, 4.0))
        FAIL_TEST();

    if(!allmost_eqd(2.0*0.5, 1.0))
        FAIL_TEST();

    if(!allmost_eqd(-2.0*0.5, -1.0))
        FAIL_TEST();

    if(!(1.0 > 0.0))
        FAIL_TEST();

    if(!(-1.0 < 0.0))
        FAIL_TEST();

    if(!(1.0 == 1.0))
        FAIL_TEST();

    if(!(_fabsd(-2.34) == 2.34))
        FAIL_TEST();

    SUCEED_TEST(); 


}
