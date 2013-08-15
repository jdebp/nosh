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
	const char * name,
	int fd
) {
	char buf[4096], s[16], n[8];
	size_t s_pos(0), n_pos(0), at(0);
	bool done(false);
	enum { BOL, STAMP, BODY } state = BOL;
	for (;;) {
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
						const uint64_t secs(convert(s, s_pos) - 0x4000000000000000ULL - 10U);
						const uint32_t nano(convert(n, n_pos));
						const std::time_t t(secs);
						if (const struct tm * tm = localtime(&t)) {
							char fmt[64];
							const int l(std::strftime(fmt, sizeof fmt, non_standard ? "%x %X" : "%F %T", tm));
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

int
main (
	int argc, 
	const char * argv[] 
) {
	if (argc < 1) return EXIT_FAILURE;
	const char * prog(basename_of(argv[0]));
	std::vector<const char *> args(argv, argv + argc);
	try {
		popt::bool_definition non_standard_option('l', "local-format", "Use the locale-specific local format for date and time.", non_standard);
		popt::definition * top_table[] = {
			&non_standard_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "file(s)");

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
		if (!process(prog, "<stdin>", STDIN_FILENO))
			return EXIT_TEMPORARY_FAILURE;
	} else {
		for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
			const char * name(*i);
			const int fd(open_read_at(AT_FDCWD, name));
			if (0 > fd) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
				return EXIT_PERMANENT_FAILURE;
			}
			if (!process(prog, name, fd))
				return EXIT_TEMPORARY_FAILURE;
			close(fd);
		}
	}
	return EXIT_SUCCESS;
}
