/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "utils.h"
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
#include <unistd.h>
#include <sys/exec.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

extern
void
setprocargv (
	size_t argc,
	const char * argv[]
) {
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	std::string s;
	for (size_t c(0); c < argc; ++c) {
		if (!argv[c]) break;
		s += argv[c];
		s += '\0';
	}
	const int oid[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ARGS, getpid() };
#if defined(__OpenBSD__)
	// OpenBSD requires a const incorrectness bodge.
	sysctl(oid, sizeof oid/sizeof *oid, 0, 0, const_cast<char *>(s.data()), s.length());
#else
	sysctl(oid, sizeof oid/sizeof *oid, 0, 0, s.data(), s.length());
#endif
#elif defined(__LINUX__) || defined(__linux__)
	// This is not possible on Linux.
#else
#error "Don't know how to overwrite the process argv on your platform."
#endif
}
