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
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "utils.h"
#include "popt.h"
#include "fdutils.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
envdir ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool ignore_nodir(false), full(false), chomp(false);
	try {
		popt::bool_definition ignore_nodir_option('i', "ignore-nodir", "Ignore the absence of the directory.", ignore_nodir);
		popt::bool_definition full_option('f', "full", "Read the full contents of each file.", full);
		popt::bool_definition chomp_option('c', "chomp", "Trim leading and trailing whitespace.", chomp);
		popt::definition * top_table[] = {
			&ignore_nodir_option,
			&full_option,
			&chomp_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing directory name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * dir(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);
	const int scan_dir_fd(open_dir_at(AT_FDCWD, dir));
	if (0 > scan_dir_fd) {
		if (!ignore_nodir) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, dir, std::strerror(error));
			throw EXIT_FAILURE;
		}
	} else {
		if (!process_env_dir(prog, envs, dir, scan_dir_fd, false /*errors are fatal*/, full, chomp))
			throw EXIT_FAILURE;
	}
}
