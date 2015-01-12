/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/ioctl.h>
#if defined(__LINUX__) || defined(__linux__)
#include <sys/vt.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include "popt.h"
#include "utils.h"

/* Main function ************************************************************
// **************************************************************************
*/

static const char tty[] = "/dev/tty";

void
detach_controlling_tty ( 
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

	const int fd(open(tty, O_RDWR|O_NOCTTY, 0));
	if (0 <= fd) {
		if (!isatty(fd)) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, tty, "Not a terminal device.");
close_exit:
			close(fd);
			throw EXIT_FAILURE;
		}
		if (0 > ioctl(fd, TIOCNOTTY, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, "TIOCNOTTY", tty, std::strerror(error));
			goto close_exit;
		}
		close(fd);
	}
}