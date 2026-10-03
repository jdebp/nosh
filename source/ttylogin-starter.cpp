/* coPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <unistd.h>
#if defined(__LINUX__) || defined(__linux__)
#include <linux/vt.h>
#include "kqueue_linux.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#else
#include <sys/event.h>
#endif
#include "kqueue_common.h"
#include "popt.h"
#include "fdutils.h"
#include "utils.h"
#include "SignalManagement.h"

/* Main function ************************************************************
// **************************************************************************
*/

#if defined(__LINUX__) || defined(__linux__)
static const char tty_class_dir[] = "/sys/class/tty";
static const char tty0[] = "tty0";
static const char console[] = "console";
static const char active[] = "active";
#endif

void
ttylogin_starter (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
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
		popt::string_definition log_prefix_option('\0', "log-prefix", "string", "Specify the additional prefix for the log service name.", log_prefix);
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
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (!args.empty()) die_unexpected_argument(prog, args, envs);

#if defined(__LINUX__) || defined(__linux__)
	FileDescriptorOwner class_dir_fd(open_dir_at(AT_FDCWD, tty_class_dir));
	if (0 > class_dir_fd.get()) {
		die_errno(prog, envs, tty_class_dir);
	}
	FileDescriptorOwner tty0_dir_fd(open_dir_at(class_dir_fd.get(), tty0));
	if (0 > tty0_dir_fd.get()) {
		die_errno(prog, envs, tty_class_dir, tty0);
	}
	FileDescriptorOwner console_dir_fd(open_dir_at(class_dir_fd.get(), console));
	if (0 > console_dir_fd.get()) {
		die_errno(prog, envs, tty_class_dir, console);
	}
	class_dir_fd.release();
	FileDescriptorOwner tty0_active_file_fd(open_read_at(tty0_dir_fd.get(), active));
	if (0 > tty0_active_file_fd.get()) {
		die_errno(prog, envs, tty_class_dir, tty0, active);
	}
	tty0_dir_fd.release();
	FileDescriptorOwner console_active_file_fd(open_read_at(console_dir_fd.get(), active));
	if (0 > console_active_file_fd.get()) {
		die_errno(prog, envs, tty_class_dir, console, active);
	}
	console_dir_fd.release();

	// Pre-create the kernel virtual terminals so that the user can switch to them in the first place.
	for (unsigned n(0U); n < num_ttys; ++n) {
		char b[32];
		snprintf(b, sizeof b, "/dev/tty%u", n);
		const FileDescriptorOwner tty_fd(open_readwriteexisting_at(AT_FDCWD, b));
	}

	const FileStar tty0_active_file(fdopen(tty0_active_file_fd.get(), "r"));
	if (!tty0_active_file) {
		die_errno(prog, envs, tty_class_dir, tty0, active);
	}

	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGTSTP, SIGALRM, SIGPIPE, SIGQUIT, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGTSTP, SIGALRM, SIGPIPE, SIGQUIT, 0);

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		die_errno(prog, envs, "kqueue");
	}

	std::vector<struct kevent> ip;
	append_event(ip, tty0_active_file_fd.get(), EVFILT_READ, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGALRM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGQUIT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);

	tty0_active_file_fd.release();

	bool in_shutdown(false);
	for (;;) {
		if (in_shutdown) break;
		struct kevent e;
		const int rc(kevent(queue.get(), ip.data(), ip.size(), &e, 1, nullptr));
		ip.clear();
		if (0 > rc) {
			if (EINTR == errno) continue;
			die_errno(prog, envs, "kevent");
		} else
		if (1 > rc) continue;
		switch (e.filter) {
			case EVFILT_READ:
			{
				const int fd(e.ident);
				if (fileno(tty0_active_file) == fd) {
					// It is important to read from the file, clearing the READ state, before forking and letting the parent loop round to poll again.
					// To ensure that we re-read the file, we must call std::fflush() after std::rewind(), so that the standard library actually reads from the underlying file rather than from its internal buffer that it has already read and just rewinds back to the beginning of.
					// Strictly speaking, we are relying upon GNU and BSD libc non-standard extensions to std::fflush(), here.
					std::rewind(tty0_active_file);
					std::fflush(tty0_active_file);
					std::string ttyname;
					const bool success(read_line(tty0_active_file, ttyname));
					if (!success) {
						die_errno(prog, envs, active);
					}
					const pid_t system_control_pid(fork());
					if (-1 == system_control_pid) {
						die_errno(prog, envs, "fork");
					} else if (0 == system_control_pid) {
						// These must have static storage duration as we are using them in args.
						static std::string service_name, log_service_name;

						service_name = prefix + ttyname + ".service";
						log_service_name = log_prefix + service_name;
						args.clear();
						args.push_back("system-control");
						args.push_back("start");
						if (verbose)
							args.push_back("--verbose");
						args.push_back(service_name.c_str());
						args.push_back(log_service_name.c_str());
						next_prog = arg0_of(args);
						return;
					} else
					{
						int status, code;
						if (0 >= wait_blocking_for_exit_of(system_control_pid, status, code)) {
							die_errno(prog, envs, "wait");
						}
					}
				}
				break;
			}
			case EVFILT_SIGNAL:
				switch (e.ident) {
					case SIGHUP:
					case SIGTERM:
					case SIGINT:
					case SIGPIPE:
					case SIGQUIT:
						in_shutdown = true;
						break;
					case SIGTSTP:
						std::fprintf(stderr, "%s: INFO: %s\n", prog, "Paused.");
						raise(SIGSTOP);
						std::fprintf(stderr, "%s: INFO: %s\n", prog, "Continued.");
						break;
					case SIGALRM:
					default:
#if defined(DEBUG)
						std::fprintf(stderr, "%s: DEBUG: signal event ident %lu fflags %x\n", prog, e.ident, e.fflags);
#endif
						break;
				}
				break;
			default:
#if defined(DEBUG)
				std::fprintf(stderr, "%s: DEBUG: event filter %hd ident %lu fflags %x\n", prog, e.filter, e.ident, e.fflags);
#endif
				break;
		}
	}
	throw EXIT_SUCCESS;
#else
	args.clear();
	args.push_back("pause");
	next_prog = arg0_of(args);
#endif
}
