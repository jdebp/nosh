/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "service-manager.h"

/* Main function ************************************************************
// **************************************************************************
*/

int
main (
	int argc, 
	const char * argv[] 
) {
	if (argc < 1) return EXIT_FAILURE;
	const char * prog(basename_of(argv[0]));
	std::vector<const char *> args(argv, argv + argc);
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "directory");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) return EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		return EXIT_USAGE;
	}
	if (1 != args.size()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "One directory name is required.");
		return EXIT_USAGE;
	}
	const char * name(args[0]);
	const int dir_fd(open_dir_at(AT_FDCWD, name));
	if (0 > dir_fd) return EXIT_TEMPORARY_FAILURE;
	const int ok_fd(open_writeexisting_at(dir_fd, "ok"));
	if (0 <= ok_fd) return EXIT_SUCCESS;
	const int supervise_ok_fd(open_writeexisting_at(dir_fd, "supervise/ok"));
	if (0 <= supervise_ok_fd) return EXIT_SUCCESS;
	return EXIT_PERMANENT_FAILURE;
}
