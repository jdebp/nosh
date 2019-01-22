/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <iostream>
#include <unistd.h>
#include <cstddef>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <cmath>
#include <sys/wait.h>
#include "utils.h"
#include "popt.h"

/* System commands **********************************************************
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

void
reboot_poweroff_halt_command (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool force(stripfast(prog));
	if (0 == std::strcmp(prog, "boot")) prog = "reboot";
	try {
		bool poweroff(false);
		bool powercycle(false);
		bool halt(false);
		bool reboot(false);
		popt::bool_definition force_option('f', "force", "Bypass service shutdown jobs.", force);
		popt::bool_definition poweroff_option('p', "poweroff", "Power off, even if this is not the poweroff command.", poweroff);
		popt::bool_definition powercycle_option('c', "powercycle", "Power cycle, even if this is not the powercycle command.", powercycle);
		popt::bool_definition halt_option('h', "halt", "Halt, even if this is not the halt command.", halt);
		popt::bool_definition reboot_option('r', "reboot", "Reboot, even if this is not the reboot command.", reboot);
		popt::definition * main_table[] = {
			&force_option,
			&poweroff_option,
			&powercycle_option,
			&halt_option,
			&reboot_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;

		if (poweroff) prog = "poweroff";
		else if (powercycle) prog = "powercycle";
		else if (reboot) prog = "reboot";
		else if (halt) prog = "halt";
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	args.clear();
	if (force) {
		args.insert(args.end(), "system-control");
		args.insert(args.end(), prog);
		args.insert(args.end(), "--force");
	} else {
		// Stuff that we put into args has to have static storage duration.
		static char opt[3] = { '-', ' ', '\0' };

		args.insert(args.end(), "shutdown");
		if ('p' == prog[0] && 'c' == prog[5])
			opt[1] = 'c';
#if defined(__LINUX__) || defined(__linux__)
		else if ('h' == prog[0])
			opt[1] = 'H';
#endif
		else
			opt[1] = prog[0];
		args.insert(args.end(), opt);
		// systemd shutdown has this sensible default if the time specification is omitted.
		// BSD shutdown does not.
		args.insert(args.end(), "now");
	}
	args.insert(args.end(), 0);
	next_prog = arg0_of(args);
}

// Some commands are thin wrappers around system-control subcommands of the same name.
void
wrap_system_control_subcommand (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	args.insert(args.begin(), "system-control");
	next_prog = arg0_of(args);
}

static
const char *
formats[] = {
	"%M",
	"%H:%M",
	"%H%M",
	"%d%H%M",
	"%m%d%H%M",
	"%y%m%d%H%M",
	"%Y%m%d%H%M",
};

void
shutdown (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	const char * grace_period(0);
	bool halt(false), reboot(false), powercycle(false), no_wall(false), kick_off(false), verbose(false);
	try {
		bool poweroff(true), o(false);
		popt::bool_definition verbose_option('v', "verbose", "Print messages.", verbose);
		popt::bool_definition halt_option('h', "halt", "Halt the machine.", halt);
		popt::bool_definition h_option('H', 0, "Halt the machine.", halt);
		popt::bool_definition poweroff_option('p', "poweroff", "Compatibility option, on by default.", poweroff);
		popt::bool_definition p_option('P', 0, "Compatibility option, on by default.", poweroff);
		popt::bool_definition powercycle_option('c', "powercycle", "Power cycle the machine.", powercycle);
		popt::bool_definition o_option('o', 0, "Compatibility option, off by default.", o);
		popt::bool_definition reboot_option('r', "reboot", "Reboot the machine.", reboot);
		popt::bool_definition no_wall_option('\0', "no-wall", "Do not send wall messages.", no_wall);
		popt::bool_definition kick_off_option('k', "kick-off", "Do not enact the final state change.", kick_off);
		popt::string_definition grace_period_option('g', "grace-period", "[hh:]mm", "The grace period before shutdown happens.", grace_period);
		popt::definition * main_table[] = {
			&verbose_option,
			&reboot_option,
			&halt_option,
			&h_option,
			&poweroff_option,
			&p_option,
			&powercycle_option,
			&o_option,
			&no_wall_option,
			&kick_off_option,
			&grace_period_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "{time} [message]");

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

	time_t now(std::time(0)), when(now);
	struct std::tm now_tm;
	localtime_r(&when, &now_tm);

	if (grace_period) {
		if (0 != std::strcmp("now", grace_period)) {
			const char * end;
			unsigned long minutes(strtoul(grace_period, const_cast<char **>(&end), 10));
			if (end == grace_period || (*end && ':' != *end)) {
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, grace_period, "Bad interval");
				throw EXIT_FAILURE;
			}
			grace_period = end;
			if (*grace_period) {
				++grace_period;	// Can only be a colon, from the test above.
				unsigned long l(strtoul(grace_period, const_cast<char **>(&end), 10));
				if (end == grace_period || *end) {
					std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, grace_period, "Bad interval");
					throw EXIT_FAILURE;
				}
				minutes = minutes * 60UL + l;
			}
			struct std::tm when_tm(now_tm);
			when_tm.tm_min += minutes;
			when = std::mktime(&when_tm);
		}
	} else {
		if (args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing time.");
			throw static_cast<int>(EXIT_USAGE);
		}
		const char * timespec(args.front());
		args.erase(args.begin());
		bool found(false);
		if (!found && 0 == std::strcmp("now", timespec)) {
			found = true;
		}
		if (!found) {
			for (const char ** pp(formats); pp < formats + sizeof formats/sizeof *formats; ++pp) {
				struct std::tm when_tm(now_tm);
				const char * r(strptime(timespec, *pp, &when_tm));
				if (r && !*r) {
					when_tm.tm_sec = 0;
					when = std::mktime(&when_tm);
					if (when < now) {
						std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, timespec, "Time is in the past.");
						throw EXIT_FAILURE;
					}
					found = true;
					break;
				}
			}
		}
		if (!found && '+' == *timespec) {
			const char * end;
			unsigned long l(strtoul(timespec + 1, const_cast<char **>(&end), 10));
			if (end != timespec + 1 && !*end) {
				struct std::tm when_tm(now_tm);
				when_tm.tm_min += l;
				when = std::mktime(&when_tm);
				found = true;
			}
		}
		if (!found) {
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, timespec, "Bad time");
			throw EXIT_FAILURE;
		}
	}

	const char * participle = reboot ? "rebooted" : halt ? "halted" : "powered off";

	for (;;) {
		const double secs(std::difftime(when, now));
		if (verbose)
			std::fprintf(stderr, "%s: INFO: Shutdown in %g seconds.\n", prog, secs);
		if (!no_wall && secs <= 86400.0 && (secs < 1.0 || secs >= 15.0)) {
			const pid_t wall(fork());
			if (-1 == wall) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
			} else if (0 == wall) {
				static char banner[256];
				if (secs <= 0.0)
					snprintf(banner, sizeof banner, "The system is being %s right now.", participle);
				else
					snprintf(banner, sizeof banner, "The system is going to be %s in %lg seconds.", participle, secs);
				const bool no_message(args.empty());
				args.insert(args.end(), "|");
				args.insert(args.end(), "wall");
				if (no_message)
					args.insert(args.begin(), "true");
				else
					args.insert(args.begin(), "echo");
				args.insert(args.begin(), banner);
				args.insert(args.begin(), "line-banner");
				args.insert(args.begin(), "pipe");
				next_prog = arg0_of(args);
				return;
			} else {
				int status, code;
				wait_blocking_for_exit_of(wall, status, code);
			}
		}
		if (secs <= 0.0)
			break;
		else if (secs <= 15.0)
			sleep(1);
		else if (secs <= 60.0) {
			const unsigned int v(static_cast<unsigned int>(std::fmod(secs, 15.0)));
			sleep(v ? v : 15);
		}
		else if (secs <= 300.0) {
			const unsigned int v(static_cast<unsigned int>(std::fmod(secs, 60.0)));
			sleep(v ? v : 60);
		}
		else if (secs <= 900.0) {
			const unsigned int v(static_cast<unsigned int>(std::fmod(secs, 300.0)));
			sleep(v ? v : 300);
		}
		else if (secs <= 3600.0) {
			const unsigned int v(static_cast<unsigned int>(std::fmod(secs, 900.0)));
			sleep(v ? v : 900);
		}
		else if (secs <= 21600.0) {
			const unsigned int v(static_cast<unsigned int>(std::fmod(secs, 3600.0)));
			sleep(v ? v : 3600);
		}
		else if (secs <= 86400.0) {
			const unsigned int v(static_cast<unsigned int>(std::fmod(secs, 21600.0)));
			sleep(v ? v : 21600);
		}
		else {
			const unsigned int v(static_cast<unsigned int>(std::fmod(secs, 86400.0)));
			sleep(v ? v : 86400);
		}
	       	now = std::time(0);
	}

	if (verbose)
		std::fprintf(stderr, "%s: INFO: Shutdown now.\n", prog);
	if (kick_off)
		throw EXIT_SUCCESS;
	args.clear();
	args.insert(args.end(), "system-control");
	if (reboot)
		args.insert(args.end(), "reboot");
	else if (halt)
		args.insert(args.end(), "halt");
	else if (powercycle)
		args.insert(args.end(), "powercycle");
	else
		args.insert(args.end(), "poweroff");
	args.insert(args.end(), 0);
	next_prog = arg0_of(args);
}

void
rcctl (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	next_prog = args[0] = "system-control";
	if (args.size() > 1) {
		if (0 == std::strcmp(args[1], "set"))
			args[1] = "set-service-env";
		else
		if (0 == std::strcmp(args[1], "get"))
			args[1] = "print-service-env";
	}
}

void
runlevel [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * main_table[] = { };
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

	std::cout << "N N\n";
	throw EXIT_SUCCESS;
}

void
telinit ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	args[0] = "init";
	args.insert(args.begin(), "system-control");
	next_prog = args[0];
}
