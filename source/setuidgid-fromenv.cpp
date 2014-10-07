/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <grp.h>
#include "popt.h"
#include "utils.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
setuidgid_fromenv ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "prog");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	if (const char * text = std::getenv("GID")) {
		const char * old(text);
		gid_t gid = std::strtoul(text, const_cast<char **>(&text), 0);
		if (text == old || *text) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, old, "Not a number.");
			throw EXIT_FAILURE;
		}
		gid_t groups[1] = { gid };
		if (0 > setgroups(sizeof groups/sizeof *groups, groups) || 0 > setgid(gid)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, old, std::strerror(error));
			throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
		}
	} else {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "GID", "Missing environment variable.");
		throw EXIT_FAILURE;
	}
	if (const char * text = std::getenv("UID")) {
		const char * old(text);
		uid_t uid = std::strtoul(text, const_cast<char **>(&text), 0);
		if (text == old || *text) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, old, "Not a number.");
			throw EXIT_FAILURE;
		}
		if (0 > setuid(uid)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, old, std::strerror(error));
			throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
		}
	} else {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "UID", "Missing environment variable.");
		throw EXIT_FAILURE;
	}
}
