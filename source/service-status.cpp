/* coPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

/* Time *********************************************************************
// **************************************************************************
*/

static inline
struct tm
convert(
	const uint64_t & s
) {
	bool leap;
	const std::time_t t(tai64_to_time(s, leap));
	struct tm tm;
	localtime_r(&t, &tm);
	if (leap) ++tm.tm_sec;
	return tm;
}

/* Pretty coloured output ***************************************************
// **************************************************************************
*/

static inline
int
colour_of_state (
	char c
) {
	switch (c) {
		case encore_status_stopped:	return COLOR_WHITE;
		case encore_status_starting:	return COLOR_YELLOW;
		case encore_status_started:	return COLOR_MAGENTA;
		case encore_status_running:	return COLOR_GREEN;
		case encore_status_stopping:	return COLOR_YELLOW;
		case encore_status_failed:	return COLOR_RED;
		default:			return COLOR_BLUE;
	}
}

static inline
void
set_colour_of_state (
	bool no_colours,
	char c
) {
	if (no_colours) return;
	const char * s(tigetstr("setaf"));
	if (!s || reinterpret_cast<const char *>(-1) == s) return;
	tputs(tparm(s, colour_of_state(c)), 1, putchar);
}

static inline
void
reset_colour (
	bool no_colours
) {
	if (no_colours) return;
	const char * s(tigetstr("op"));
	if (!s || reinterpret_cast<const char *>(-1) == s) return;
	tputs(s, 1, putchar);
}

static inline
const char *
state_of (
	char c
) {
	switch (c) {
		case encore_status_stopped:	return "stopped";
		case encore_status_starting:	return "starting";
		case encore_status_started:	return "started";
		case encore_status_running:	return "running";
		case encore_status_stopping:	return "stopping";
		case encore_status_failed:	return "failed";
		default:			return "unknown";
	}
}

static inline
void
display (
	const char * name,
	const bool long_form,
	const bool colours,
	const bool initially_up,
	const uint64_t z,
	const int b,
	char status[20]
) {
	if (b < 18)
		std::fprintf(stdout, "%s: loading\n", name);
	else {
		const uint64_t s(unpack_bigendian(status, 8));
//			const uint32_t n(unpack_bigendian(status + 8, 4));
		const uint32_t p(unpack_littleendian(status + 12, 4));

		std::fprintf(stdout, "%s: ", name);
		if (long_form) std::fprintf(stdout, "\n\tState   : ");
		if (b < 20) {
			set_colour_of_state(!colours, p ? encore_status_running : encore_status_stopped);
			std::fprintf(stdout, "%s", p ? "up" : "down");
			reset_colour(!colours);
			// supervise doesn't turn off the want flag.
			if (p) {
				if ('u' == status[17]) status[17] = '\0';
			} else {
				if ('d' == status[17]) status[17] = '\0';
			}
		} else {
			const char state(status[18]);
			set_colour_of_state(!colours, state);
			std::fprintf(stdout, "%s", state_of(state));
			reset_colour(!colours);
		}
		if (p && !long_form)
			std::fprintf(stdout, " (pid %u)", p);
		if (z >= s) {
			char buf[64];
			const struct tm t(convert(s));
			const size_t len(std::strftime(buf, sizeof buf, "%F %T %z", &t));
			std::fputs(" since ", stdout);
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
		const char * const paused(status[16] ? "paused" : "");
		const char * const want('u' == status[17] ? "want up" : 'O' == status[17] ? "once at most" : 'o' == status[17] ? "once" : 'd' == status[17] ? "want down" : "");
		if (long_form) {
			if (p)
				std::fprintf(stdout, "\n\tMain PID: %u", p);
			std::fprintf(stdout, "\n\tConfig  : %s%s%s%s%s", initially_up ? "enabled" : "disabled", *paused ? ", " : "", paused, *want ? ", " : "", want);
		} else {
			std::fputs(". ", stdout);
			const bool is_up(b < 20 ? p : encore_status_stopped != status[18]);
			std::fprintf(stdout, "%s%s%s", paused, *want ? ", " : "", want);
			if (is_up != initially_up) {
				std::fputs(", initially ", stdout);
				if (initially_up)
					std::fputs(b < 20 ? "up" : "started", stdout);
				else
					std::fputs(b < 20 ? "down" : "stopped", stdout);
			}
		}
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
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));

	bool long_form(0 != std::strcmp(prog, "svstat"));
	bool colours(isatty(STDOUT_FILENO));
	const char * log_lines = "5";
	try {
		popt::bool_definition long_form_option('\0', "long", "Output in a longer form.", long_form);
		popt::bool_definition colours_option('\0', "colour", "Output in colour if the terminal is capable of it.", colours);
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
	const uint64_t z(time_to_tai64(now.tv_sec, false));

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

		const FileDescriptorOwner ok_fd(open_writeexisting_at(supervise_dir_fd.get(), "ok"));
		if (0 > ok_fd.get()) {
			const int error(errno);
			if (ENXIO == error)
				std::fprintf(stdout, "%s: No supervisor is running\n", name);
			else
				std::fprintf(stdout, "%s: %s: %s\n", name, "supervise/ok", std::strerror(error));
			continue;
		}

		const FileDescriptorOwner status_fd(open_read_at(supervise_dir_fd.get(), "status"));
		if (0 > status_fd.get()) {
			const int error(errno);
			std::fprintf(stdout, "%s: %s: %s\n", name, "status", std::strerror(error));
			continue;
		}
		char status[20];
		const int b(read(status_fd.get(), status, sizeof status));

		display(name, long_form, colours, initially_up, z, b, status);

		if (long_form) {
			const FileDescriptorOwner log_main_dir_fd(open_dir_at(bundle_dir_fd.get(), "log/main/"));

			if (0 <= log_main_dir_fd.get()) {
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
				int cs;
				waitpid(child, &cs, 0);
			}
		}
	}
	throw EXIT_SUCCESS;
}
