/* coPYING ******************************************************************
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
#include "service-manager-client.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
service_is_ok [[gnu::noreturn]] (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "{directory}");

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

	if (1 != args.size()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "One directory name is required.");
		throw static_cast<int>(EXIT_USAGE);
	}

	const char * name(args[0]);
	const int bundle_dir_fd(open_dir_at(AT_FDCWD, name));
	if (0 > bundle_dir_fd) throw static_cast<int>(EXIT_TEMPORARY_FAILURE);
	const int supervise_dir_fd(open_supervise_dir(bundle_dir_fd));
	if (0 > supervise_dir_fd) throw static_cast<int>(EXIT_TEMPORARY_FAILURE);

	if (is_ok(supervise_dir_fd)) 
		throw EXIT_SUCCESS;
	else 
		throw static_cast<int>(EXIT_PERMANENT_FAILURE);
}
