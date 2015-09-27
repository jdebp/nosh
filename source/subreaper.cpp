/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "utils.h"

/* Subreapers ***************************************************************
// **************************************************************************
*/

#if defined(__LINUX__) || defined(__linux__)
#include <sys/prctl.h>
#else
#include <sys/procctl.h>
#endif
#include <unistd.h>

void
subreaper (
	bool on
) {
#if defined(__LINUX__) || defined(__linux__)
#	if defined(PR_SET_CHILD_SUBREAPER)
		prctl(PR_SET_CHILD_SUBREAPER, on ? 1 : 0);
#	endif
#else
	if (on) {
#	if defined(PROC_REAP_ACQUIRE)
		procctl(P_PID, getpid(), PROC_REAP_ACQUIRE, 0);
#	endif
	} else {
#	if defined(PROC_REAP_RELEASE)
		procctl(P_PID, getpid(), PROC_REAP_RELEASE, 0);
#	endif
	}
#endif
}
