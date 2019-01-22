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
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
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
		pid_t child;
		if (0 >= wait_nonblocking_for_anychild_exit(child, status, code)) break;
		if (connections) {
			--connections;
			if (verbose)
				std::fprintf(stderr, "%s: %u ended status %i code %i %lu/%lu\n", prog, child, status, code, connections, connection_limit);
		}
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
local_stream_socket_accept ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	unsigned long connection_limit = 40U;
	const char * localname = 0;
	bool verbose(false);
	try {
		popt::bool_definition verbose_option('v', "verbose", "Print status information.", verbose);
		popt::unsigned_number_definition connection_limit_option('c', "connection-limit", "number", "Specify the limit on the number of simultaneous parallel connections.", connection_limit, 0);
		popt::string_definition localname_option('l', "localname", "pathname", "Override the local name.", localname);
		popt::definition * top_table[] = {
			&verbose_option,
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

	ReserveSignalsForKQueue kqueue_reservation(SIGCHLD, 0);
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

			union {
				sockaddr_storage s;
				sockaddr_un u;
			} remoteaddr;
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

			union {
				sockaddr_storage s;
				sockaddr_un u;
			} localaddr;
			socklen_t localaddrsz = sizeof localaddr;
			if (0 > getsockname(s, reinterpret_cast<sockaddr *>(&localaddr), &localaddrsz)) goto exit_error;

			for (unsigned j(0U); j < listen_fds; ++j)
				close(LISTEN_SOCKET_FILENO + j);
			if (0 > dup2(s, STDIN_FILENO)) goto exit_error;
			if (0 > dup2(s, STDOUT_FILENO)) goto exit_error;
			if (s != STDIN_FILENO && s != STDOUT_FILENO)
				close(s);

			envs.set("PROTO", "UNIX");
			switch (localaddr.u.sun_family) {
				case AF_LOCAL:
					if (!localname && localaddrsz > offsetof(sockaddr_un, sun_path) && localaddr.u.sun_path[0])
						localname = localaddr.u.sun_path;
					break;
				default:
					break;
			}
			envs.set("UNIXLOCALPATH", localname);
			envs.set("UNIXLOCALUID", 0);
			envs.set("UNIXLOCALGID", 0);
			envs.set("UNIXLOCALPID", 0);
			switch (remoteaddr.u.sun_family) {
				case AF_LOCAL:
				{
					if (remoteaddrsz > offsetof(sockaddr_un, sun_path) && remoteaddr.u.sun_path[0])
						envs.set("UNIXREMOTEPATH", remoteaddr.u.sun_path);
					else
						envs.set("UNIXREMOTEPATH", 0);
#if defined(SO_PEERCRED)
#if defined(__LINUX__) || defined(__linux__)
					struct ucred u;
#elif defined(__OpenBSD__)
					struct sockpeercred u;
#else
#error "Don't know how to do SO_PEERCRED on your platform."
#endif
					socklen_t ul = sizeof u;
					if (0 > getsockopt(STDIN_FILENO, SOL_SOCKET, SO_PEERCRED, &u, &ul)) goto exit_error;
					char buf[64];
					snprintf(buf, sizeof buf, "%u", u.pid);
					envs.set("UNIXREMOTEPID", buf);
					snprintf(buf, sizeof buf, "%u", u.gid);
					envs.set("UNIXREMOTEEGID", buf);
					snprintf(buf, sizeof buf, "%u", u.uid);
					envs.set("UNIXREMOTEEUID", buf);
#else
					envs.set("UNIXREMOTEPID", 0);
					envs.set("UNIXREMOTEEGID", 0);
					envs.set("UNIXREMOTEEUID", 0);
#endif
					break;
				}
				default:
					envs.set("UNIXREMOTEPATH", 0);
					envs.set("UNIXREMOTEPID", 0);
					envs.set("UNIXREMOTEEGID", 0);
					envs.set("UNIXREMOTEEUID", 0);
					break;
			}

			if (verbose)
				std::fprintf(stderr, "%s: %u %s %s %s %s %s\n", prog, getpid(), q(envs, "UNIXLOCALPATH"), q(envs, "UNIXREMOTEPATH"), q(envs, "UNIXREMOTEPID"), q(envs, "UNIXREMOTEEUID"), q(envs, "UNIXREMOTEEGID"));

			return;
		}
	}
}
