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

static 
bool 
process (
	const char * prog,
	const char * name,
	int fd
) {
	char buf[4096];
	bool done(false), bol(true);
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
			if (bol) {
				timespec now;
				clock_gettime(CLOCK_REALTIME, &now);
				const uint64_t secs(0x4000000000000000ULL + now.tv_sec + 10U);
				const uint32_t nano(now.tv_nsec);
				std::fprintf(stdout, "@%016" PRIx64 "%08" PRIx32 " ", secs, nano);
				bol = false;
			}
			std::fputc(c, stdout);
			bol = ('\n' == c);
		}
	}
	if (!bol && done)
		std::fputc('\n', stdout);
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
		popt::top_table_definition main_option(0, 0, "Main options", "file(s)");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) return EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		return static_cast<int>(EXIT_USAGE);
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
