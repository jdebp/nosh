/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

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
#include <unistd.h>
#include "utils.h"
#include "popt.h"
#include "listen.h"
#include "SignalManagement.h"

/* Support functions ********************************************************
// **************************************************************************
*/

namespace {
struct Client {
	Client() : off(0) {}

	struct initreq {
		enum { MAGIC = 0x03091969 };
		enum { RUNLVL = 1 };
		int magic;
		int cmd;
		int runlevel;
	};
	union {
		char buffer[384];
		initreq request;
	};
	std::size_t off;
} ;
}

/* Main function ************************************************************
// **************************************************************************
*/

// This must have static storage duration as we are using it in args.
static std::string runlevel_option;

void
initctl_read (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "");

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

	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	const unsigned listen_fds(query_listen_fds_or_daemontools(envs));
	if (1U > listen_fds) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "LISTEN_FDS", std::strerror(error));
		throw EXIT_FAILURE;
	}

	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGTSTP, SIGALRM, SIGPIPE, SIGQUIT, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGTSTP, SIGALRM, SIGPIPE, SIGQUIT, 0);

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	{
		std::vector<struct kevent> p(listen_fds + 7);
		for (unsigned i(0U); i < listen_fds; ++i)
			EV_SET(&p[i], LISTEN_SOCKET_FILENO + i, EVFILT_READ, EV_ADD, 0, 0, 0);
		EV_SET(&p[listen_fds + 0], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[listen_fds + 1], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[listen_fds + 2], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[listen_fds + 3], SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[listen_fds + 4], SIGALRM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[listen_fds + 5], SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[listen_fds + 6], SIGQUIT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		if (0 > kevent(queue, p.data(), listen_fds + 7, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	std::vector<Client> clients(listen_fds);
	bool in_shutdown(false);
	for (;;) {
		try {
			if (in_shutdown) break;
			struct kevent e;
			if (0 > kevent(queue, 0, 0, &e, 1, 0)) {
				const int error(errno);
				if (EINTR == error) continue;
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
				throw EXIT_FAILURE;
			}
			switch (e.filter) {
				case EVFILT_READ:
				{
					const int fd(e.ident);
					if (LISTEN_SOCKET_FILENO > fd || LISTEN_SOCKET_FILENO + static_cast<int>(listen_fds) <= fd) {
						std::fprintf(stderr, "%s: DEBUG: read event ident %lu\n", prog, e.ident);
						break;
					}
					Client & c(clients[fd]);
					const int n(read(fd, c.buffer + c.off, sizeof c.buffer - c.off));
					if (0 > n) {
						const int error(errno);
						std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "read", std::strerror(error));
						break;
					}
					if (0 == n)
						// FIXME: This incorrectly assumes one input fd.
						break;
					c.off += n;
					if (c.off < sizeof c.buffer)
						break;
					c.off = 0;
					if (c.request.MAGIC != c.request.magic) {
						std::fprintf(stderr, "%s: ERROR: %s\n", prog, "bad magic number in request");
						break;
					}
					if (c.request.cmd != c.request.RUNLVL) {
						std::fprintf(stderr, "%s: ERROR: %d: %s\n", prog, c.request.cmd, "unsupported command in request");
						break;
					}
					if (!std::isprint(c.request.runlevel)) {
						std::fprintf(stderr, "%s: ERROR: %d: %s\n", prog, c.request.runlevel, "unsupported run level in request");
						break;
					}
					const int pid(fork());
					if (0 > pid) {
						const int error(errno);
						std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
						break;
					}
					if (0 != pid)
						break;
					const char option[3] = { '-', static_cast<char>(c.request.runlevel), '\0' };
					runlevel_option = option;
					args.clear();
					args.insert(args.end(), "telinit");
					args.insert(args.end(), runlevel_option.c_str());
					args.insert(args.end(), 0);
					next_prog = arg0_of(args);
					return;
				}
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
							std::fprintf(stderr, "%s: DEBUG: signal event ident %lu fflags %x\n", prog, e.ident, e.fflags);
							break;
					}
					break;
				default:
					std::fprintf(stderr, "%s: DEBUG: event filter %hd ident %lu fflags %x\n", prog, e.filter, e.ident, e.fflags);
					break;
			}
		} catch (const std::exception & e) {
			std::fprintf(stderr, "%s: ERROR: exception: %s\n", prog, e.what());
		}
	}
	throw EXIT_SUCCESS;
}
