#include "libsparc.h"
#include "math.h"
#include "test.h"

#define SUM1 4.3416f

int main()
{
    
    float a = 3.1415f;
    float b = 1.2001f;

    float sum = a + b;

    //if(sum != SUM1)
    //    FAIL_TEST();

    if( !allmost_eqf(sum, SUM1))
        FAIL_TEST();

    float aa = 1.46766f;
    float naa = -1.46766f;
    if( (-aa) != naa)
        FAIL_TEST();

    


    float correct_ans = a * b + 1.0f;


    float d = a - 1.01f;
    float d2 = d + 1.01f;


    float e = b + 5.6f;
    float e2 = e - 5.6f;

    float f = d2 * e2 + 1.0f;

    if( !allmost_eqf(f, correct_ans))
       FAIL_TEST();

    if(!allmost_eqf(1.0f-2.0f, -1.0f))
        FAIL_TEST();

    if(!allmost_eqf(2.0f-1.0f, 1.0f))
        FAIL_TEST();

    if(!allmost_eqf(1.0f/2.0f, 0.5f))
        FAIL_TEST();

    if(!allmost_eqf(1.0f/0.5f, 2.0f))
        FAIL_TEST();

    if(!allmost_eqf(2.0f*2.0f, 4.0f))
        FAIL_TEST();

    if(!allmost_eqf(2.0f*0.5f, 1.0f))
        FAIL_TEST();

    if(!allmost_eqf(-2.0f*0.5f, -1.0f))
        FAIL_TEST();

    if(!(1.0f > 0.0f))
        FAIL_TEST();

    if(!(-1.0f < 0.0f))
        FAIL_TEST();

    if(!(1.0f == 1.0f))
        FAIL_TEST();

    if(!(_fabs(-2.34f) == 2.34f))
        FAIL_TEST();

    SUCEED_TEST(); 


}
