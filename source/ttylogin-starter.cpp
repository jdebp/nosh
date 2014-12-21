/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <sys/poll.h>
#include <unistd.h>
#if defined(__LINUX__) || defined(__linux__)
#include <linux/vt.h>
#endif
#include "popt.h"
#include "fdutils.h"
#include "utils.h"

/* Main function ************************************************************
// **************************************************************************
*/

#if defined(__LINUX__) || defined(__linux__)
static const char active[] = "active";
#endif

// This must have static storage duration as we are using it in args.
static std::string service_name;

void
ttylogin_starter ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
#if defined(__LINUX__) || defined(__linux__)
	const char * class_dir("/sys/class/tty");
	const char * tty("tty0");
	unsigned long num_ttys(MAX_NR_CONSOLES);
#endif
	bool verbose(false);
	try {
#if defined(__LINUX__) || defined(__linux__)
		popt::string_definition class_option('\0', "class", "directory", "Specify the sysfs TTY class for kernel virtual consoles.", class_dir);
		popt::string_definition tty_option('\0', "tty", "tty-name", "Specify the sysfs TTY name for the 0th kernel virtual console.", tty);
		popt::unsigned_number_definition num_ttys_option('n', "num-ttys", "number", "Number of kernel virtual terminals.", num_ttys, 0);
#endif
		popt::bool_definition verbose_option('v', "verbose", "Verbose mode.", verbose);
		popt::definition * top_table[] = {
#if defined(__LINUX__) || defined(__linux__)
			&class_option,
			&tty_option,
			&num_ttys_option,
#endif
			&verbose_option
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

	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unrecognized argument(s).");
		throw EXIT_FAILURE;
	}

	struct sigaction sa;
	sa.sa_flags=0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler=SIG_IGN;
	sigaction(SIGCHLD,&sa,NULL);

#if defined(__LINUX__) || defined(__linux__)
	const int class_dir_fd(open_dir_at(AT_FDCWD, class_dir));
	if (0 > class_dir_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, class_dir, std::strerror(error));
		throw EXIT_FAILURE;
	}
	const int tty_dir_fd(open_dir_at(class_dir_fd, tty));
	if (0 > tty_dir_fd) {
		const int error(errno);
		close(class_dir_fd);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, class_dir, tty, std::strerror(error));
		throw EXIT_FAILURE;
	}
	close(class_dir_fd);
	const int active_file_fd(open_read_at(tty_dir_fd, active));
	if (0 > active_file_fd) {
		const int error(errno);
		close(tty_dir_fd);
		std::fprintf(stderr, "%s: FATAL: %s/%s/%s: %s\n", prog, class_dir, tty, active, std::strerror(error));
		throw EXIT_FAILURE;
	}
	close(tty_dir_fd);

	// Pre-create the kernel virtual terminals so that the user can switch to them in the first place.
	for (unsigned n(0U); n < num_ttys; ++n) {
		char b[32];
		snprintf(b, sizeof b, "/dev/tty%u", n);
		const int tty_fd(open_readwriteexisting_at(AT_FDCWD, b));
		if (0 <= tty_fd) close(tty_fd);
	}

	FILE * active_file(fdopen(active_file_fd, "r"));
	if (!active_file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s/%s: %s\n", prog, class_dir, tty, active, std::strerror(error));
		throw EXIT_FAILURE;
	}
	pollfd p;
	p.fd = active_file_fd;
	p.events = POLLPRI;
	for (;;) {
		const int rc(poll(&p, 1, -1));
		if (0 > rc) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, active, std::strerror(error));
			throw EXIT_FAILURE;
		}
		if (p.revents & POLLPRI) {
			// It is important to read from the file, clearing the POLLPRI state, before forking and letting the parent loop round to poll again.
			// To ensure that we re-read the file, we must call std::fflush() after std::rewind(), so that the standard library actually reads from the underlying file rather than from its internal buffer that it has already read and just rewinds back to the beginning of.
			// Strictly speaking, we are relying upon GNU and BSD libc non-standard extensions to std::fflush(), here.
			std::rewind(active_file);
			std::fflush(active_file);
			std::string ttyname;
			const bool success(read_line(active_file, ttyname));
			if (!success) {
				const int error(errno);
				std::fclose(active_file);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, active, std::strerror(error));
				throw EXIT_FAILURE;
			}
			const int system_control_pid(fork());
			if (-1 == system_control_pid) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
			} else if (0 == system_control_pid) {
				std::fclose(active_file);
				service_name = "ttylogin@" + ttyname + ".service";
				args.clear();
				args.insert(args.end(), "system-control");
				args.insert(args.end(), "start");
				if (verbose)
					args.insert(args.end(), "--verbose");
				args.insert(args.end(), service_name.c_str());
				args.insert(args.end(), 0);
				next_prog = arg0_of(args);
				return;
			}
		}
	}
#else
	for (unsigned int i(0); i < 16; ++i) {
		const int system_control_pid(fork());
		if (-1 == system_control_pid) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
		} else if (0 == system_control_pid) {
			char ttyname[64];
			snprintf(ttyname, sizeof ttyname, "ttyv%x", i);
			service_name = (std::string("ttylogin@") + ttyname) + ".service";
			args.clear();
			args.insert(args.end(), "system-control");
			args.insert(args.end(), "start");
			if (verbose)
				args.insert(args.end(), "--verbose");
			args.insert(args.end(), service_name.c_str());
			args.insert(args.end(), 0);
			next_prog = arg0_of(args);
			return;
		}
	}
#endif
	throw EXIT_SUCCESS;
}
