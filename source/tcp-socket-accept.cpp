/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <sys/types.h>
#if defined(__LINUX__) || defined(__linux__)
#include "kqueue_linux.h"
#else
#include <sys/event.h>
#endif
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <unistd.h>
#include <grp.h>
#include "popt.h"
#include "utils.h"
#include "ProcessEnvironment.h"
#include "listen.h"
#include "SignalManagement.h"

/* Helper functions *********************************************************
// **************************************************************************
*/

static sig_atomic_t child_signalled = false;

static
void
handle_signal (
	int signo
) {
	if (SIGCHLD != signo) return;
	child_signalled = true;
}

static inline
const char *
q (
	const ProcessEnvironment & envs,
	const char * name
) {
	const char * value(envs.query(name));
	return value ? value : "";
}

static inline
void
reap (
	const char * prog,
	bool verbose,
	unsigned long & connections,
	unsigned long connection_limit
) {
	for (;;) {
		int status, code;
		pid_t c;
		if (0 >= wait_nonblocking_for_anychild_exit(c, status, code)) break;
		if (connections) {
			--connections;
			if (verbose)
				std::fprintf(stderr, "%s: %u ended status %i code %i %lu/%lu\n", prog, c, status, code, connections, connection_limit);
		}
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
tcp_socket_accept ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	unsigned long connection_limit = 40U;
	const char * localname = 0;
	bool verbose(false);
	bool keepalives(false);
#if defined(IP_OPTIONS)
	bool no_kill_IP_options(false);
#endif
	bool no_delay(false);
	try {
		popt::bool_definition verbose_option('v', "verbose", "Print status information.", verbose);
		popt::bool_definition keepalives_option('k', "keepalives", "Enable TCP keepalive processing.", keepalives);
#if defined(IP_OPTIONS)
		popt::bool_definition no_kill_IP_options_option('o', "no-kill-IP-options", "Allow a client to set source routes.", no_kill_IP_options);
#endif
		popt::bool_definition no_delay_option('D', "no-delay", "Disable the TCP delay algorithm.", no_delay);
		popt::unsigned_number_definition connection_limit_option('c', "connection-limit", "number", "Specify the limit on the number of simultaneous parallel connections.", connection_limit, 0);
		popt::string_definition localname_option('l', "localname", "hostname", "Override the local host name.", localname);
		popt::definition * top_table[] = {
			&verbose_option,
			&keepalives_option,
#if defined(IP_OPTIONS)
			&no_kill_IP_options_option,
#endif
			&no_delay_option,
			&connection_limit_option,
			&localname_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog}");

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

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing next program.");
		throw static_cast<int>(EXIT_USAGE);
	}

	const unsigned listen_fds(query_listen_fds_or_daemontools(envs));
	if (1U > listen_fds) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "LISTEN_FDS", std::strerror(error));
		throw EXIT_FAILURE;
	}

	ReserveSignalsForKQueue kqueue_reservation(SIGPIPE, SIGCHLD, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGPIPE, 0);

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	std::vector<struct kevent> p(listen_fds + 2);
	for (unsigned i(0U); i < listen_fds; ++i)
		EV_SET(&p[i], LISTEN_SOCKET_FILENO + i, EVFILT_READ, EV_ADD, 0, 0, 0);
	EV_SET(&p[listen_fds + 0], SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[listen_fds + 1], SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	if (0 > kevent(queue, p.data(), listen_fds + 2, 0, 0, 0)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
		throw EXIT_FAILURE;
	}

	unsigned long connections(0);

	for (;;) {
		if (child_signalled) {
			reap(prog, verbose, connections, connection_limit);
			child_signalled = false;
		}
		for (unsigned i(0U); i < listen_fds; ++i)
			EV_SET(&p[i], LISTEN_SOCKET_FILENO + i, EVFILT_READ, connections < connection_limit ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		const int rc(kevent(queue, p.data(), listen_fds, p.data(), listen_fds + 2, 0));
		if (0 > rc) {
			if (EINTR == errno) continue;
exit_error:
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
			throw EXIT_FAILURE;
		}
		for (size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			if (EVFILT_SIGNAL == e.filter) {
				handle_signal (e.ident);
				continue;
			} else
			if (EVFILT_READ != e.filter) 
				continue;
			const int l(static_cast<int>(e.ident));

			sockaddr_storage remoteaddr;
			socklen_t remoteaddrsz = sizeof remoteaddr;
			const int s(accept(l, reinterpret_cast<sockaddr *>(&remoteaddr), &remoteaddrsz));
			if (0 > s) {
				const int error(errno);
				if (ECONNABORTED == error) {
					std::fprintf(stderr, "%s: ERROR: %s\n", prog, std::strerror(error));
					close(s);
					continue;
				}
				goto exit_error;
			}

			const int child(fork());
			if (0 > child) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s\n", prog, std::strerror(error));
				close(s);
				continue;
			}
			if (0 != child) {
				++connections;
				if (verbose)
					std::fprintf(stderr, "%s: %u started %lu/%lu\n", prog, child, connections, connection_limit);
				close(s);
				continue;
			}

			if (keepalives) {
				const int on = 1;
				if (0 > setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof on)) goto exit_error ;
			}
			if (no_delay) {
				const int on = 1;
				if (0 > setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on)) goto exit_error ;
			}
#if defined(IP_OPTIONS)
			if (!no_kill_IP_options) {
				switch (remoteaddr.ss_family) {
					case AF_INET:
						if (0 > setsockopt(s, IPPROTO_IP, IP_OPTIONS, 0, 0)) goto exit_error ;
						break;
					default:
						break;
				}
			}
#endif

			sockaddr_storage localaddr;
			socklen_t localaddrsz = sizeof localaddr;
			if (0 > getsockname(s, reinterpret_cast<sockaddr *>(&localaddr), &localaddrsz)) goto exit_error;

			for (unsigned j(0U); j < listen_fds; ++j)
				close(LISTEN_SOCKET_FILENO + j);
			if (0 > dup2(s, STDIN_FILENO)) goto exit_error;
			if (0 > dup2(s, STDOUT_FILENO)) goto exit_error;
			if (s != STDIN_FILENO && s != STDOUT_FILENO)
				close(s);

			envs.set("PROTO", "TCP");
			switch (localaddr.ss_family) {
				case AF_INET:
				{
					const struct sockaddr_in & localaddr4(*reinterpret_cast<const struct sockaddr_in *>(&localaddr));
					char port[64], ip[INET_ADDRSTRLEN];
					if (0 == inet_ntop(localaddr4.sin_family, &localaddr4.sin_addr, ip, sizeof ip)) goto exit_error;
					snprintf(port, sizeof port, "%u", ntohs(localaddr4.sin_port));
					envs.set("TCPLOCALIP", ip);
					envs.set("TCPLOCALPORT", port);
					break;
				}
				case AF_INET6:
				{
					const struct sockaddr_in6 & localaddr6(*reinterpret_cast<const struct sockaddr_in6 *>(&localaddr));
					char port[64], ip[INET6_ADDRSTRLEN];
					if (0 == inet_ntop(localaddr6.sin6_family, &localaddr6.sin6_addr, ip, sizeof ip)) goto exit_error;
					snprintf(port, sizeof port, "%u", ntohs(localaddr6.sin6_port));
					envs.set("TCPLOCALIP", ip);
					envs.set("TCPLOCALPORT", port);
					break;
				}
				default:
					envs.set("TCPLOCALIP", 0);
					envs.set("TCPLOCALPORT", 0);
					break;
			}
			switch (remoteaddr.ss_family) {
				case AF_INET:
				{
					const struct sockaddr_in & remoteaddr4(*reinterpret_cast<const struct sockaddr_in *>(&remoteaddr));
					char port[64], ip[INET_ADDRSTRLEN];
					if (0 == inet_ntop(remoteaddr4.sin_family, &remoteaddr4.sin_addr, ip, sizeof ip)) goto exit_error;
					snprintf(port, sizeof port, "%u", ntohs(remoteaddr4.sin_port));
					envs.set("TCPREMOTEIP", ip);
					envs.set("TCPREMOTEPORT", port);
					break;
				}
				case AF_INET6:
				{
					const struct sockaddr_in6 & remoteaddr6(*reinterpret_cast<const struct sockaddr_in6 *>(&remoteaddr));
					char port[64], ip[INET6_ADDRSTRLEN];
					if (0 == inet_ntop(remoteaddr6.sin6_family, &remoteaddr6.sin6_addr, ip, sizeof ip)) goto exit_error;
					snprintf(port, sizeof port, "%u", ntohs(remoteaddr6.sin6_port));
					envs.set("TCPREMOTEIP", ip);
					envs.set("TCPREMOTEPORT", port);
					break;
				}
				default:
					envs.set("TCPREMOTEIP", 0);
					envs.set("TCPREMOTEPORT", 0);
					break;
			}
			envs.set("TCPLOCALHOST", localname);
			envs.set("TCPLOCALINFO", 0);
			envs.set("TCPREMOTEHOST", 0);
			envs.set("TCPREMOTEINFO", 0);

			if (verbose)
				std::fprintf(stderr, "%s: %u %s %s %s %s\n", prog, getpid(), q(envs, "TCPLOCALIP"), q(envs, "TCPLOCALPORT"), q(envs, "TCPREMOTEIP"), q(envs, "TCPREMOTEPORT"));

			return;
		}
	}
}
