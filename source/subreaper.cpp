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
#elif defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/procctl.h>
#endif
#include <unistd.h>
#include <cerrno>

int
subreaper (
	bool on
) {
#if defined(__LINUX__) || defined(__linux__)
#	if defined(PR_SET_CHILD_SUBREAPER)
		return prctl(PR_SET_CHILD_SUBREAPER, on ? 1 : 0);
#	else
		return errno = ENOSYS, -1;
#	endif
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	if (on) {
#	if defined(PROC_REAP_ACQUIRE)
		return procctl(P_PID, getpid(), PROC_REAP_ACQUIRE, 0);
#	endif
	} else {
#	if defined(PROC_REAP_RELEASE)
		return procctl(P_PID, getpid(), PROC_REAP_RELEASE, 0);
#	endif
	}
	return errno = ENOSYS, -1;
#else
	return errno = ENOSYS, -1;
#endif
}
