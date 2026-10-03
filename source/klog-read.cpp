/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <vector>
#include <map>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <ostream>
#include <sys/types.h>
#if defined(__LINUX__) || defined(__linux__)
#include "kqueue_linux.h"
#else
#include <sys/event.h>
#endif
#include "kqueue_common.h"
#include <unistd.h>
#include "utils.h"
#include "listen.h"
#include "popt.h"
#include "SignalManagement.h"

/* Support functions ********************************************************
// **************************************************************************
*/

static inline
void
process_message (
	const char * prog,
	int fifo_fd
) {
	char msg[65536];
	const int rc(read(fifo_fd, &msg, sizeof msg));
	if (0 > rc) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "read", std::strerror(error));
		return;
	}
	std::cout.write(msg, rc).flush();
}

/* Main function ************************************************************
// **************************************************************************
*/

void
klog_read [[gnu::noreturn]] (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, nullptr, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	const unsigned listen_fds(query_listen_fds_or_daemontools(envs));
	if (1U > listen_fds) {
		die_errno(prog, envs, "LISTEN_FDS");
	}

	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGTSTP, SIGALRM, SIGPIPE, SIGQUIT, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGTSTP, SIGALRM, SIGPIPE, SIGQUIT, 0);

	const int queue(kqueue());
	if (0 > queue) {
		die_errno(prog, envs, "kqueue");
	}

	{
		std::vector<struct kevent> p;
		for (unsigned i(0U); i < listen_fds; ++i)
			append_event(p, LISTEN_SOCKET_FILENO + i, EVFILT_READ, EV_ADD, 0, 0, nullptr);
		append_event(p, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		append_event(p, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		append_event(p, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		append_event(p, SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		append_event(p, SIGALRM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		append_event(p, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		append_event(p, SIGQUIT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		if (0 > kevent(queue, p.data(), p.size(), nullptr, 0, nullptr)) {
			die_errno(prog, envs, "kevent");
		}
	}

	bool in_shutdown(false);
	for (;;) {
		try {
			if (in_shutdown) break;
			struct kevent e;
			if (0 > kevent(queue, nullptr, 0, &e, 1, nullptr)) {
				if (EINTR == errno) continue;
				die_errno(prog, envs, "kevent");
			}
			switch (e.filter) {
				case EVFILT_READ:
					if (LISTEN_SOCKET_FILENO <= static_cast<int>(e.ident) && LISTEN_SOCKET_FILENO + static_cast<int>(listen_fds) > static_cast<int>(e.ident))
						process_message(prog, e.ident);
					else
						std::fprintf(stderr, "%s: DEBUG: read event ident %lu\n", prog, e.ident);
					break;
				case EVFILT_SIGNAL:
					switch (e.ident) {
						case SIGHUP:
						case SIGTERM:
						case SIGINT:
						case SIGPIPE:
						case SIGQUIT:
							in_shutdown = true;
							break;
						case SIGTSTP:
							std::fprintf(stderr, "%s: INFO: %s\n", prog, "Paused.");
							raise(SIGSTOP);
							std::fprintf(stderr, "%s: INFO: %s\n", prog, "Continued.");
							break;
						case SIGALRM:
						default:
#if defined(DEBUG)
							std::fprintf(stderr, "%s: DEBUG: signal event ident %lu fflags %x\n", prog, e.ident, e.fflags);
#endif
							break;
					}
					break;
				default:
#if defined(DEBUG)
					std::fprintf(stderr, "%s: DEBUG: event filter %hd ident %lu fflags %x\n", prog, e.filter, e.ident, e.fflags);
#endif
					break;
			}
		} catch (const std::exception & e) {
			std::fprintf(stderr, "%s: ERROR: exception: %s\n", prog, e.what());
		}
	}
	throw EXIT_SUCCESS;
}
