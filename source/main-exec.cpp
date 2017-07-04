/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#if defined(__LINUX__) || defined(__linux__)
#include <sys/prctl.h>
#endif
#include "utils.h"
#include "ProcessEnvironment.h"

/* Main function ************************************************************
// **************************************************************************
*/

int
main (
	int argc, 
	const char * argv[],
	const char * envp[] 
) {
	if (argc < 1) return EXIT_FAILURE;
	std::vector<const char *> args(argv, argv + argc);
	ProcessEnvironment envs(envp);
	const char * next_prog(arg0_of(args));
	const char * prog(basename_of(next_prog));
	const command * c(find(prog, true));
	if (!c) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unknown command personality.");
		return EXIT_FAILURE;
	}
	try {
#if defined(__LINUX__) || defined(__linux__)
#	if defined(PR_SET_NAME)
		// If we were run via fexec() then the "comm" name will be something uninformative like "4" or "5" (from "/proc/self/fd/5").
		// Make it something informative.
		prctl(PR_SET_NAME, prog);
#	endif
#endif
		c->func(next_prog, args, envs);
		exec_terminal (prog, next_prog, args, envs);
	} catch (int r) {
		return r;
	}
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing terminal command.");
		return EXIT_USAGE;
	} else {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, next_prog, std::strerror(error));
		return EXIT_FAILURE;
	}
}
