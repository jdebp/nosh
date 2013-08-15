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
#include "service-manager.h"

/* Main function ************************************************************
// **************************************************************************
*/

static
std::string
to_json_string (
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		const char c(*p);
		if ('\\' == c || '\"' == c)
			r += '\\';
		r += c;
	}
	return "\"" + r + "\"";
}

static bool json(false);
static char inner_comma(' ');
static char outer_comma(' ');

static
void
write_document_start()
{
	if (json) {
		std::fputs("{\n", stdout);
		outer_comma = ' ';
	}
}

static
void
write_document_end()
{
	if (json)
		std::fputs("}\n", stdout);
}

static
void
write_section_start(
	const char * name
) {
	if (json) {
		std::fprintf(stdout, "%c%s:{", outer_comma, to_json_string(name).c_str());
		outer_comma = ',';
		inner_comma = ' ';
	} else
		std::fprintf(stdout, "[%s]\n", name);
}

static
void
write_section_end()
{
	if (json)
		std::fputs("}\n", stdout);
	else
		std::fputc('\n', stdout);
}

static
void
write_boolean_value(
	const char * name,
	bool value
) {
	if (json) {
		std::fprintf(stdout, "%c%s:%s", inner_comma, to_json_string(name).c_str(), value ? "true": "false");
		inner_comma = ',';
	} else
		std::fprintf(stdout, "%s=%s\n", name, value ? "yes" : "no");
}

static
void
write_string_value(
	const char * name,
	const char * value
) {
	if (json) {
		std::fprintf(stdout, "%c%s:%s", inner_comma, to_json_string(name).c_str(), to_json_string(value).c_str());
		inner_comma = ',';
	} else
		std::fprintf(stdout, "%s=%s\n", name, value);
}

static
void
write_numeric_int_value(
	const char * name,
	int value
) {
	if (json) {
		std::fprintf(stdout, "%c%s:%d", inner_comma, to_json_string(name).c_str(), value);
		inner_comma = ',';
	} else
		std::fprintf(stdout, "%s=%d\n", name, value);
}

static
void
write_numeric_uint64_value(
	const char * name,
	uint_least64_t value
) {
	if (json) {
		std::fprintf(stdout, "%c%s:%"PRIu64, inner_comma, to_json_string(name).c_str(), value);
		inner_comma = ',';
	} else
		std::fprintf(stdout, "%s=%"PRIu64"\n", name, value);
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
		popt::bool_definition json_option('\0', "json", "Output in JSON format.", json);
		popt::definition * top_table[] = {
			&json_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "directory");

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

	write_document_start();
	for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
		const char * name(*i);
		int dir_fd(open_dir_at(AT_FDCWD, name));
		if (0 > dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: %s\n", name, std::strerror(error));
			continue;
		}
		struct stat sbuf;
		const bool initially_up(0 > fstatat(dir_fd, "down", &sbuf, 0) && ENOENT == errno);
		int ok_fd(open_writeexisting_at(dir_fd, "ok"));
		if (0 > ok_fd) {
			const int old_dir_fd(dir_fd);
			dir_fd = open_dir_at(dir_fd, "supervise");
			close(old_dir_fd);
			if (0 > dir_fd) {
				const int error(errno);
				std::fprintf(stderr, "%s: %s: %s\n", name, "supervise", std::strerror(error));
				continue;
			}
			ok_fd = open_writeexisting_at(dir_fd, "ok");
			if (0 > ok_fd) {
				const int error(errno);
				close(dir_fd);
				if (ENXIO == error)
					std::fprintf(stderr, "%s: No supervisor is running\n", name);
				else
					std::fprintf(stderr, "%s: %s: %s\n", name, "supervise/ok", std::strerror(error));
				continue;
			}
		}
		close(ok_fd), ok_fd = -1;
		int status_fd(open_read_at(dir_fd, "status"));
		close(dir_fd), dir_fd = -1;
		if (0 > status_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: %s: %s\n", name, "status", std::strerror(error));
			continue;
		}
		char status[20];
		const int b(read(status_fd, status, sizeof status));
		close(status_fd), status_fd = -1;
		write_section_start(name);
		if (b < 18)
			write_boolean_value("Loading", true);
		else {
			const uint64_t s(unpack_bigendian(status, 8));
//			const uint32_t n(unpack_bigendian(status + 8, 4));
			const uint32_t p(unpack_littleendian(status + 12, 4));

			if (b < 20) {
				// supervise doesn't turn off the want flag.
				if (p) {
					if ('u' == status[17]) status[17] = '\0';
				} else {
					if ('d' == status[17]) status[17] = '\0';
				}
			}
			write_string_value("DaemontoolsState", p ? "up" : "down");
			if (b >= 20)
				write_string_value("DaemontoolsEncoreState", state_of(status[18]));
			write_numeric_int_value("MainPID", p);
			const uint64_t z(s - 0x4000000000000000ULL - 10U);
			write_numeric_uint64_value("Timestamp", z);
			const char * const want('u' == status[17] ? "up" : 'O' == status[17] ? "once at most" : 'o' == status[17] ? "once" : 'd' == status[17] ? "down" : "nothing");
			write_string_value("Want", want);
			write_boolean_value("Paused", status[16]);
			write_boolean_value("Enabled", initially_up);
		}
		write_section_end();
	}
	write_document_end();
	return EXIT_SUCCESS;
}
