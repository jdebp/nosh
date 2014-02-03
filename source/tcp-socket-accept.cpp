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
#if !defined(__LINUX__) && !defined(__linux__)
#include <sys/event.h>
#else
#include <sys/poll.h>
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
#include "listen.h"

/* Main function ************************************************************
// **************************************************************************
*/

#if defined(__LINUX__) || defined(__linux__)
static sig_atomic_t child_signalled = false;

static
void
sig_child (
	int signo
) {
	if (SIGCHLD != signo) return;
	child_signalled = true;
}
#endif

static
void
sig_ignore (
	int 
) {
}

static
void
env (
	const char * name,
	const char * value
) {
	if (value)
		setenv(name, value, 1);
	else
		unsetenv(name);
}

static inline
const char *
q (
	const char * name
) {
	const char * value(std::getenv(name));
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
		int status;
		pid_t c = waitpid(-1, &status, WNOHANG);
		if (c <= 0) break;
		if (connections) {
			--connections;
			if (verbose)
				std::fprintf(stderr, "%s: %u ended status %i %lu/%lu\n", prog, c, status, connections, connection_limit);
		}
	}
}

void
tcp_socket_accept ( 
	const char * & next_prog,
	std::vector<const char *> & args
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
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "prog");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing next program.");
		throw EXIT_FAILURE;
	}

	const unsigned listen_fds(query_listen_fds());
	if (1U > listen_fds) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "LISTEN_FDS", std::strerror(error));
		throw EXIT_FAILURE;
	}

#if !defined(__LINUX__) && !defined(__linux__)
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

	{
		struct sigaction sa;
		sa.sa_flags=0;
		sigemptyset(&sa.sa_mask);
		sa.sa_handler=sig_ignore;
		sigaction(SIGCHLD,&sa,NULL);
		sigaction(SIGPIPE,&sa,NULL);
	}
#else
	std::vector<pollfd> p(listen_fds);
	for (unsigned i(0U); i < listen_fds; ++i) {
		p[i].fd = LISTEN_SOCKET_FILENO + i;
		p[i].events = 0;
	}

	sigset_t original_signals;
	sigprocmask(SIG_SETMASK, 0, &original_signals);
	sigset_t masked_signals(original_signals);
	sigaddset(&masked_signals, SIGCHLD);
	sigaddset(&masked_signals, SIGPIPE);
	sigprocmask(SIG_SETMASK, &masked_signals, 0);
	sigset_t masked_signals_during_poll(masked_signals);
	sigdelset(&masked_signals_during_poll, SIGCHLD);
	sigdelset(&masked_signals_during_poll, SIGPIPE);

	{
		struct sigaction sa;
		sa.sa_flags=0;
		sigemptyset(&sa.sa_mask);
		sa.sa_handler=sig_child;
		sigaction(SIGCHLD,&sa,NULL);
		sa.sa_handler=sig_ignore;
		sigaction(SIGPIPE,&sa,NULL);
	}
#endif

	unsigned long connections(0);

	for (;;) {
#if !defined(__LINUX__) && !defined(__linux__)
		for (unsigned i(0U); i < listen_fds; ++i)
			EV_SET(&p[i], LISTEN_SOCKET_FILENO + i, EVFILT_READ, connections < connection_limit ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		const int rc(kevent(queue, p.data(), listen_fds, p.data(), listen_fds + 2, 0));
#else
		if (child_signalled) {
			reap(prog, verbose, connections, connection_limit);
			child_signalled = false;
		}
		for (unsigned i(0U); i < listen_fds; ++i)
			if (connections < connection_limit)
				p[i].events |= POLLIN;
			else
				p[i].events &= ~POLLIN;
		const int rc(ppoll(p.data(), listen_fds, 0, &masked_signals_during_poll));
#endif
		if (0 > rc) {
			if (EINTR == errno) continue;
exit_error:
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
			throw EXIT_FAILURE;
		}
#if !defined(__LINUX__) && !defined(__linux__)
		for (size_t i(0); i < static_cast<std::size_t>(rc); ++i) 
#else
		for (size_t i(0); i < listen_fds; ++i) 
#endif
		{
#if !defined(__LINUX__) && !defined(__linux__)
			if (EVFILT_SIGNAL == p[i].filter) {
				switch (p[i].ident) {
					case SIGCHLD:
						reap(prog, verbose, connections, connection_limit);
						break;
				}
				continue;
			} else
			if (EVFILT_READ != p[i].filter) 
				continue;
			const int l(static_cast<int>(p[i].ident));
#else
			if (!(p[i].revents & POLLIN)) continue;
			const int l(p[i].fd);
#endif

			sockaddr_storage remoteaddr;
			socklen_t remoteaddrsz = sizeof remoteaddr;
			const int s(accept(l, reinterpret_cast<sockaddr *>(&remoteaddr), &remoteaddrsz));
			if (0 > s) goto exit_error;

			const int child(fork());
			if (0 > child) goto exit_error;
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
#if defined(IP_OPTIONS)
			if (!no_kill_IP_options) {
				if (0 > setsockopt(s, IPPROTO_IP, IP_OPTIONS, 0, 0)) goto exit_error ;
			}
#endif
			if (no_delay) {
				const int on = 1;
				if (0 > setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on)) goto exit_error ;
			}

			sockaddr_storage localaddr;
			socklen_t localaddrsz = sizeof localaddr;
			if (0 > getsockname(s, reinterpret_cast<sockaddr *>(&localaddr), &localaddrsz)) goto exit_error;

			close(LISTEN_SOCKET_FILENO);
			if (0 > dup2(s, STDIN_FILENO)) goto exit_error;
			if (0 > dup2(s, STDOUT_FILENO)) goto exit_error;
			if (s != STDIN_FILENO && s != STDOUT_FILENO)
				close(s);

			env("PROTO", "TCP");
			switch (localaddr.ss_family) {
				case AF_INET:
				{
					const struct sockaddr_in & localaddr4(*reinterpret_cast<const struct sockaddr_in *>(&localaddr));
					char port[64], ip[INET_ADDRSTRLEN];
					if (0 == inet_ntop(localaddr4.sin_family, &localaddr4.sin_addr, ip, sizeof ip)) goto exit_error;
					snprintf(port, sizeof port, "%u", ntohs(localaddr4.sin_port));
					env("TCPLOCALIP", ip);
					env("TCPLOCALPORT", port);
					break;
				}
				case AF_INET6:
				{
					const struct sockaddr_in6 & localaddr6(*reinterpret_cast<const struct sockaddr_in6 *>(&localaddr));
					char port[64], ip[INET6_ADDRSTRLEN];
					if (0 == inet_ntop(localaddr6.sin6_family, &localaddr6.sin6_addr, ip, sizeof ip)) goto exit_error;
					snprintf(port, sizeof port, "%u", ntohs(localaddr6.sin6_port));
					env("TCPLOCALIP", ip);
					env("TCPLOCALPORT", port);
					break;
				}
				default:
					env("TCPLOCALIP", 0);
					env("TCPLOCALPORT", 0);
					break;
			}
			switch (remoteaddr.ss_family) {
				case AF_INET:
				{
					const struct sockaddr_in & remoteaddr4(*reinterpret_cast<const struct sockaddr_in *>(&remoteaddr));
					char port[64], ip[INET_ADDRSTRLEN];
					if (0 == inet_ntop(remoteaddr4.sin_family, &remoteaddr4.sin_addr, ip, sizeof ip)) goto exit_error;
					snprintf(port, sizeof port, "%u", ntohs(remoteaddr4.sin_port));
					env("TCPREMOTEIP", ip);
					env("TCPREMOTEPORT", port);
					break;
				}
				case AF_INET6:
				{
					const struct sockaddr_in6 & remoteaddr6(*reinterpret_cast<const struct sockaddr_in6 *>(&remoteaddr));
					char port[64], ip[INET6_ADDRSTRLEN];
					if (0 == inet_ntop(remoteaddr6.sin6_family, &remoteaddr6.sin6_addr, ip, sizeof ip)) goto exit_error;
					snprintf(port, sizeof port, "%u", ntohs(remoteaddr6.sin6_port));
					env("TCPREMOTEIP", ip);
					env("TCPREMOTEPORT", port);
					break;
				}
				default:
					env("TCPREMOTEIP", 0);
					env("TCPREMOTEPORT", 0);
					break;
			}
			env("TCPLOCALHOST", localname);
			env("TCPLOCALINFO", 0);
			env("TCPREMOTEHOST", 0);
			env("TCPREMOTEINFO", 0);

			if (verbose)
				std::fprintf(stderr, "%s: %u %s %s %s %s\n", prog, getpid(), q("TCPLOCALIP"), q("TCPLOCALPORT"), q("TCPREMOTEIP"), q("TCPREMOTEPORT"));

#if !defined(__LINUX__) && !defined(__linux__)
			{
				struct sigaction sa;
				sa.sa_flags=0;
				sigemptyset(&sa.sa_mask);
				sa.sa_handler=SIG_DFL;
				sigaction(SIGCHLD,&sa,NULL);
				sigaction(SIGPIPE,&sa,NULL);
			}
#else
			{
				struct sigaction sa;
				sa.sa_flags=0;
				sigemptyset(&sa.sa_mask);
				sa.sa_handler=SIG_DFL;
				sigaction(SIGCHLD,&sa,NULL);
				sigaction(SIGPIPE,&sa,NULL);
			}
			sigprocmask(SIG_SETMASK, &original_signals, 0);
#endif
			return;
		}
	}
}
