#include "libsparc.h"

int main()
{
    float a = 3.1415f;
    float b = 1.2001f;

    float correct_ans = a * b + 1.0f;


    float d = a - 1.01f;
    float d2 = d + 1.01f;


    float e = b + 5.6f;
    float e2 = e - 5.6f;

    float f = d2 * e2;


    if( (f - correct_ans > 0.00001f) || (f - correct_ans < -0.00001f))
       return (0xbad);

    return 0xbed; 


}
