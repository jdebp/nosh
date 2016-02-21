/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "utils.h"
#if !defined(__LINUX__) && !defined(__linux__)
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
#if !defined(__LINUX__) && !defined(__linux__)
	std::string s;
	for (size_t c(0); c < argc; ++c) {
		if (!argv[c]) break;
		s += argv[c];
		s += '\0';
	}
	const int oid[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ARGS, getpid() };
	sysctl(oid, sizeof oid/sizeof *oid, 0, 0, s.data(), s.length());
#endif
}
