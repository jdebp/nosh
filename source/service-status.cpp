/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <curses.h>
#include <term.h>
#include "utils.h"
#include "fdutils.h"
#include "unpack.h"
#include "popt.h"
#include "service-manager-client.h"
#include "service-manager.h"

/* Main function ************************************************************
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

int
main (
	int argc, 
	const char * argv[] 
) {
	if (argc < 1) return EXIT_FAILURE;
	const char * prog(basename_of(argv[0]));
	std::vector<const char *> args(argv, argv + argc);
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "directory");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) return EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		return EXIT_USAGE;
	}
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing directory name(s).");
		return EXIT_USAGE;
	}

	bool no_colours(!isatty(STDOUT_FILENO));
	if (!no_colours) {
		int err;
		if (OK != setupterm(0, STDOUT_FILENO, &err))
			no_colours = false;
	}

	timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	const uint64_t z(0x4000000000000000ULL + now.tv_sec + 10U);

	reset_colour(no_colours);

	for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
		const char * name(*i);
		int bundle_dir_fd(open_dir_at(AT_FDCWD, name));
		if (0 > bundle_dir_fd) {
			const int error(errno);
			std::fprintf(stdout, "%s: %s\n", name, std::strerror(error));
			continue;
		}

		int service_dir_fd(open_dir_at(bundle_dir_fd, "service/"));
		if (0 > service_dir_fd) service_dir_fd = dup(bundle_dir_fd);

		const bool initially_up(is_initially_up(service_dir_fd));

		close(service_dir_fd); service_dir_fd = -1;

		int supervise_dir_fd(open_dir_at(bundle_dir_fd, "supervise/"));
		if (0 > supervise_dir_fd) supervise_dir_fd = dup(bundle_dir_fd);

		close(bundle_dir_fd), bundle_dir_fd = -1;

		int ok_fd(open_writeexisting_at(supervise_dir_fd, "ok"));
		if (0 > ok_fd) {
			const int error(errno);
			close(supervise_dir_fd); supervise_dir_fd = -1;
			if (ENXIO == error)
				std::fprintf(stdout, "%s: No supervisor is running\n", name);
			else
				std::fprintf(stdout, "%s: %s: %s\n", name, "supervise/ok", std::strerror(error));
			continue;
		}
		close(ok_fd), ok_fd = -1;

		int status_fd(open_read_at(supervise_dir_fd, "status"));
		close(supervise_dir_fd); supervise_dir_fd = -1;
		if (0 > status_fd) {
			const int error(errno);
			std::fprintf(stdout, "%s: %s: %s\n", name, "status", std::strerror(error));
			continue;
		}
		char status[20];
		const int b(read(status_fd, status, sizeof status));
		close(status_fd), status_fd = -1;

		if (b < 18)
			std::fprintf(stdout, "%s: loading\n", name);
		else {
			const uint64_t s(unpack_bigendian(status, 8));
//			const uint32_t n(unpack_bigendian(status + 8, 4));
			const uint32_t p(unpack_littleendian(status + 12, 4));

			std::fprintf(stdout, "%s: ", name);
			if (b < 20) {
				set_colour_of_state(no_colours, p ? encore_status_running : encore_status_stopped);
				std::fprintf(stdout, "%s", p ? "up" : "down");
				reset_colour(no_colours);
				// supervise doesn't turn off the want flag.
				if (p) {
					if ('u' == status[17]) status[17] = '\0';
				} else {
					if ('d' == status[17]) status[17] = '\0';
				}
			} else {
				const char state(status[18]);
				set_colour_of_state(no_colours, state);
				std::fprintf(stdout, "%s", state_of(state));
				reset_colour(no_colours);
			}
			if (p)
				std::fprintf(stdout, " (pid %u)", p);
			if (z >= s) {
				uint64_t secs(z - s);
				uint64_t mins(secs / 60U);
				secs %= 60U;
				uint64_t hours(mins / 60U);
				mins %= 60U;
				uint64_t days(hours / 24U);
				hours %= 24U;
				if (days > 0U)
					std::fprintf(stdout, " %"PRIu64"d", days);
				if (days > 0 || hours > 0U)
					std::fprintf(stdout, " %"PRIu64"h", hours);
				if (days > 0 || hours > 0U || mins > 0U)
					std::fprintf(stdout, " %"PRIu64"m", mins);
				std::fprintf(stdout, " %"PRIu64"s ago", secs);
			}
			const char * const paused(status[16] ? ", paused" : "");
			const char * const want('u' == status[17] ? ", want up" : 'O' == status[17] ? ", once at most" : 'o' == status[17] ? ", once" : 'd' == status[17] ? ", want down" : "");
			std::fprintf(stdout, "%s%s", paused, want);
			const bool is_up(b < 20 ? p : encore_status_stopped != status[18]);
			if (is_up != initially_up) {
				std::fputs(", initially ", stdout);
				if (initially_up)
					std::fputs(b < 20 ? "up" : "started", stdout);
				else
					std::fputs(b < 20 ? "down" : "stopped", stdout);
			}
			std::fputc('\n', stdout);
		}
	}
	return EXIT_SUCCESS;
}
