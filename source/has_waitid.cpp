/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <sys/types.h>
#include <sys/wait.h>
#include <cstdlib>

int
main ()
{
#if defined(WEXITED)
	return EXIT_SUCCESS;
#else
	return EXIT_FAILURE;
#endif
}
