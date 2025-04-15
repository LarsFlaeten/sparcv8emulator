#include "libsparc.h"


int main()
{

	int x = 3;
	int y = 5;

	if( x+y != 8)
		FAIL_TEST();

	if( y-x != 2)
		FAIL_TEST();

	if( (x*y) != 15)
		FAIL_TEST();
	//FAIL_TEST();
	SUCEED_TEST(); 

}
