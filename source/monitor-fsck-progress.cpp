/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <stdint.h>
#include <sys/types.h>
#if !defined(__LINUX__) && !defined(__linux__)
#include <sys/event.h>
#include <curses.h>
#else
#include <sys/poll.h>
#include <ncursesw/curses.h>
#endif
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#include "fdutils.h"
#include "listen.h"

/* Helper functions *********************************************************
// **************************************************************************
*/

static sig_atomic_t halt_signalled(false);

static
void
handle_signal (
	int signo
) {
	switch (signo) {
		case SIGINT:		halt_signalled = true; break;
		case SIGTERM:		halt_signalled = true; break;
		case SIGHUP:		halt_signalled = true; break;
	}
}

#if !defined(__LINUX__) && !defined(__linux__)
static void sig_ignore (int) {}
#endif

/* Main function ************************************************************
// **************************************************************************
*/

void
monitor_fsck_progress ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {};
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

	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
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

	std::vector<struct kevent> p(listen_fds);
	for (unsigned i(0U); i < listen_fds; ++i)
		EV_SET(&p[i], LISTEN_SOCKET_FILENO + i, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, 0);
	if (0 > kevent(queue, p.data(), listen_fds, 0, 0, 0)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
		throw EXIT_FAILURE;
	}
#else
	std::vector<pollfd> p(listen_fds);
	for (unsigned i(0U); i < listen_fds; ++i) {
		p[i].fd = LISTEN_SOCKET_FILENO + i;
		p[i].events = POLLIN;
	}

	sigset_t original_signals;
	sigprocmask(SIG_SETMASK, 0, &original_signals);
	sigset_t masked_signals(original_signals);
	sigaddset(&masked_signals, SIGINT);
	sigaddset(&masked_signals, SIGTERM);
	sigaddset(&masked_signals, SIGHUP);
	sigprocmask(SIG_SETMASK, &masked_signals, 0);
	sigset_t masked_signals_during_poll(masked_signals);
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
		sigaction(SIGINT,&sa,NULL);
		sigaction(SIGTERM,&sa,NULL);
		sigaction(SIGHUP,&sa,NULL);
	}

	unsigned read_fds(0U);
	std::vector<std::string> b(read_fds);
	std::vector<std::string> l(read_fds);

	WINDOW * window(initscr());
	start_color();

	try {
		for (;;) {
			if (halt_signalled) {
				throw EXIT_SUCCESS;
			}
#if !defined(__LINUX__) && !defined(__linux__)
			const int rc(kevent(queue, p.data(), listen_fds + read_fds, p.data(), listen_fds + read_fds, 0));
#else
			const int rc(ppoll(p.data(), listen_fds + read_fds, 0, &masked_signals_during_poll));
#endif
			if (0 > rc) {
				if (EINTR == errno) continue;
	exit_error:
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
				throw EXIT_FAILURE;
			}
#if !defined(__LINUX__) && !defined(__linux__)
			for (size_t i(0); i < static_cast<std::size_t>(rc); ) 
#else
			for (size_t i(0); i < listen_fds + read_fds; ) 
#endif
			{
#if !defined(__LINUX__) && !defined(__linux__)
				const struct kevent & e(p[i]);
				if (EVFILT_SIGNAL == e.filter) {
					handle_signal (e.ident);
					++i;
					continue;
				} else
				if (EVFILT_READ != e.filter) {
					++i;
					continue;
				}
				const int fd(static_cast<int>(e.ident));
				const bool hangup(EV_EOF & e.flags);
#else
				const pollfd & e(p[i]);
				if (!(e.revents & (POLLIN|POLLHUP))) {
					++i;
					continue;
				}
				const int fd(e.fd);
				const bool hangup(e.revents & POLLHUP);
#endif
				if (i < listen_fds) {
					sockaddr_storage remoteaddr;
					socklen_t remoteaddrsz = sizeof remoteaddr;
					const int s(accept(fd, reinterpret_cast<sockaddr *>(&remoteaddr), &remoteaddrsz));
					if (0 > s) goto exit_error;

					if (read_fds < 1U)
						wrefresh(window);

#if !defined(__LINUX__) && !defined(__linux__)
					struct kevent o;
					EV_SET(&o, s, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, 0);
					if (0 > kevent(queue, &o, 1, 0, 0, 0)) {
						const int error(errno);
						std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
						throw EXIT_FAILURE;
					}
#else
					pollfd o;
					o.fd = s;
					o.events = POLLIN|POLLHUP;
					o.revents = 0;
#endif
					p.push_back(o);
					b.push_back(std::string());
					l.push_back(std::string());
					++read_fds;

					++i;
				} else {
					const int row(i - listen_fds);
					std::string & d(b[row]);
					char buf[8U * 1024U];
					const int c(read(fd, buf, sizeof buf));
					if (c > 0)
						d += std::string(buf, buf + c);
					if (hangup && !c && !d.empty())
						d += '\n';
					for (;;) {
						if (d.empty()) break;
						const std::string::size_type n(d.find('\n'));
						if (std::string::npos == n) break;
						l[row] = d.substr(0, n);
						d = d.substr(n + 1, std::string::npos);
					}
					if (hangup && !c) {
#if !defined(__LINUX__) && !defined(__linux__)
						struct kevent o;
						EV_SET(&o, fd, EVFILT_READ, EV_DELETE|EV_DISABLE, 0, 0, 0);
						if (0 > kevent(queue, &o, 1, 0, 0, 0)) {
							const int error(errno);
							std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
							throw EXIT_FAILURE;
						}
#endif
						close(fd);
						p.erase(p.begin() + i);
						b.erase(b.begin() + row);
						l.erase(l.begin() + row);
						--read_fds;

						if (read_fds < 1U)
							endwin();
					} else
						++i;
					if (read_fds > 0U) {
						werase(window);
						for (size_t r(0); r < read_fds; ++r)
							wprintw(window, "%s\n", l[r].c_str());
						wrefresh(window);
					}
				}
			}
		} 
	} catch (...) {
		if (read_fds > 0) endwin();
		throw;
	}
}
