/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include "utils.h"

/* Main function ************************************************************
// **************************************************************************
*/

int
main (
	int argc, 
	const char * argv[] 
) {
	if (argc < 1) return EXIT_FAILURE;
	std::vector<const char *> args(argv, argv + argc);
	const char * next_prog(arg0_of(args));
	const char * prog(basename_of(next_prog));
	const command * c(find(prog, true));
	if (!c) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unknown command personality.");
		return EXIT_FAILURE;
	}
	try {
		c->func(next_prog, args);
		exec_terminal (prog, next_prog, args);
	} catch (int r) {
		return r;
	}
	if (args.empty())
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing terminal command.");
	else {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, next_prog, std::strerror(error));
	}
	return EXIT_FAILURE;
}
