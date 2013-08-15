/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "environ.h"

#if !defined(_GNU_SOURCE)
extern "C" 
int 
clearenv()
{
	for (char **e(environ); *e; ++e)
		*e = 0;
	return 0;
}
#endif
