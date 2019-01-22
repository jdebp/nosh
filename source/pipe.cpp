/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <csignal>
#include <sys/types.h>
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
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool in(false);
#if 0 // This was undocumented and has been removed.
	bool err(false);
#endif
	bool grandchild(false);
	bool ignore_children(false);
	const char * sep("|");
	try {
		popt::bool_definition in_option('i', "inwards", "Pipe inwards rather than outwards.", in);
#if 0 // This was undocumented and has been removed.
		popt::bool_definition err_option('e', "error", "Pipe standard error as well.", err);
#endif
		popt::bool_definition grandchild_option('g', "grandchild", "Fork and orphan a grandchild.", grandchild);
		popt::bool_definition ignore_children_option('i', "ignore-children", "Ignore SIGCHLD so that all children are automatically cleaned up.", ignore_children);
		popt::string_definition sep_option('S', "separator", "string", "Specify an alternative pipe separator.", sep);
		popt::definition * top_table[] = {
			&in_option,
#if 0 // This was undocumented and has been removed.
			&err_option,
#endif
			&grandchild_option,
			&ignore_children_option,
			&sep_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog} {|} {prog}");

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
		if (0 == std::strcmp(sep, arg)) {
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

	int fds[2];
	if (0 > pipe_close_on_exec(fds)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "pipe", std::strerror(error));
		throw EXIT_FAILURE;
	}

	pid_t child(fork());
	if (0 > child) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "fork", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (grandchild) {
		if (0 == child) {
			child = fork();
			if (0 > child) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "fork", std::strerror(error));
				throw error;	// Parent is us, and we understand this convention.
			}
			if (0 != child)
				throw 0;
		} else {
			int status, code;
			wait_blocking_for_exit_of(child, status, code);
			if (0 != code) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "fork", std::strerror(code));
				throw EXIT_FAILURE;
			}
		}
	} else
	if (ignore_children && 0 != child) {
		struct sigaction sa;
		sa.sa_flags=0;
		sigemptyset(&sa.sa_mask);
		sa.sa_handler=SIG_IGN;
		sigaction(SIGCHLD,&sa,NULL);
	}

	// Outward mode: parent | child
	// Inward mode: child | parent
	const bool am_right((0 == child) ^ in);

	if (am_right) {
		args = rhs;
		dup2(fds[0], STDIN_FILENO);
	} else {
		args = lhs;
		dup2(fds[1], STDOUT_FILENO);
#if 0 // This was undocumented and has been removed.
		if (err)
			dup2(fds[1], STDERR_FILENO);
#endif
	}
	// We cannot avoid explicity closing these since we have builtin commands.
	close(fds[0]);
	close(fds[1]);
	next_prog = arg0_of(args);
}
