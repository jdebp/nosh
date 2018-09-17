/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <cstddef>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <cerrno>
#include "utils.h"
#include "fdutils.h"
#include "runtime-dir.h"
#include "common-manager.h"
#include "FileDescriptorOwner.h"
#include "popt.h"

/* Utility functions ********************************************************
// **************************************************************************
*/

static inline
bool
stripfast (
	const char * & prog
) {
	if ('f' != prog[0] || 'a' != prog[1] || 's' != prog[2] || 't' != prog[3]) return false;
	prog += 4;
	return true;
}

/* Signal sending functions *************************************************
// **************************************************************************
*/

static inline
void
send_signal_to_system_manager_process ( 
	const char * prog,
	int signo
) {
	if (0 > kill(1, signo)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kill", std::strerror(error));
		throw EXIT_FAILURE;
	}
}

static inline
void
send_command_to_per_user_manager_process ( 
	const char * prog,
	char command
) {
	const std::string name(effective_user_runtime_dir() + "per-user-manager/control");
	const FileDescriptorOwner control_fd(open_writeexisting_at(AT_FDCWD, name.c_str()));
	if (0 > control_fd.get()) {
		const int error(errno);
		std::fprintf(stdout, "%s: %s: %s\n", prog, name.c_str(), std::strerror(error));
		return;
	}
	const ssize_t n(write(control_fd.get(), &command, sizeof command));
	if (0 > n) {
		const int error(errno);
		std::fprintf(stdout, "%s: %s: %s\n", prog, name.c_str(), std::strerror(error));
		return;
	}
}

static
void
instruct_manager_process ( 
	const char * prog,
	int signo,
	char command
) {
	if (per_user_mode)
		send_command_to_per_user_manager_process(prog, command);
	else
		send_signal_to_system_manager_process(prog, signo);
}

static inline
void
emergency (
	const char * prog
) {
	instruct_manager_process(prog, EMERGENCY_SIGNAL, 'b') ;
}

static inline
void
rescue (
	const char * prog
) {
	instruct_manager_process(prog, SYSINIT_SIGNAL, 'S');
	instruct_manager_process(prog, RESCUE_SIGNAL, 's');
}

static inline
void
normal (
	const char * prog
) {
	instruct_manager_process(prog, SYSINIT_SIGNAL, 'S');
	instruct_manager_process(prog, NORMAL_SIGNAL, 'n');
}

static inline
void
reboot (
	const char * prog,
	bool force
) {
	instruct_manager_process(prog, force ? FORCE_REBOOT_SIGNAL : REBOOT_SIGNAL, force ? 'R' : 'r');
} 

static inline
void
halt (
	const char * prog,
	bool force
) {
#if defined(FORCE_HALT_SIGNAL)
	instruct_manager_process(prog, force ? FORCE_HALT_SIGNAL : HALT_SIGNAL, force ? 'H' : 'h');
#endif
}

static inline
void
poweroff (
	const char * prog,
	bool force
) {
#if defined(FORCE_POWEROFF_SIGNAL)
	instruct_manager_process(prog, force ? FORCE_POWEROFF_SIGNAL : POWEROFF_SIGNAL, force ? 'P' : 'p');
#endif
}

static inline
void
powercycle (
	const char * prog,
	bool force
) {
#if defined(FORCE_POWERCYCLE_SIGNAL)
	instruct_manager_process(prog, force ? FORCE_POWERCYCLE_SIGNAL : POWERCYCLE_SIGNAL, force ? 'C' : 'c');
#else
	instruct_manager_process(prog, force ? FORCE_REBOOT_SIGNAL : REBOOT_SIGNAL, force ? 'R' : 'r');
#endif
}

/* System control commands **************************************************
// **************************************************************************
// These are the built-in state change commands in system-control.
*/

void
reboot_poweroff_halt_command [[gnu::noreturn]] (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool force(stripfast(prog));
	if (0 == std::strcmp(prog, "boot")) prog = "reboot";
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
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
		throw static_cast<int>(EXIT_USAGE);
	}
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	switch (prog[0]) {
		case 'r':	reboot(prog, force); break;
		case 'h':	halt(prog, force); break;
		case 'p':	
			if ('o' == prog[5]) {
				poweroff(prog, force); break;
			}
			if ('c' == prog[5]) {
				powercycle(prog, force); break;
			}
			// Fall through to:
			[[clang::fallthrough]];
		default:
			std::fprintf(stderr, "%s: FATAL: %c: %s\n", prog, prog[0], "Unknown action");
			throw EXIT_FAILURE;
	}

	throw EXIT_SUCCESS;
}

void
emergency_rescue_normal_command [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
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
		throw static_cast<int>(EXIT_USAGE);
	}
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	switch (prog[0]) {
		case 'e':	emergency(prog); break;
		case 's':	// single
		case 'r':	rescue(prog); break;
		case 'd':	// default
		case 'n':	normal(prog); break;
		default:
			std::fprintf(stderr, "%s: FATAL: %c: %s\n", prog, prog[0], "Unknown action");
			throw EXIT_FAILURE;
	}

	throw EXIT_SUCCESS;
}

void
init ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	enum Action { ///< in order of lowest to highest precedence
		NORMAL = 0,
		HALT,
		REBOOT,
		POWERCYCLE,
		POWEROFF,
		UPDATE,
		RESCUE,
		EMERGENCY
	} action = NORMAL;
	try {
		const char * z(0);
		bool rescue_mode(false), emergency_mode(false), update_mode(false);
		bool ignore;
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
#if defined(__LINUX__) || defined(__linux__)
		popt::bool_definition rescue_option('s', "single", "Start in rescue mode.", rescue_mode);
		popt::bool_definition emergency_option('b', "emergency", "Start in emergency mode.", emergency_mode);
#else
		popt::bool_definition emergency_option('s', "single", "Start in emergency mode.", emergency_mode);
#endif
		popt::bool_definition update_option('o', "update", "Start in update mode.", update_mode);
		popt::bool_definition autoboot_option('a', 0, "Compatibility option, ignored.", ignore);
		popt::bool_definition fastboot_option('f', 0, "Compatibility option, ignored.", ignore);
		popt::string_definition z_option('z', 0, "string", "Compatibility option, ignored.", z);
		popt::definition * top_table[] = {
			&user_option,
#if defined(__LINUX__) || defined(__linux__)
			&rescue_option,
			&emergency_option,
#else
			&emergency_option,
#endif
			&update_option,
			&autoboot_option,
			&fastboot_option,
			&z_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "[runlevel(s)...]");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(false /* intermix options and arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		if (emergency_mode && action < EMERGENCY) action = EMERGENCY;
		if (rescue_mode && action < RESCUE) action = RESCUE;
		if (update_mode && action < UPDATE) action = UPDATE;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		const char *p(*i);
		if (p[0] && !p[1]) switch (p[0]) {
			case 'B':
			case 'b':
				if (action < EMERGENCY) action = EMERGENCY;
				break;
			case '1':
			case 'S':
			case 's':
				if (action < RESCUE) action = RESCUE;
				break;
			case 'H':
			case 'h':
				if (action < HALT) action = HALT;
				break;
			case 'C':
			case 'c':
				if (action < POWERCYCLE) action = POWERCYCLE;
				break;
			case '0':
				if (action < POWEROFF) action = POWEROFF;
				break;
			case '6':
				if (action < REBOOT) action = REBOOT;
				break;
			case '2':
			case '3':
			case '4':
			case '5':
			case 'm':
				if (action < NORMAL) action = NORMAL;
				break;
			default:
				std::fprintf(stderr, "%s: WARNING: %c: %s\n", prog, p[0], "Unknown run level ignored.");
				break;
		} else
		if (0 == std::strcmp("emergency", p)) {
			if (action < EMERGENCY) action = EMERGENCY;
		} else
		if (0 == std::strcmp("rescue", p)
		||  0 == std::strcmp("single", p)) {
			if (action < RESCUE) action = RESCUE;
		} else
		if (0 == std::strcmp("update", p)) {
			if (action < UPDATE) action = UPDATE;
		} else
		if (0 == std::strcmp("auto", p)
		||  0 == std::strcmp("default", p)
		||  0 == std::strcmp("normal", p)
		||  0 == std::strcmp("multi", p)) {
			if (action < NORMAL) action = NORMAL;
		} else
			std::fprintf(stderr, "%s: WARNING: %s: %s\n", prog, p, "Unknown run level ignored.");
	}

	switch (action) {
		case EMERGENCY:
			emergency(prog);
			break;
		case RESCUE:
			rescue(prog);
			break;
		case UPDATE:
			args.clear();
			args.insert(args.end(), "update");
			args.insert(args.end(), 0);
			next_prog = arg0_of(args);
			return;
		case POWEROFF:
			poweroff(prog, false);
			break;
		case POWERCYCLE:
			powercycle(prog, false);
			break;
		case REBOOT:
			reboot(prog, false);
			break;
		case HALT:
			halt(prog, false);
			break;
		case NORMAL:
		default:
			normal(prog);
			break;
	}
	throw EXIT_SUCCESS;
}
