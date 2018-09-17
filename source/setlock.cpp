/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include "utils.h"
#include "popt.h"
#include "fdutils.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
setlock ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool non_blocking(false);
	bool ignore(false);
	try {
		popt::bool_definition non_blocking_option('n', "non-blocking", "Fail if the lock cannot be acquired immediately.", non_blocking);
		popt::bool_definition ignore_option('x', "ignore", "Quietly ignore failure.", ignore);
		popt::definition * top_table[] = {
			&non_blocking_option,
			&ignore_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{filename} {prog}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing file name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * filename(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);
	const int fd((non_blocking ? open_lockfile_at : open_lockfile_or_wait_at)(AT_FDCWD, filename));
	if (0 > fd) {
		if (ignore) throw EXIT_SUCCESS;
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
	set_close_on_exec(fd, false);
}
