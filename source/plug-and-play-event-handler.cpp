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
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#include "listen.h"

/* Helper functions *********************************************************
// **************************************************************************
*/

static sig_atomic_t child_signalled(false), halt_signalled(false);

static
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

#if !defined(__LINUX__) && !defined(__linux__)
static void sig_ignore (int) {}
#endif

static inline
void
reap (
	const char * prog,
	bool verbose,
	unsigned long & children,
	unsigned long max_children
) {
	for (;;) {
		int status;
		pid_t c = waitpid(-1, &status, WNOHANG);
		if (c <= 0) break;
		if (children) {
			--children;
			if (verbose)
				std::fprintf(stderr, "%s: %u ended status %i %lu/%lu\n", prog, c, status, children, max_children);
		}
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
plug_and_play_event_handler ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	bool verbose(false);
	try {
		popt::bool_definition verbose_option('v', "verbose", "Print status information.", verbose);
		popt::definition * top_table[] = {
			&verbose_option
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

	std::vector<struct kevent> p(listen_fds + 4);
	for (unsigned i(0U); i < listen_fds; ++i)
		EV_SET(&p[i], LISTEN_SOCKET_FILENO + i, EVFILT_READ, EV_ADD, 0, 0, 0);
	EV_SET(&p[listen_fds + 0], SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[listen_fds + 1], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[listen_fds + 2], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[listen_fds + 3], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	if (0 > kevent(queue, p.data(), listen_fds + 1, 0, 0, 0)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
		throw EXIT_FAILURE;
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
	sigaddset(&masked_signals, SIGINT);
	sigaddset(&masked_signals, SIGTERM);
	sigaddset(&masked_signals, SIGHUP);
	sigprocmask(SIG_SETMASK, &masked_signals, 0);
	sigset_t masked_signals_during_poll(masked_signals);
	sigdelset(&masked_signals_during_poll, SIGCHLD);
	sigdelset(&masked_signals_during_poll, SIGINT);
	sigdelset(&masked_signals_during_poll, SIGTERM);
	sigdelset(&masked_signals_during_poll, SIGHUP);
#endif

	{
		struct sigaction sa;
		sa.sa_flags=0;
		sigemptyset(&sa.sa_mask);
#if !defined(__LINUX__) && !defined(__linux__)
		sa.sa_handler=sig_ignore;
#else
		sa.sa_handler=handle_signal;
#endif
		sigaction(SIGCHLD,&sa,NULL);
		sigaction(SIGINT,&sa,NULL);
		sigaction(SIGTERM,&sa,NULL);
		sigaction(SIGHUP,&sa,NULL);
	}

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
#if !defined(__LINUX__) && !defined(__linux__)
		for (unsigned i(0U); i < listen_fds; ++i)
			EV_SET(&p[i], LISTEN_SOCKET_FILENO + i, EVFILT_READ, children < max_children ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		const int rc(kevent(queue, p.data(), listen_fds, p.data(), listen_fds + 4, 0));
#else
		for (unsigned i(0U); i < listen_fds; ++i)
			if (children < max_children)
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
				handle_signal (p[i].ident);
				continue;
			} else
			if (EVFILT_READ != p[i].filter) 
				continue;
			const int l(static_cast<int>(p[i].ident));
#else
			if (!(p[i].revents & POLLIN)) continue;
			const int l(p[i].fd);
#endif

			char buf[8U * 1024U];
			const int r(read(l, buf, sizeof buf));
			if (0 > r) goto exit_error;

			const int child(fork());
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
					if (b) {
						const std::string s(buf + b, buf + e);
						const std::string::size_type q(s.find('='));
						const std::string var(s.substr(0, q));
						const std::string val(q == std::string::npos ? std::string() : s.substr(q + 1, std::string::npos));
						setenv(var.c_str(), val.c_str(), 1);
					}
					if (e >= r) break;
					++e;
					b = e;
				} else
					++e;
			}

			{
				struct sigaction sa;
				sa.sa_flags=0;
				sigemptyset(&sa.sa_mask);
				sa.sa_handler=SIG_DFL;
				sigaction(SIGCHLD,&sa,NULL);
				sigaction(SIGINT,&sa,NULL);
				sigaction(SIGTERM,&sa,NULL);
				sigaction(SIGHUP,&sa,NULL);
			}
#if defined(__LINUX__) || defined(__linux__)
			sigprocmask(SIG_SETMASK, &original_signals, 0);
#endif
			return;
		}
	}
}
