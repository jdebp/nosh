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
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
fdmove ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool copy(false);
	try {
		popt::bool_definition copy_option('c', "copy", "Copy rather than move the file descriptor.", copy);
		popt::definition * top_table[] = {
			&copy_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{to} {from} {prog}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing destination file descriptor number.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * to(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing source file descriptor number.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * from(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	const char * old(0);
	old = to;
	const unsigned long d(std::strtoul(to, const_cast<char **>(&to), 0));
	if (to == old || *to) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, old, "Not a number.");
		throw EXIT_FAILURE;
	}
	old = from;
	const unsigned long s(std::strtoul(from, const_cast<char **>(&from), 0));
	if (from == old || *from) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, old, "Not a number.");
		throw EXIT_FAILURE;
	}

	const int rc(dup2(s, d));
	if (0 > rc) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "dup2", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (!copy) close(s);
}
