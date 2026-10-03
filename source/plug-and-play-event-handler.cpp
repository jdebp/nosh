/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
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
#include "kqueue_common.h"
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#include "ProcessEnvironment.h"
#include "listen.h"
#include "SignalManagement.h"

/* Helper functions *********************************************************
// **************************************************************************
*/

namespace {

sig_atomic_t child_signalled(false), halt_signalled(false);

void
handle_signal (
	int signo
) {
	switch (signo) {
		case SIGCHLD:		child_signalled = true; break;
		case SIGINT:		halt_signalled = true; break;
		case SIGTERM:		halt_signalled = true; break;
		case SIGHUP:		halt_signalled = true; break;
	}
}

inline
void
reap (
	const char * prog,
	bool verbose,
	unsigned long & children,
	unsigned long max_children
) {
	for (;;) {
		int status, code;
		pid_t c;
		if (0 >= wait_nonblocking_for_anychild_exit(c, status, code)) break;
		if (children) {
			--children;
			if (verbose)
				std::fprintf(stderr, "%s: %u ended status %i code %i %lu/%lu\n", prog, c, status, code, children, max_children);
		}
	}
}

}

/* Main function ************************************************************
// **************************************************************************
*/

void
plug_and_play_event_handler (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool verbose(false);
	try {
		popt::bool_definition verbose_option('v', "verbose", "Print status information.", verbose);
		popt::definition * top_table[] = {
			&verbose_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) die_missing_next_program(prog, envs);

	const unsigned listen_fds(query_listen_fds_or_daemontools(envs));
	if (1U > listen_fds) {
		die_errno(prog, envs, "LISTEN_FDS");
	}

	const int queue(kqueue());
	if (0 > queue) {
		die_errno(prog, envs, "kqueue");
	}

	ReserveSignalsForKQueue kqueue_reservation(SIGCHLD, SIGINT, SIGTERM, SIGHUP, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGINT, SIGTERM, SIGHUP, 0);

	std::vector<struct kevent> ip;
	for (unsigned i(0U); i < listen_fds; ++i)
		append_event(ip, LISTEN_SOCKET_FILENO + i, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);

	const unsigned long max_children(1);
	unsigned long children(0);

	if (verbose)
		std::fprintf(stderr, "%s: startup\n", prog);
	for (;;) {
		if (child_signalled) {
			reap(prog, verbose, children, max_children);
			child_signalled = false;
		}
		if (halt_signalled && children <= 0) {
			if (verbose)
				std::fprintf(stderr, "%s: shutdown\n", prog);
			throw EXIT_SUCCESS;
		}
		for (unsigned i(0U); i < listen_fds; ++i)
			append_event(ip, LISTEN_SOCKET_FILENO + i, EVFILT_READ, children < max_children ? EV_ENABLE : EV_DISABLE, 0, 0, nullptr);
		struct kevent p[128];
		const int rc(kevent(queue, ip.data(), ip.size(), p, sizeof p/sizeof *p, nullptr));
		ip.clear();
		if (0 > rc) {
			if (EINTR == errno) continue;
exit_error:
			die_errno(prog, envs, prog);
		}
		for (size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			if (EVFILT_SIGNAL == p[i].filter) {
				handle_signal (p[i].ident);
				continue;
			} else
			if (EVFILT_READ != p[i].filter)
				continue;
			const int l(static_cast<int>(p[i].ident));

			char buf[8U * 1024U];
			const ssize_t r(read(l, buf, sizeof buf));
			if (0 > r) goto exit_error;

			const pid_t child(fork());
			if (0 > child) goto exit_error;
			if (0 != child) {
				++children;
				if (verbose)
					std::fprintf(stderr, "%s: %u started %lu/%lu\n", prog, child, children, max_children);
				continue;
			}

			for (unsigned j(0U); j < listen_fds; ++j)
				close(LISTEN_SOCKET_FILENO + j);

			for (int e(0), b(e); ;) {
				if (e >= r || !buf[e]) {
					if (e > b) {
						const std::string s(buf + b, buf + e);
						const std::string::size_type q(s.find('='));
						const std::string var(s.substr(0, q));
						const std::string val(q == std::string::npos ? std::string() : s.substr(q + 1, std::string::npos));
						envs.set(var, val);
					}
					if (e >= r) break;
					++e;
					b = e;
				} else
					++e;
			}

			return;
		}
	}
}
