/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <vector>
#include <map>
#include <set>
#include <utility>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <sys/types.h>
#include "kqueue_common.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netinet/ip.h>
#include <netinet/in.h>
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
	int socket_fd
) {
	char msg[65536];	// RFC 5426 maximum legal size
	union {
		sockaddr_storage s;
		sockaddr_un u;
	} remoteaddr;
	socklen_t addrlen(sizeof remoteaddr);
	const int rc(recvfrom(socket_fd, &msg, sizeof msg, 0, reinterpret_cast<sockaddr *>(&remoteaddr), &addrlen));
	if (0 > rc) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "recvfrom", std::strerror(error));
		return;
	}
	switch (remoteaddr.s.ss_family) {
		case AF_INET:
		{
			const struct sockaddr_in & remoteaddr4(*reinterpret_cast<const struct sockaddr_in *>(&remoteaddr));
			char ip[INET_ADDRSTRLEN];
			if (nullptr != inet_ntop(remoteaddr4.sin_family, &remoteaddr4.sin_addr, ip, sizeof ip))
				std::cout << ip << ':' << ntohs(remoteaddr4.sin_port) << ": ";
			break;
		}
		case AF_INET6:
		{
			const struct sockaddr_in6 & remoteaddr6(*reinterpret_cast<const struct sockaddr_in6 *>(&remoteaddr));
			char ip[INET6_ADDRSTRLEN];
			if (nullptr != inet_ntop(remoteaddr6.sin6_family, &remoteaddr6.sin6_addr, ip, sizeof ip))
				std::cout << ip << ':' << ntohs(remoteaddr6.sin6_port) << ": ";
			break;
		}
		case AF_LOCAL:
		{
			if (addrlen > offsetof(sockaddr_un, sun_path) && remoteaddr.u.sun_path[0])
				std::cout << remoteaddr.u.sun_path << ": ";
			break;
		}
		default:
			break;
	}
	std::cout.write(msg, rc).put('\n').flush();
}

/* Main function ************************************************************
// **************************************************************************
*/

void
syslog_read [[gnu::noreturn]] (
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
		std::vector<struct kevent> p(listen_fds + 7);
		for (unsigned i(0U); i < listen_fds; ++i)
			set_event(p[i], LISTEN_SOCKET_FILENO + i, EVFILT_READ, EV_ADD, 0, 0, nullptr);
		set_event(p[listen_fds + 0], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		set_event(p[listen_fds + 1], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		set_event(p[listen_fds + 2], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		set_event(p[listen_fds + 3], SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		set_event(p[listen_fds + 4], SIGALRM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		set_event(p[listen_fds + 5], SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		set_event(p[listen_fds + 6], SIGQUIT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		if (0 > kevent(queue, p.data(), listen_fds + 7, nullptr, 0, nullptr)) {
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
