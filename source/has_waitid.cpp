/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <sys/types.h>
#include <sys/wait.h>

int
main ()
{
#if defined(WEXITED)
	return 0;
#else
	return 1;
#endif
}
