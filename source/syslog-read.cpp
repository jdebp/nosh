/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <map>
#include <set>
#include <utility>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <ostream>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <unistd.h>
#include "utils.h"
#include "listen.h"

static const char * prog(0);

/* Support functions ********************************************************
// **************************************************************************
*/

static inline
void
process_message (
	int socket_fd
) {
	char msg[65536];	// RFC 5426 maximum legal size
	sockaddr_storage remoteaddr;
	socklen_t addrlen(sizeof remoteaddr);
	const int rc(recvfrom(socket_fd, &msg, sizeof msg, 0, reinterpret_cast<sockaddr *>(&remoteaddr), &addrlen));
	if (0 > rc) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "recv", std::strerror(error));
		return;
	}
	switch (remoteaddr.ss_family) {
		case AF_INET:
		{
			const struct sockaddr_in & remoteaddr4(*reinterpret_cast<const struct sockaddr_in *>(&remoteaddr));
			char ip[INET_ADDRSTRLEN];
			if (0 != inet_ntop(remoteaddr4.sin_family, &remoteaddr4.sin_addr, ip, sizeof ip))
				std::clog << ip << ':' << ntohs(remoteaddr4.sin_port) << ": ";
			break;
		}
		case AF_INET6:
		{
			const struct sockaddr_in6 & remoteaddr6(*reinterpret_cast<const struct sockaddr_in6 *>(&remoteaddr));
			char ip[INET6_ADDRSTRLEN];
			if (0 != inet_ntop(remoteaddr6.sin6_family, &remoteaddr6.sin6_addr, ip, sizeof ip))
				std::clog << ip << ':' << ntohs(remoteaddr6.sin6_port) << ": ";
			break;
		}
		default:
			break;
	}
	std::clog.write(msg, rc).put('\n');
}

/* Main function ************************************************************
// **************************************************************************
*/

void
syslog_read (
	const char * & /*next_prog*/,
	std::vector<const char *> & args
) {
	prog = basename_of(args[0]);

	const unsigned listen_fds(query_listen_fds());
	if (1U > listen_fds) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "LISTEN_FDS", std::strerror(error));
		throw EXIT_FAILURE;
	}

#if !defined(__LINUX__) && !defined(__linux__)
	struct sigaction sa;
	sa.sa_flags=0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler=SIG_IGN;
	sigaction(SIGHUP,&sa,NULL);
	sigaction(SIGTERM,&sa,NULL);
	sigaction(SIGINT,&sa,NULL);
	sigaction(SIGTSTP,&sa,NULL);
	sigaction(SIGALRM,&sa,NULL);
	sigaction(SIGPIPE,&sa,NULL);
	sigaction(SIGQUIT,&sa,NULL);
#endif

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
					if (LISTEN_SOCKET_FILENO <= static_cast<int>(e.ident) && LISTEN_SOCKET_FILENO + static_cast<int>(listen_fds) > static_cast<int>(e.ident))
						process_message(e.ident);
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
