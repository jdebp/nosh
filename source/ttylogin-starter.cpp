/* coPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <unistd.h>
#if defined(__LINUX__) || defined(__linux__)
#include <linux/vt.h>
#include <sys/poll.h>
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#else
#if 0 // This is no gain over the normal enable/disable mechanisms for services.
#include <ttyent.h>
#endif
#endif
#include "popt.h"
#include "fdutils.h"
#include "utils.h"
#include "terminal_database.h"

/* Main function ************************************************************
// **************************************************************************
*/

#if defined(__LINUX__) || defined(__linux__)
static const char tty_class_dir[] = "/sys/class/tty";
static const char tty0[] = "tty0";
static const char console[] = "console";
static const char active[] = "active";
#endif

// This must have static storage duration as we are using it in args.
static std::string service_name, log_service_name;

void
ttylogin_starter ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	const char * prefix("ttylogin@");
	const char * log_prefix("cyclog@");
#if defined(__LINUX__) || defined(__linux__)
	unsigned long num_ttys(MAX_NR_CONSOLES);
#endif
	bool verbose(false);
	try {
#if defined(__LINUX__) || defined(__linux__)
		popt::unsigned_number_definition num_ttys_option('n', "num-ttys", "number", "Number of kernel virtual terminals.", num_ttys, 0);
#endif
		popt::bool_definition verbose_option('v', "verbose", "Verbose mode.", verbose);
		popt::string_definition prefix_option('p', "prefix", "string", "Prefix each TTY name with this (template) name.", prefix);
		popt::string_definition log_prefix_option('\0', "log-prefix", "string", "Specify the prefix for the log service name.", log_prefix);
		popt::definition * top_table[] = {
#if defined(__LINUX__) || defined(__linux__)
			&num_ttys_option,
#endif
			&prefix_option,
			&log_prefix_option,
			&verbose_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "");

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

	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unrecognized argument(s).");
		throw static_cast<int>(EXIT_USAGE);
	}

	struct sigaction sa;
	sa.sa_flags=0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler=SIG_IGN;
	sigaction(SIGCHLD,&sa,NULL);

#if defined(__LINUX__) || defined(__linux__)
	FileDescriptorOwner class_dir_fd(open_dir_at(AT_FDCWD, tty_class_dir));
	if (0 > class_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, tty_class_dir, std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileDescriptorOwner tty0_dir_fd(open_dir_at(class_dir_fd.get(), tty0));
	if (0 > tty0_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, tty_class_dir, tty0, std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileDescriptorOwner console_dir_fd(open_dir_at(class_dir_fd.get(), console));
	if (0 > console_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, tty_class_dir, console, std::strerror(error));
		throw EXIT_FAILURE;
	}
	class_dir_fd.release();
	FileDescriptorOwner tty0_active_file_fd(open_read_at(tty0_dir_fd.get(), active));
	if (0 > tty0_active_file_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s/%s: %s\n", prog, tty_class_dir, tty0, active, std::strerror(error));
		throw EXIT_FAILURE;
	}
	tty0_dir_fd.release();
	FileDescriptorOwner console_active_file_fd(open_read_at(console_dir_fd.get(), active));
	if (0 > console_active_file_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s/%s: %s\n", prog, tty_class_dir, console, active, std::strerror(error));
		throw EXIT_FAILURE;
	}
	console_dir_fd.release();

	// Pre-create the kernel virtual terminals so that the user can switch to them in the first place.
	for (unsigned n(0U); n < num_ttys; ++n) {
		char b[32];
		snprintf(b, sizeof b, "/dev/tty%u", n);
		const int tty_fd(open_readwriteexisting_at(AT_FDCWD, b));
		if (0 <= tty_fd) close(tty_fd);
	}

	FileStar tty0_active_file(fdopen(tty0_active_file_fd.get(), "r"));
	if (!tty0_active_file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s/%s: %s\n", prog, tty_class_dir, tty0, active, std::strerror(error));
		throw EXIT_FAILURE;
	}
	pollfd p;
	p.fd = tty0_active_file_fd.get();
	p.events = POLLPRI;
	tty0_active_file_fd.release();

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
			std::rewind(tty0_active_file);
			std::fflush(tty0_active_file);
			std::string ttyname;
			const bool success(read_line(tty0_active_file, ttyname));
			if (!success) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, active, std::strerror(error));
				throw EXIT_FAILURE;
			}
			const int system_control_pid(fork());
			if (-1 == system_control_pid) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
			} else if (0 == system_control_pid) {
				service_name = prefix + ttyname + ".service";
				log_service_name = log_prefix + service_name;
				args.clear();
				args.insert(args.end(), "system-control");
				args.insert(args.end(), "start");
				if (verbose)
					args.insert(args.end(), "--verbose");
				args.insert(args.end(), service_name.c_str());
				args.insert(args.end(), log_service_name.c_str());
				args.insert(args.end(), 0);
				next_prog = arg0_of(args);
				return;
			}
		}
	}
	throw EXIT_SUCCESS;
#else
#if 0 // This is no gain over the normal enable/disable mechanisms for services.
	errno = 0;
	if (!setttyent()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "setttyent", std::strerror(error));
		throw EXIT_FAILURE;
	}
	while (const struct ttyent *entry = getttyent()) {
		if (!is_on(*entry))
			continue;
		const int system_control_pid(fork());
		if (-1 == system_control_pid) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
		} else if (0 == system_control_pid) {
			endttyent();
			service_name = (std::string(prefix) + entry->ty_name) + ".service";
			log_service_name = log_prefix + service_name;
			args.clear();
			args.insert(args.end(), "system-control");
			args.insert(args.end(), "start");
			if (verbose)
				args.insert(args.end(), "--verbose");
			args.insert(args.end(), service_name.c_str());
			args.insert(args.end(), log_service_name.c_str());
			args.insert(args.end(), 0);
			next_prog = arg0_of(args);
			return;
		}
	}
	endttyent();

	// Reap all of the children just created.
	for (;;) {
		pid_t c;
		int status, code;
		if (0 >= wait_blocking_for_anychild_exit(c, status, code)) break;
	}
#endif

	args.clear();
	args.insert(args.end(), "pause");
	args.insert(args.end(), 0);
	next_prog = arg0_of(args);
#endif
}
