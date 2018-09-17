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
	const ProcessEnvironment & envs,
	const char * name,
	int fd
) {
	char buf[4096];
	bool done(false), bol(true);
	for (;;) {
		if (bol)
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
			if (bol) {
				timespec now;
				clock_gettime(CLOCK_REALTIME, &now);
				const uint64_t secs(time_to_tai64(envs, TimeTAndLeap(now.tv_sec, false)));
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

void
tai64n [[gnu::noreturn]] (
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "[file(s)...]");

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
