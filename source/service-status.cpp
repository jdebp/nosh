/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <ctime>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "FileDescriptorOwner.h"
#include "unpack.h"
#include "popt.h"
#include "service-manager-client.h"
#include "service-manager.h"
#include "CharacterCell.h"
#include "popt.h"
#include "ttyname.h"
#include "ECMA48Output.h"
#include "TerminalCapabilities.h"

/* Time *********************************************************************
// **************************************************************************
*/

namespace {

inline
struct tm
convert(
	const ProcessEnvironment & envs,
	const uint64_t & s
) {
	const TimeTAndLeap z(tai64_to_time(envs, s));
	struct tm tm;
	localtime_r(&z.time, &tm);
	if (z.leap) ++tm.tm_sec;
	return tm;
}

}

/* Pretty coloured output ***************************************************
// **************************************************************************
*/

namespace {

enum { COLOUR_DEFAULT = -1 };

inline
int
colour_of_state (
	bool ready_after_run,
	uint32_t main_pid,
	bool exited_run,
	char c
) {
	switch (c) {
		case encore_status_stopped:
			if (ready_after_run)
				return exited_run ? COLOUR_GREEN : COLOUR_WHITE;
			else
				return COLOUR_WHITE;
		case encore_status_starting:	return COLOUR_YELLOW;
		case encore_status_started:	return COLOUR_MAGENTA;
		case encore_status_running:
			if (ready_after_run)
				return main_pid ? COLOUR_YELLOW : COLOUR_GREEN;
			else
				return COLOUR_GREEN;
		case encore_status_stopping:	return COLOUR_YELLOW;
		case encore_status_failed:	return COLOUR_RED;
		default:			return COLOUR_DEFAULT;
	}
}

inline
void
set_exception_colour (
	ECMA48Output & o
) {
	o.SGRColour(true, Map256Colour(COLOUR_BLUE));
}

inline
void
set_colour_of_state (
	ECMA48Output & o,
	bool ready_after_run,
	uint32_t main_pid,
	bool exited_run,
	char c
) {
	const int colour(colour_of_state(ready_after_run, main_pid, exited_run, c));
	if (0 > colour)
		o.SGRColour(true);
	else
		o.SGRColour(true, Map256Colour(colour));
}

inline
void
set_config_colour (
	ECMA48Output & o
) {
	o.SGRColour(true, Map256Colour(COLOUR_CYAN));
}

inline
void
reset_colour (
	ECMA48Output & o
) {
	o.SGRColour(true);
}

inline
const char *
state_of (
	bool ready_after_run,
	uint32_t main_pid,
	bool exited_run,
	char c
) {
	switch (c) {
		case encore_status_stopped:
			if (ready_after_run)
				return exited_run ? "done" : "stopped";
			else
				return "stopped";
		case encore_status_starting:	return "starting";
		case encore_status_started:	return "started";
		case encore_status_running:
			if (ready_after_run)
				return main_pid ? "started" : "ready";
			else
				return "running";
		case encore_status_stopping:	return "stopping";
		case encore_status_failed:	return "failed";
		default:			return "unknown";
	}
}

const char * const
status_event[4] = {
	"Started",
	"Ran",
	"Restartd",
	"Stopped",
};

inline
void
write_timestamp (
	const ProcessEnvironment & envs,
	ECMA48Output & o,
	const bool attributes,
	const char * preposition,
	const uint64_t z,
	const uint64_t s
) {
	char buf[64];
	const struct tm t(convert(envs, s));
	const size_t len(std::strftime(buf, sizeof buf, "%F %T %z", &t));
	std::fprintf(stdout, " %s ", preposition);
	if (attributes) o.set_underline(true);
	std::fwrite(buf, len, 1U, stdout);
	if (attributes) o.set_underline(false);
	std::fputc(';', stdout);
	uint64_t secs(z - s);
	uint64_t mins(secs / 60U);
	secs %= 60U;
	uint64_t hours(mins / 60U);
	mins %= 60U;
	uint64_t days(hours / 24U);
	hours %= 24U;
	if (days > 0U)
		std::fprintf(stdout, " %" PRIu64 "d", days);
	if (days > 0 || hours > 0U)
		std::fprintf(stdout, " %" PRIu64 "h", hours);
	if (days > 0 || hours > 0U || mins > 0U)
		std::fprintf(stdout, " %" PRIu64 "m", mins);
	std::fprintf(stdout, " %" PRIu64 "s ago", secs);
}

inline
void
display (
	const ProcessEnvironment & envs,
	const char * name,
	ECMA48Output & o,
	const bool attributes,
	const bool long_form,
	const bool is_ok,
	const bool initially_up,
	const bool run_on_empty,
	const bool ready_after_run,
	const bool use_hangup,
	const bool use_kill,
	const uint64_t z,
	const unsigned int b,
	char status[STATUS_BLOCK_SIZE]
) {
	const uint64_t s(unpack_bigendian(status, 8));
//	const uint32_t n(unpack_bigendian(status + 8, 4));
	const uint32_t p(unpack_littleendian(status + THIS_PID_OFFSET, 4));

	std::fprintf(stdout, "%s: ", name);
	if (long_form) std::fprintf(stdout, "\n\tState   : ");
	if (!is_ok) {
		set_exception_colour(o);
		std::fprintf(stdout, "No supervisor is running");
		reset_colour(o);
		std::fputc('\n', stdout);
	} else
	if (b < DAEMONTOOLS_STATUS_BLOCK_SIZE) {
		set_exception_colour(o);
		std::fprintf(stdout, "loading");
		reset_colour(o);
		std::fputc('\n', stdout);
	} else {
		char & want_flag(status[WANT_FLAG_OFFSET]);
		if (b < ENCORE_STATUS_BLOCK_SIZE) {
			// supervise doesn't turn off the want flag.
			if (p) {
				if ('u' == want_flag) want_flag = '\0';
			} else {
				if ('d' == want_flag) want_flag = '\0';
			}
		}
		const char state(b >= ENCORE_STATUS_BLOCK_SIZE ? status[ENCORE_STATUS_OFFSET] : p ? encore_status_running : encore_status_stopped);
		const bool exited_run(has_exited_run(b, status));
		set_colour_of_state(o, ready_after_run, p, exited_run, state);
		if (b < ENCORE_STATUS_BLOCK_SIZE) {
			std::fprintf(stdout, "%s", p ? "up" : "down");
		} else {
			std::fprintf(stdout, "%s", state_of(ready_after_run, p, exited_run, state));
		}
		reset_colour(o);
		if (p && !long_form)
			std::fprintf(stdout, " (pid %u)", p);
		if (z >= s)
			write_timestamp(envs, o, attributes, "since", z, s);
		const char * const paused(status[PAUSE_FLAG_OFFSET] ? "paused" : "");
		const char * const want('u' == want_flag ? "want up" : 'O' == want_flag ? "once at most" : 'o' == want_flag ? "once" : 'd' == want_flag ? "want down" : "");
		if (long_form) {
			if (p)
				std::fprintf(stdout, "\n\tMain PID: %u", p);
			if (*want || *paused)
				std::fprintf(stdout, "\n\tAction  : %s%s%s", paused, *want && *paused ? ", " : "", want);
			for (unsigned int i(0U); i < 4U; ++i) {
				if (b >= (EXIT_STATUSES_OFFSET + i * EXIT_STATUS_SIZE) + EXIT_STATUS_SIZE) {
					const uint8_t code(status[EXIT_STATUSES_OFFSET + i * EXIT_STATUS_SIZE]);
					if (0 == code) continue;

					const uint32_t number(unpack_bigendian(status + (EXIT_STATUSES_OFFSET + i * EXIT_STATUS_SIZE + 1U), 4));
					const char * reason(1 == code ? "exit" : classify_signal(number));
					std::fprintf(stdout, "\n\t%8.8s: %s %u", status_event[i], reason, number);
					if (1 != code) {
						const char * sname(signame(number));
						if (sname)
							std::fprintf(stdout, " %s", sname);
					}
					if (3 == code)
						std::fputs(" (core dumped)", stdout);

					const uint64_t stamp(unpack_bigendian(status + (EXIT_STATUSES_OFFSET + i * EXIT_STATUS_SIZE + 5U), 8));
					write_timestamp(envs, o, attributes, "at", z, stamp);
				}
			}
		} else {
			const bool is_up(b < ENCORE_STATUS_BLOCK_SIZE ? p : encore_status_stopped != status[ENCORE_STATUS_OFFSET] || (ready_after_run && exited_run));
			if (*want || *paused || is_up != initially_up)
				std::fputs("; ", stdout);
			if (*want || *paused)
				std::fprintf(stdout, "%s%s%s", paused, *want && *paused ? ", " : "", want);
			if (is_up != initially_up) {
				if (*want || *paused)
					std::fputs(", ", stdout);
				set_config_colour(o);
				std::fputs("initially ", stdout);
				if (initially_up)
					std::fputs(b < ENCORE_STATUS_BLOCK_SIZE ? "up" : "started", stdout);
				else
					std::fputs(b < ENCORE_STATUS_BLOCK_SIZE ? "down" : "stopped", stdout);
				reset_colour(o);
			}
			std::fputc('.', stdout);
		}
		std::fputc('\n', stdout);
	}
	if (long_form) {
		std::fprintf(stdout, "\tConfig  : %s", initially_up ? "enabled" : "disabled");
		if (run_on_empty) std::fputs(", run on empty", stdout);
		if (ready_after_run) std::fputs(", ready after run", stdout);
		if (use_hangup) std::fputs(", use SIGHUP", stdout);
		if (!use_kill) std::fputs(", no SIGKILL", stdout);
		std::fputc('\n', stdout);
	}
}

// Used to preserve this argument when we return with new arguments.
std::string current;

}

/* Main function ************************************************************
// **************************************************************************
*/

void
service_status (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	TerminalCapabilities caps(envs);
	ECMA48Output o(caps, std::cout, true /* C1 is 7-bit aliased */, false /* C1 is not raw 8-bit */);

	bool long_form(0 != std::strcmp(prog, "svstat"));
	bool colours(ECMA48Output::query_use_colours(envs, STDOUT_FILENO));
	const char * log_lines = "5";
	try {
		popt::bool_definition long_form_option('\0', "long", "Output in a longer form.", long_form);
		popt::bool_definition colours_option('\0', "colour", "Force output in colour even if standard output is not a terminal.", colours);
		popt::string_definition log_lines_option('\0', "log-lines", "number", "Control the number of log lines printed.", log_lines);
		popt::definition * top_table[] = {
			&long_form_option,
			&colours_option,
			&log_lines_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{directories...}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}
	if (args.empty()) {
		die_missing_argument(prog, envs, "directory name(s)");
	}

	if (!colours)
		caps.colour_level = caps.NO_COLOURS;

	timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	const uint64_t z(time_to_tai64(envs, TimeTAndLeap(now.tv_sec, false)));

	reset_colour(o);

	for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
		const char * name(*i);
		const FileDescriptorOwner bundle_dir_fd(open_dir_at(AT_FDCWD, name));
		if (0 > bundle_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stdout, "%s: ", name);
			if (colours) o.set_italics(true);
			std::fprintf(stdout, "%s", std::strerror(error));
			if (colours) o.set_italics(false);
			std::fputc('\n', stdout);
			continue;
		}

		const FileDescriptorOwner supervise_dir_fd(open_supervise_dir(bundle_dir_fd.get()));
		if (0 > supervise_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stdout, "%s: %s: ", name, "supervise");
			if (colours) o.set_italics(true);
			std::fprintf(stdout, "%s", std::strerror(error));
			if (colours) o.set_italics(false);
			std::fputc('\n', stdout);
			continue;
		}

		const FileDescriptorOwner service_dir_fd(open_service_dir(bundle_dir_fd.get()));

		const bool initially_up(is_initially_up(service_dir_fd.get()));
		const bool run_on_empty(!is_done_after_exit(service_dir_fd.get()));
		const bool ready_after_run(is_ready_after_run(service_dir_fd.get()));
		const bool use_hangup(is_use_hangup_signal(service_dir_fd.get()));
		const bool use_kill(is_use_kill_signal(service_dir_fd.get()));
		char status[STATUS_BLOCK_SIZE];

		const FileDescriptorOwner ok_fd(open_writeexisting_at(supervise_dir_fd.get(), "ok"));
		if (0 > ok_fd.get()) {
			const int error(errno);
			if (ENXIO != error) {
				std::fprintf(stdout, "%s: %s: ", name, "supervise/ok");
				if (colours) o.set_italics(true);
				std::fprintf(stdout, "%s", std::strerror(error));
				if (colours) o.set_italics(false);
				std::fputc('\n', stdout);
				continue;
			}
			display(envs, name, o, colours, long_form, false, initially_up, run_on_empty, ready_after_run, use_hangup, use_kill, z, 0U, status);
		} else {
			const FileDescriptorOwner status_fd(open_read_at(supervise_dir_fd.get(), "status"));
			if (0 > status_fd.get()) {
				const int error(errno);
				std::fprintf(stdout, "%s: %s: ", name, "status");
				if (colours) o.set_italics(true);
				std::fprintf(stdout, "%s", std::strerror(error));
				if (colours) o.set_italics(false);
				std::fputc('\n', stdout);
				display(envs, name, o, colours, long_form, true, initially_up, run_on_empty, ready_after_run, use_hangup, use_kill, z, 0U, status);
			} else {
				const int b(read(status_fd.get(), status, sizeof status));
				display(envs, name, o, colours, long_form, true, initially_up, run_on_empty, ready_after_run, use_hangup, use_kill, z, static_cast<unsigned int>(b), status);
			}
		}

		if (long_form) {
			const FileDescriptorOwner log_main_dir_fd(open_dir_at(bundle_dir_fd.get(), "log/main/"));

			if (0 <= log_main_dir_fd.get()) {
				std::fflush(stdout);

				// We are going to clean up the grandchild(ren) so that our parent does not get stuck with zombies.
				subreaper(true);

				const pid_t child(fork());
				if (0 > child) {
					die_errno(prog, envs, "fork");
				}
				if (0 == child) {
					// We could do some of this in code, but this is simpler and more maintainable, given that we have the builtins.
					current = name + std::string("/log/main/current");
					args.clear();
					args.push_back("pipe");
					args.push_back("--inwards");
					args.push_back("tail");
					args.push_back("-n");
					args.push_back(log_lines);
					args.push_back(current.c_str());
					args.push_back("|");
					args.push_back("tai64nlocal");
					next_prog = arg0_of(args);
					return;
				}
				for (;;) {
					int child_status, child_code;
					pid_t c;
					if (0 >= wait_blocking_for_anychild_exit(c, child_status, child_code)) break;
				}
			}
		}
	}
	throw EXIT_SUCCESS;
}
