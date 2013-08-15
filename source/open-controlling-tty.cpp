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
#include <unistd.h>
#include <fcntl.h>
#include "popt.h"
#include "utils.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
open_controlling_tty ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
#if defined(_GNU_SOURCE)
	bool no_vhangup(false);
#else
	bool no_revoke(false);
#endif
	bool exclusive(false);

	const char * prog(basename_of(args[0]));
	try {
#if defined(_GNU_SOURCE)
		popt::bool_definition no_vhangup_option('\0', "no-vhangup", "Do not execute the vhangup call.", no_vhangup);
#else
		popt::bool_definition no_revoke_option('\0', "no-revoke", "Do not execute the revoke call.", no_revoke);
#endif
		popt::bool_definition exclusive_option('\0', "exclusive", "Attempt to set exclusive mode.", exclusive);
		popt::definition * top_table[] = {
#if defined(_GNU_SOURCE)
			&no_vhangup_option,
#else
			&no_revoke_option,
#endif
			&exclusive_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "prog");

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

	const char * tty(std::getenv("TTY"));
	if (!tty) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "TTY", "Missing environment variable.");
		throw EXIT_FAILURE;
	}

#if !defined(_GNU_SOURCE)
	if (!no_revoke) {
		if (0 > revoke(tty)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, tty, std::strerror(error));
			throw EXIT_FAILURE;
		}
	}
#endif

	// The System V way of acquiring the controlling TTY is just to open it.
	// This is so full of ifs and buts on Linux that it is ridiculous; and it doesn't forcibly remove control.
	// So we just use the BSD mechanism, which works on both the BSDs and Linux.
	int fd(open(tty, O_RDWR|O_NOCTTY, 0));
	if (fd < 0) {
error_exit:
		{
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, tty, std::strerror(error));
		}
close_exit:
		if (fd >= 0) close(fd);
		throw EXIT_FAILURE;
	}
	if (!isatty(fd)) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, tty, "Not a terminal device.");
		goto close_exit;
	}

	if (exclusive) {
		if (0 > ioctl(fd, TIOCEXCL, 1)) {
			const int error(errno);
			std::fprintf(stderr, "%s: WARNING: %s: %s\n", prog, tty, std::strerror(error));
		}
	}

	// This is the BSD way of acquiring the controlling TTY.
	// The 1 flag causes the forcible removal of this controlling TTY from existing processes, if we have the privileges.
	if (0 > ioctl(fd, TIOCSCTTY, 1)) goto error_exit;

#if defined(_GNU_SOURCE)
	if (!no_vhangup) {
		// We are be about to do vhangup() with an open fd to the TTY.
		// We don't want to be terminated by the hangup signal ourselves.
		struct sigaction sa;
		sa.sa_handler=SIG_IGN;
		sa.sa_flags=0;
		sigemptyset(&sa.sa_mask);
		sigaction(SIGHUP,&sa,NULL);

		// Unfortunately, the TTY has to actually ALREADY be OUR controlling TTY for this to work.
		if (vhangup()) goto error_exit;

		sa.sa_handler=SIG_DFL;
		sa.sa_flags=0;
		sigemptyset(&sa.sa_mask);
		sigaction(SIGHUP,&sa,NULL);

		// Because we've just hung up, we have to re-open.
		// This causes a race condition against any other privileged process also attempting to open this TTY.
		close(fd);
		fd = open(tty, O_RDWR, 0);
		if (fd < 0) goto error_exit;
	}
#endif

	// Now just dup the file descriptors.
	if (0 > dup2(fd, STDIN_FILENO)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "stdin", std::strerror(error));
		goto close_exit;
	}
	if (0 > dup2(fd, STDOUT_FILENO)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "stdout", std::strerror(error));
		goto close_exit;
	}
	if (0 > dup2(fd, STDERR_FILENO)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "stderr", std::strerror(error));
		goto close_exit;
	}

	if ((STDIN_FILENO != fd) && (STDOUT_FILENO != fd) && (STDERR_FILENO != fd))
		close(fd);
	fd = -1;
}
