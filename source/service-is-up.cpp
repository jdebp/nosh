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

void
service_is_up (
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));

	try {
		popt::top_table_definition main_option(0, 0, "Main options", "directory");

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
	int dir_fd(open_dir_at(AT_FDCWD, name));
	if (0 > dir_fd) throw EXIT_TEMPORARY_FAILURE;
	int ok_fd(open_writeexisting_at(dir_fd, "ok"));
	if (0 > ok_fd) {
		const int old_dir_fd(dir_fd);
		dir_fd = open_dir_at(dir_fd, "supervise");
		close(old_dir_fd);
		if (0 > dir_fd) throw EXIT_TEMPORARY_FAILURE;
		ok_fd = open_writeexisting_at(dir_fd, "ok");
		if (0 > ok_fd) throw EXIT_TEMPORARY_FAILURE;
	}
	const int status_fd(open_read_at(dir_fd, "status"));
	if (0 > status_fd) throw EXIT_TEMPORARY_FAILURE;
	char status[20];
	const int n(read(status_fd, status, sizeof status));
	if (0 > n) throw EXIT_TEMPORARY_FAILURE;
	if (18 <= n) {
		if (20 <= n) {
			// This is the same as daemontools-encore, but subtly different from the nagios-check-service command.
			if (encore_status_running == status[18] || encore_status_starting == status[18]) throw EXIT_SUCCESS;
		} else {
			if (status[12] || status[13] || status[14] || status[15]) throw EXIT_SUCCESS;
		}
	}
	throw EXIT_PERMANENT_FAILURE;
}
