/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <ctime>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <curses.h>
#include <term.h>
#include "utils.h"
#include "fdutils.h"
#include "FileDescriptorOwner.h"
#include "unpack.h"
#include "popt.h"
#include "service-manager-client.h"
#include "service-manager.h"
#include "curses-const-fix.h"	// Must come after curses.h .

/* Time *********************************************************************
// **************************************************************************
*/

static inline
struct tm
convert(
	const ProcessEnvironment & envs,
	const uint64_t & s
) {
	bool leap;
	const std::time_t t(tai64_to_time(envs,s, leap));
	struct tm tm;
	localtime_r(&t, &tm);
	if (leap) ++tm.tm_sec;
	return tm;
}

/* Pretty coloured output ***************************************************
// **************************************************************************
*/

enum { COLOR_DEFAULT = 9 };

static
NCURSES_CONST
char 
	setaf[] = "setaf",
	op[] = "op";

static inline
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
				return exited_run ? COLOR_GREEN : COLOR_WHITE;
			else
				return COLOR_WHITE;
		case encore_status_starting:	return COLOR_YELLOW;
		case encore_status_started:	return COLOR_MAGENTA;
		case encore_status_running:
			if (ready_after_run)
				return main_pid ? COLOR_YELLOW : COLOR_GREEN;
			else
				return COLOR_GREEN;
		case encore_status_stopping:	return COLOR_YELLOW;
		case encore_status_failed:	return COLOR_RED;
		default:			return COLOR_DEFAULT;
	}
}

static inline
void
put (
	const char * s,
	const int colour
) {
	tputs(tparm(const_cast<NCURSES_CONST char *>(s), colour), 1, putchar);
}

static inline
void
set_exception_colour (
	bool no_colours
) {
	if (no_colours) return;
	const char * s(tigetstr(setaf));
	if (!s || reinterpret_cast<const char *>(-1) == s) return;
	put(s, COLOR_BLUE);
}

static inline
void
set_colour_of_state (
	bool no_colours,
	bool ready_after_run,
	uint32_t main_pid,
	bool exited_run,
	char c
) {
	if (no_colours) return;
	const char * s(tigetstr(setaf));
	if (!s || reinterpret_cast<const char *>(-1) == s) return;
	put(s, colour_of_state(ready_after_run, main_pid, exited_run, c));
}

static inline
void
set_config_colour (
	bool no_colours
) {
	if (no_colours) return;
	const char * s(tigetstr(setaf));
	if (!s || reinterpret_cast<const char *>(-1) == s) return;
	put(s, COLOR_CYAN);
}

static inline
void
reset_colour (
	bool no_colours
) {
	if (no_colours) return;
	const char * s(tigetstr(op));
	if (!s || reinterpret_cast<const char *>(-1) == s) {
		s = tigetstr(setaf);
		if (!s) return;
		put(s, COLOR_DEFAULT);
	} else
		tputs(s, 1, putchar);
}

static inline
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

static inline
bool
has_exited_run (
	const unsigned int b,
	const char status[STATUS_BLOCK_SIZE]
) {
	if (b < STATUS_BLOCK_SIZE) return false;
	const char * const ran_exit_status(status + EXIT_STATUSES_OFFSET + EXIT_STATUS_SIZE * 1);
	return 0 != ran_exit_status[0];
}

static
const char * const
status_event[4] = {
	"Started",
	"Ran",
	"Restartd",
	"Stopped",
};

static inline
void
write_timestamp (
	const ProcessEnvironment & envs,
	const char * preposition,
	const uint64_t z,
	const uint64_t s
) {
	char buf[64];
	const struct tm t(convert(envs, s));
	const size_t len(std::strftime(buf, sizeof buf, "%F %T %z", &t));
	std::fprintf(stdout, " %s ", preposition);
	std::fwrite(buf, len, 1U, stdout);
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

static inline
void
display (
	const ProcessEnvironment & envs,
	const char * name,
	const bool long_form,
	const bool colours,
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
		set_exception_colour(!colours);
		std::fprintf(stdout, "No supervisor is running");
		reset_colour(!colours);
		std::fputc('\n', stdout);
	} else
	if (b < DAEMONTOOLS_STATUS_BLOCK_SIZE) {
		set_exception_colour(!colours);
		std::fprintf(stdout, "loading");
		reset_colour(!colours);
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
		set_colour_of_state(!colours, ready_after_run, p, exited_run, state);
		if (b < ENCORE_STATUS_BLOCK_SIZE) {
			std::fprintf(stdout, "%s", p ? "up" : "down");
		} else {
			std::fprintf(stdout, "%s", state_of(ready_after_run, p, exited_run, state));
		}
		reset_colour(!colours);
		if (p && !long_form)
			std::fprintf(stdout, " (pid %u)", p);
		if (z >= s) 
			write_timestamp(envs, "since", z, s);
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
					write_timestamp(envs, "at", z, stamp);
				}
			}
		} else {
			const bool is_up(b < ENCORE_STATUS_BLOCK_SIZE ? p : encore_status_stopped != status[ENCORE_STATUS_OFFSET]);
			if (*want || *paused || is_up != initially_up)
				std::fputs("; ", stdout);
			if (*want || *paused)
				std::fprintf(stdout, "%s%s%s", paused, *want && *paused ? ", " : "", want);
			if (is_up != initially_up) {
				if (*want || *paused)
					std::fputs(", ", stdout);
				set_config_colour(!colours);
				std::fputs("initially ", stdout);
				if (initially_up)
					std::fputs(b < ENCORE_STATUS_BLOCK_SIZE ? "up" : "started", stdout);
				else
					std::fputs(b < ENCORE_STATUS_BLOCK_SIZE ? "down" : "stopped", stdout);
				reset_colour(!colours);
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
static std::string current;

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

	bool long_form(0 != std::strcmp(prog, "svstat"));
	bool colours(isatty(STDOUT_FILENO));
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
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "directories...");

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
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing directory name(s).");
		throw static_cast<int>(EXIT_USAGE);
	}

	if (colours) {
		int err;
		if (OK != setupterm(0, STDOUT_FILENO, &err))
			colours = false;
	}

	timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	const uint64_t z(time_to_tai64(envs, now.tv_sec, false));

	reset_colour(!colours);

	for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
		const char * name(*i);
		const FileDescriptorOwner bundle_dir_fd(open_dir_at(AT_FDCWD, name));
		if (0 > bundle_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stdout, "%s: %s\n", name, std::strerror(error));
			continue;
		}

		const FileDescriptorOwner supervise_dir_fd(open_supervise_dir(bundle_dir_fd.get()));
		if (0 > supervise_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stdout, "%s: %s: %s\n", name, "supervise", std::strerror(error));
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
				std::fprintf(stdout, "%s: %s: %s\n", name, "supervise/ok", std::strerror(error));
				continue;
			}
			display(envs, name, long_form, colours, false, initially_up, run_on_empty, ready_after_run, use_hangup, use_kill, z, 0U, status);
		} else {
			const FileDescriptorOwner status_fd(open_read_at(supervise_dir_fd.get(), "status"));
			if (0 > status_fd.get()) {
				const int error(errno);
				std::fprintf(stdout, "%s: %s: %s\n", name, "status", std::strerror(error));
				display(envs, name, long_form, colours, true, initially_up, run_on_empty, ready_after_run, use_hangup, use_kill, z, 0U, status);
			} else {
				const int b(read(status_fd.get(), status, sizeof status));
				display(envs, name, long_form, colours, true, initially_up, run_on_empty, ready_after_run, use_hangup, use_kill, z, static_cast<unsigned int>(b), status);
			}
		}

		if (long_form) {
			const FileDescriptorOwner log_main_dir_fd(open_dir_at(bundle_dir_fd.get(), "log/main/"));

			if (0 <= log_main_dir_fd.get()) {
				std::fflush(stdout);

				// We are going to clean up the grandchild(ren) so that our parent does not get stuck with zombies.
				subreaper(true);

				const int child(fork());
				if (0 > child) {
					const int error(errno);
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "fork", std::strerror(error));
					throw EXIT_FAILURE;
				}
				if (0 == child) {
					// We could do some of this in code, but this is simpler and more maintainable, given that we have the builtins.
					current = name + std::string("/log/main/current");
					args.clear();
					args.insert(args.end(), "pipe");
					args.insert(args.end(), "--inwards");
					args.insert(args.end(), "tail");
					args.insert(args.end(), "-n");
					args.insert(args.end(), log_lines);
					args.insert(args.end(), current.c_str());
					args.insert(args.end(), "|");
					args.insert(args.end(), "tai64nlocal");
					args.insert(args.end(), 0);
					next_prog = arg0_of(args);
					return;
				}
				for (;;) {
					int child_status;
					pid_t c;
					if (0 >= wait_blocking_for_anychild_exit(c, child_status)) break;
				}
			}
		}
	}
	throw EXIT_SUCCESS;
}
