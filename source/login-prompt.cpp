/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
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
#include "kqueue_common.h"
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
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, nullptr, "Main options", "{prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	write(STDOUT_FILENO, "Press ENTER to log on:", sizeof "Press ENTER to log on:" - 1);
	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		die_errno(prog, envs, "kqueue");
	}
	struct kevent p;
	set_event(p, STDOUT_FILENO, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	const int rc(kevent(queue.get(), &p, 1, &p, 1, nullptr));
	if (0 > rc) {
		die_errno(prog, envs, "stdin");
	}
	tcflush(STDIN_FILENO, TCIFLUSH);
}
