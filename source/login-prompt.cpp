/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#if defined(__LINUX__) || defined(__linux__)
#include "kqueue_linux.h"
#else
#include <sys/types.h>
#include <sys/event.h>
#endif
#include "FileDescriptorOwner.h"
#include <unistd.h>
#include <termios.h>
#include "popt.h"
#include "utils.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
login_prompt ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, NULL, "Main options", "{prog}");

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

	write(STDOUT_FILENO, "Press ENTER to log on:", sizeof "Press ENTER to log on:" - 1);
	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}
	struct kevent p;
	EV_SET(&p, STDOUT_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
	const int rc(kevent(queue.get(), &p, 1, &p, 1, 0));
	if (0 > rc) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "stdin", std::strerror(error));
		throw EXIT_FAILURE;
	}
	tcflush(STDIN_FILENO, TCIFLUSH);
}
