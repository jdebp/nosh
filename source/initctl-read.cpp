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
#include <unistd.h>
#include "utils.h"
#include "kqueue_common.h"
#if defined(SIGPWR)
#include <fcntl.h>
#include "fdutils.h"
#endif
#include "popt.h"
#include "listen.h"
#include "SignalManagement.h"
#include "FileDescriptorOwner.h"

/* Support functions ********************************************************
// **************************************************************************
*/

namespace {

struct Client {
	Client() : off(0) {}

	// We roll our own cross-platform definition of this structure, with the same constants.
	// We do not rely upon a Linux-only header, that is only even present when one optional software is installed on the system, for it.
	struct initreq {
		enum { MAGIC = 0x03091969 };
		enum { RUNLVL = 1, POWER_LOW = 2, POWER_FAIL = 3, POWER_OK = 4 };
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

bool
fork_runlevel_worker (
	const char * prog,
	const char * & next_prog,
	std::vector<const char *> & args,
	int runlevel
) {
	if (!std::isprint(runlevel)) {
		std::fprintf(stderr, "%s: ERROR: %d: %s\n", prog, runlevel, "unsupported run level in request");
		return false;
	}
	const pid_t pid(fork());
	if (0 > pid) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
		return false;
	}
	if (0 != pid)
		return false;
	const char option[3] = { '-', static_cast<char>(runlevel), '\0' };
	// This must have static storage duration as we are using it in args.
	static std::string runlevel_option;
	runlevel_option = option;
	args.clear();
	args.push_back("telinit");
	args.push_back(runlevel_option.c_str());
	next_prog = arg0_of(args);
	return true;
}

#if defined(SIGPWR)
void
powerstatus_event (
	const char * prog,
	char type
) {
	const FileDescriptorOwner fd(open_writecreate_at(AT_FDCWD, "/run/powerstatus", 0640));
	if (0 > fd.get()) {
fail:
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "/run/powerstatus", std::strerror(error));
		return;
	}
	if (0 > pwrite(fd.get(), &type, sizeof type, 0)) goto fail;
	if (0 > kill(1, SIGPWR)) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "failed to signal init", std::strerror(error));
		return;
	}
}
#endif

}

/* Main function ************************************************************
// **************************************************************************
*/

void
initctl_read (
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

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		die_errno(prog, envs, "kqueue");
	}

	std::vector<struct kevent> ip;
	for (unsigned i(0U); i < listen_fds; ++i)
		append_event(ip, LISTEN_SOCKET_FILENO + i, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGALRM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGQUIT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);

	std::vector<Client> clients(listen_fds);
	bool in_shutdown(false);
	for (;;) {
		try {
			if (in_shutdown) break;
			struct kevent e;
			const int rc(kevent(queue.get(), ip.data(), ip.size(), &e, 1, nullptr));
			ip.clear();
			if (0 > rc) {
				if (EINTR == errno) continue;
				die_errno(prog, envs, "kevent");
			} else
			if (1 > rc) continue;
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
						break;
					c.off += n;
					if (c.off < sizeof c.buffer)
						break;
					c.off = 0;
					if (c.request.MAGIC != c.request.magic) {
						std::fprintf(stderr, "%s: ERROR: %s\n", prog, "bad magic number in request");
					} else
					if (c.request.RUNLVL == c.request.cmd) {
						if (fork_runlevel_worker(prog, next_prog, args, c.request.runlevel)) return;
					} else
#if defined(SIGPWR)
					if (c.request.POWER_LOW == c.request.cmd) {
						powerstatus_event(prog, 'L');
					} else
					if (c.request.POWER_FAIL == c.request.cmd) {
						powerstatus_event(prog, 'F');
					} else
					if (c.request.POWER_OK == c.request.cmd) {
						powerstatus_event(prog, 'O');
					} else
#endif
					{
						std::fprintf(stderr, "%s: ERROR: %d: %s\n", prog, c.request.cmd, "unsupported command in request");
					}
					break;
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
