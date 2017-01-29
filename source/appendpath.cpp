/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include "utils.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
appendpath ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "var dir prog");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing variable name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * var(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing variable value.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * dir(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);
	if (const char * val = std::getenv(var)) {
		std::string s(val);
		s += ':';
		s += dir;
		const int rc(setenv(var, s.c_str(), 1));
		if (0 > rc) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, var, std::strerror(error));
			throw EXIT_FAILURE;
		}
	} else {
		const int rc(setenv(var, dir, 1));
		if (0 > rc) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, var, std::strerror(error));
			throw EXIT_FAILURE;
		}
	}
}
