/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>
#include <stdint.h>
#include <cctype>
#include <cerrno>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"

static bool non_standard(false);

static 
void 
finish (
	std::size_t at,
	const char * s,
	std::size_t s_len,
	const char * n,
	std::size_t n_len
) {
	if (at) std::fputc('@', stdout);
	std::fwrite(s, s_len, 1, stdout);
	std::fwrite(n, n_len, 1, stdout);
}

static inline 
int 
x2d ( int c ) 
{
	if (std::isdigit(c)) return c - '0';
	if (std::isalpha(c)) {
		c = std::tolower(c);
		return c - 'a' + 10;
	}
	return EOF;
}

static
uint64_t
convert (
	const char * buf,
	std::size_t len
) {
	uint64_t r(0U);
	while (len) {
		--len;
		r <<= 4;
		r |= x2d(*buf++);
	}
	return r;
}

static 
bool 
process (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * name,
	int fd
) {
	char buf[32768], s[16], n[8];
	size_t s_pos(0), n_pos(0), at(0);
	bool done(false);
	enum { BOL, STAMP, BODY } state = BOL;
	for (;;) {
		if (BOL == state)
			std::fflush(stdout);
		const int rd(read(fd, buf, sizeof buf));
		if (0 > rd) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
			return false;
		} else if (0 == rd)
			break;
		done = true;
		for (int i(0); i < rd; ++i) {
			const char c(buf[i]);
			switch (state) {
				body:
				case BODY:
					std::fputc(c, stdout);
					if ('\n' == c) state = BOL;
					break;;
				case BOL:
					if ('@' == c) {
						state = STAMP;
						s_pos = n_pos = 0;
						at = 1;
					} else {
						finish(at, s, s_pos, n, s_pos);
						at = s_pos = n_pos = 0;
						state = BODY;
						goto body;
					}
					break;
				case STAMP:
					if (s_pos < sizeof s/sizeof *s) {
						if (std::isxdigit(c)) {
							s[s_pos++] = c;
						} else {
							finish(at, s, s_pos, n, n_pos);
							at = s_pos = n_pos = 0;
							state = BODY;
							goto body;
						}
					} else
					if (n_pos < sizeof n/sizeof *n) {
						if (std::isxdigit(c)) {
							n[n_pos++] = c;
						} else {
							finish(at, s, s_pos, n, n_pos);
							at = s_pos = n_pos = 0;
							state = BODY;
							goto body;
						}
					} else
					{
						const TimeTAndLeap z(tai64_to_time(envs, convert(s, s_pos)));
						const uint32_t nano(convert(n, n_pos));
						struct tm tm;
						if (localtime_r(&z.time, &tm)) {
							if (z.leap) ++tm.tm_sec;
							char fmt[64];
							const int l(std::strftime(fmt, sizeof fmt, non_standard ? "%x %X" : "%F %T", &tm));
							std::fwrite(fmt, l, 1, stdout);
							std::fprintf(stdout, ".%09" PRIu32, nano);
						} else
							finish(at, s, s_pos, n, n_pos);
						at = s_pos = n_pos = 0;
						state = BODY;
						goto body;
					}
					break;
			}
		}
	}
	if (BOL != state && done) {
		finish(at, s, s_pos, n, n_pos);
		std::fputc('\n', stdout);
	}
	return true;
}

/* Main function ************************************************************
// **************************************************************************
*/

void
tai64nlocal [[gnu::noreturn]] (
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition non_standard_option('l', "local-format", "Use the locale-specific local format for date and time.", non_standard);
		popt::definition * top_table[] = {
			&non_standard_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "[file(s)...]");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}
	if (args.empty()) {
		if (!process(prog, envs, "<stdin>", STDIN_FILENO))
			throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
	} else {
		for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
			const char * name(*i);
			const int fd(open_read_at(AT_FDCWD, name));
			if (0 > fd) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);	// Bernstein daemontools compatibility
			}
			if (!process(prog, envs, name, fd))
				throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
			close(fd);
		}
	}
	throw EXIT_SUCCESS;
}
