/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cctype>
#include "utils.h"

/* Main function ************************************************************
// **************************************************************************
*/

/// We need to keep these around after the function exits.
static std::vector<std::string> arg_strings;

static inline
std::vector<const char *>
convert (
	const std::vector<std::string> & s
) {
	std::vector<const char *> r(s.size());
	for (size_t i(s.size()); i > 0 ; ) {
		--i;
		r[i] = s[i].c_str();
	}
	return r;
}

void
nosh ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	if (2 != args.size()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Incorrect number of arguments.");
		throw EXIT_FAILURE;
	}
	arg_strings = read_file(prog, args[1]);
	std::vector<const char *> new_args(convert(arg_strings));
	if (new_args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "No arguments in script.");
		throw EXIT_FAILURE;
	}
	args = new_args;
	next_prog = arg0_of(args);
}
