/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "utils.h"
#if defined(__LINUX__) || defined(__linux__)
#include <sys/prctl.h>
#endif

extern
void
setprocname (
	const char * name
) {
#if defined(__LINUX__) || defined(__linux__)
#	if defined(PR_SET_NAME)
			prctl(PR_SET_NAME, name);
#	endif
#endif
}
