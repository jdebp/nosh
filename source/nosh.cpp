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
#include "fdutils.h"

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

#if defined(__LINUX__) || defined(__linux__)
#	define FD_PREFIX "/proc/self/fd/"
#else
#	define FD_PREFIX "/dev/fd/"
#endif

static inline
void
undo_open_exec_fd_bodge(
	const char * name
) {
	const std::size_t l(std::strlen(name));
	if (l < sizeof FD_PREFIX) return;	// It must have at least 1 digit character after the prefix.
	if (0 != std::strncmp(name, FD_PREFIX, sizeof FD_PREFIX - 1)) return;
	name += sizeof FD_PREFIX - 1;
	int fd(0);
	while (const int c = *name) {
		if (!std::isdigit(c)) return;
		fd = fd * 10 + (c - '0');
		++name;
	}
	set_close_on_exec(fd, true);
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
		throw static_cast<int>(EXIT_USAGE);
	}
	arg_strings = read_file(prog, args[1]);
	std::vector<const char *> new_args(convert(arg_strings));
	if (new_args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "No arguments in script.");
		throw EXIT_FAILURE;
	}
	undo_open_exec_fd_bodge(args[1]);
	args = new_args;
	next_prog = arg0_of(args);
}
