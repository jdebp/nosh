/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <unistd.h>
#include <cstddef>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <cerrno>
#include "utils.h"
#include "common-manager.h"
#include "popt.h"

/* Kill function ************************************************************
// **************************************************************************
*/

static
void
doit ( 
	int signo,
	const char * prog
) {
	if (0 > kill(query_manager_pid(!local_session_mode), signo)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kill", std::strerror(error));
		throw EXIT_FAILURE;
	}
	throw EXIT_SUCCESS;
}

/* Signal sending functions *************************************************
// **************************************************************************
*/

static inline
void
reboot (
	bool bypass_services,
	const char * prog
) {
	if (bypass_services)
		// This signal tells process #1 to shut down and reboot.
		doit(SIGRTMIN + 15, prog);
	else
		// These signals tell process #1 to activate the reboot target.
#if !defined(__LINUX__) && !defined(__linux__)
		// On the BSDs, it is SIGINT and overlaps secure-attention-key.
		doit(SIGINT, prog);
#else
		// On Linux, we go with systemd since upstart and SystemV init do not have it.
		doit(SIGRTMIN + 5, prog);
#endif
}

static inline
void
poweroff (
	bool bypass_services,
	const char * prog
) {
	if (bypass_services)
		// This signal tells process #1 to shut down and power off.
		doit(SIGRTMIN + 14, prog);
	else
		// These signals tell process #1 to activate the poweroff target.
#if !defined(__LINUX__) && !defined(__linux__)
		// On the BSDs, it is SIGUSR2.
		doit(SIGUSR2, prog);
#else
		// On Linux, we go with systemd since upstart and SystemV init do not have it.
		doit(SIGRTMIN + 4, prog);
#endif
}

static inline
void
halt (
	bool bypass_services,
	const char * prog
) {
	if (bypass_services)
		// This signal tells process #1 to shut down and halt.
		doit(SIGRTMIN + 13, prog);
	else
		// These signals tell process #1 to activate the halt target.
#if !defined(__LINUX__) && !defined(__linux__)
		// On the BSDs, it is SIGUSR1.
		doit(SIGUSR1, prog);
#else
		// On Linux, we go with systemd since upstart and SystemV init do not have it.
		doit(SIGRTMIN + 3, prog);
#endif
}

static inline
void
emergency (
	const char * prog
) {
	// This signal tells process #1 to enter what systemd terms "emergency mode".
	// We go with systemd since BSD init, upstart, and SystemV init do not have it.
	doit(SIGRTMIN + 2, prog);
}

static inline
void
rescue (
	const char * prog
) {
	// This signal tells process #1 to enter what BSD terms "single user mode" and what systemd terms "rescue mode".
#if !defined(__LINUX__) && !defined(__linux__)
	// On the BSDs, it is SIGTERM.
	doit(SIGTERM, prog);
#else
	// On Linux, we go with systemd since upstart and SystemV init do not have it.
	doit(SIGRTMIN + 1, prog);
#endif
}

static inline
void
normal (
	const char * prog
) {
	// This signal tells process #1 to enter what BSD terms "multi user mode" and what systemd terms "default mode".
	// On Linux and BSD, we go with systemd since upstart, BSD init, and SystemV init do not have it.
	doit(SIGRTMIN + 0, prog);
}

static inline
void
sysinit (
	const char * prog
) {
	// This signal tells process #1 to spawn "system-control init" with its own arguments.
	doit(SIGRTMIN + 10, prog);
}

/* System control commands **************************************************
// **************************************************************************
*/

void
reboot ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	bool force(false);
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
		popt::bool_definition force_option('f', "force", "Bypass service shutdown.", force);
		popt::definition * main_table[] = {
			&user_option,
			&force_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "");

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

	reboot(force, prog);
}

void
halt ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	bool force(false);
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
		popt::bool_definition force_option('f', "force", "Bypass service shutdown.", force);
		popt::definition * main_table[] = {
			&user_option,
			&force_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "");

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

	halt(force, prog);
}

void
poweroff ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	bool force(false);
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
		popt::bool_definition force_option('f', "force", "Bypass service shutdown.", force);
		popt::definition * main_table[] = {
			&user_option,
			&force_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "");

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

	poweroff(force, prog);
}

void
emergency ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
		popt::definition * main_table[] = {
			&user_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "");

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

	emergency(prog);
}

void
rescue ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
		popt::definition * main_table[] = {
			&user_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "");

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

	rescue(prog);
}

void
normal ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
		popt::definition * main_table[] = {
			&user_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "");

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

	normal(prog);
}

void
sysinit ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
		popt::definition * main_table[] = {
			&user_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "");

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

	sysinit(prog);
}

/* The init subcommand ******************************************************
// **************************************************************************
*/

void
init ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	bool rescue_mode(false), emergency_mode(false);
	try {
		bool ignore;
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
		popt::bool_definition rescue_option('s', "single", "Start in rescue mode.", rescue_mode);
		popt::bool_definition emergency_option('b', "emergency", "Start in emergency mode.", emergency_mode);
		popt::bool_definition autoboot_option('a', "autoboot", "Compatibility option, ignored.", ignore);
		popt::bool_definition fastboot_option('f', "fastboot", "Compatibility option, ignored.", ignore);
		popt::definition * top_table[] = {
			&user_option,
			&rescue_option,
			&emergency_option,
			&autoboot_option,
			&fastboot_option,
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
		throw EXIT_FAILURE;
	}

	if (args.empty() || emergency_mode || rescue_mode) {
		args.clear();
		if (emergency_mode)
			args.insert(args.end(), "emergency");
		else if (rescue_mode)
			args.insert(args.end(), "rescue");
		else
			args.insert(args.end(), "normal");
		args.insert(args.end(), 0);
	} else
		args.insert(args.begin(), "activate");
	next_prog = arg0_of(args);
}
