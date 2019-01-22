/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

static
void
foreground_background_common ( 
	bool do_wait,
	const char * sep,
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::string_definition sep_option('S', "separator", "string", "Specify an alternative pipe separator.", sep);
		popt::definition * top_table[] = {
			&sep_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog} {;}|{&} {prog}");

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

	std::vector<const char *> lhs, rhs, *c = &lhs;
	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		const char * arg(*i);
		if (c != &rhs && 0 == std::strcmp(sep, arg)) {
			c = &rhs;
		} else {
			c->push_back(arg);
		}
	}

	if (lhs.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing left-hand side.");
		throw static_cast<int>(EXIT_USAGE);
	}
	if (rhs.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing right-hand side.");
		throw static_cast<int>(EXIT_USAGE);
	}

	const pid_t pid(fork());
	if (0 > pid) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "fork", std::strerror(error));
		throw EXIT_FAILURE;
	}

	args = 0 < pid ? rhs : lhs;
	next_prog = arg0_of(args);
	if (do_wait && (0 < pid)) {
		int status, code;
		wait_blocking_for_exit_of(pid, status, code);
	}
}

void
foreground ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	foreground_background_common(true, ";", next_prog, args);
}

void
background ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	foreground_background_common(false, "&", next_prog, args);
}
