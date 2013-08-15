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

void
pipe ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	bool in(false);
	bool err(false);
	const char * sep("|");
	try {
		popt::bool_definition in_option('i', "inwards", "Pipe inwards rather than outwards.", in);
		popt::bool_definition err_option('e', "error", "Pipe standard error as well.", err);
		popt::string_definition sep_option('S', "separator", "string", "Specify an alternative pipe separator.", sep);
		popt::definition * top_table[] = {
			&in_option,
			&err_option,
			&sep_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "prog | prog");

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

	std::vector<const char *> lhs, rhs, *c = &lhs;
	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		const char * arg(*i);
		if (0 == std::strcmp(sep, arg)) {
			c = &rhs;
		} else {
			c->push_back(arg);
		}
	}

	if (lhs.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing left-hand side.");
		throw EXIT_FAILURE;
	}
	if (rhs.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing right-hand side.");
		throw EXIT_FAILURE;
	}

	int fds[2];
	if (0 > pipe_close_on_exec(fds)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "pipe", std::strerror(error));
		throw EXIT_FAILURE;
	}

	const int pid(fork());
	if (0 > pid) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "fork", std::strerror(error));
		throw EXIT_FAILURE;
	}

	// Outward mode: parent | child
	// Inward mode: child | parent
	const bool am_right((0 == pid) ^ in);

	if (am_right) {
		args = rhs;
		dup2(fds[0], STDIN_FILENO);
	} else {
		args = lhs;
		dup2(fds[1], STDOUT_FILENO);
		if (err)
			dup2(fds[1], STDERR_FILENO);
	}
	// We cannot avoid explicity closing these since we have builtin commands.
	close(fds[0]);
	close(fds[1]);
	next_prog = arg0_of(args);
}
