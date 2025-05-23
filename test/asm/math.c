#define EPSILONF 0.00001f
#define EPSILOND 0.00001

float _fabs(float a)
{
    if(a < 0.0f)
        return -a;
    else
        return a;
}

int allmost_eqf(float a, float b)
{
    if(  (_fabs(a-b) < EPSILONF)) // && ( _fabs(a-b) > -EPSILON ) )
        return 1;
    else
        return 0;
}

double _fabsd(double a)
{
    if(a < 0.0)
        return -a;
    else
        return a;
}

int allmost_eqd(double a, double b)
{
    if(  (_fabsd(a-b) < EPSILOND))
        return 1;
    else
        return 0;
}
