/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <map>
#include <vector>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#if defined(__LINUX__) || defined(__linux__)
#include "kqueue_linux.h"
#include <ncursesw/curses.h>
#else
#include <sys/event.h>
#include <curses.h>
#endif
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#include "ttyutils.h"
#include "listen.h"
#include "FileDescriptorOwner.h"
#include "SignalManagement.h"

/* Helper functions *********************************************************
// **************************************************************************
*/

static sig_atomic_t halt_signalled(false), window_resized(false);

static
void
handle_signal (
	int signo
) {
	switch (signo) {
		case SIGWINCH:	window_resized = true; break;
		case SIGINT:	halt_signalled = true; break;
		case SIGTERM:	halt_signalled = true; break;
		case SIGHUP:	halt_signalled = true; break;
	}
}

enum {
	DEFAULT_COLOURS = 0,
	TITLE_COLOURS,
	STATUS_COLOURS,
	LINE_COLOURS,
	PROGRESS_COLOURS
};

static inline
void
initialize_colours()
{
	start_color();
	init_pair(PROGRESS_COLOURS, COLOR_BLACK, COLOR_GREEN);
	init_pair(LINE_COLOURS, COLOR_BLACK, COLOR_YELLOW);
	init_pair(TITLE_COLOURS, COLOR_BLUE, COLOR_WHITE);
	init_pair(STATUS_COLOURS, COLOR_WHITE, COLOR_BLUE);
#if defined(NCURSES_VERSION)
	assume_default_colors(COLOR_WHITE, COLOR_BLACK);
#else
	init_pair(DEFAULT_COLOURS, COLOR_WHITE, COLOR_BLACK);
#endif
}

namespace {
	struct ConnectedClient {
		ConnectedClient() : count(0U), max(0U) {}

		std::string lines, pass, name;
		uint_least64_t count, max;

		std::string left() const;
		std::string right() const;
		void parse(std::string);
	};

	typedef std::map<int, ConnectedClient> ClientTable;
}

static inline
std::string
munch (
	std::string & s
) {
	s = ltrim(s);
	for (std::string::size_type p(0); s.length() != p; ++p) {
		if (std::isspace(s[p])) {
			const std::string r(s.substr(0, p));
			s = s.substr(p, std::string::npos);
			return r;
		}
	}
	const std::string r(s);
	s = std::string();
	return r;
}

std::string 
ConnectedClient::left() const
{
	std::string r(pass + " ");
	if (max) {
		const long double percent(count * 100.0 / max);
		char buf[10];
		snprintf(buf, sizeof buf, "%3.0Lf", percent);
		r += buf;
	} else
		r += "---";
	r += "%";
	return r;
}

std::string 
ConnectedClient::right() const
{
	char buf[128];
	snprintf(buf, sizeof buf, "%" PRIu64 "/%" PRIu64, count, max);
	return buf;
}

void 
ConnectedClient::parse(
	std::string s
) {
	pass = munch(s);
	count = val(munch(s));
	max = val(munch(s));
	name = ltrim(s);
}

static const char title[] = "nosh package parallel fsck monitor";
static const char in_progress[] = "fsck in progress";
static const char no_fscks[] = "no fscks";

static inline
void
repaint (
	WINDOW * window,
	ClientTable & clients
) {
	const int width(getmaxx(window));
	attr_t a;
	short c;
	wattr_get(window, &a, &c, 0);
	wcolor_set(window, DEFAULT_COLOURS, 0);
	werase(window);
	wcolor_set(window, TITLE_COLOURS, 0);
	mvwhline(window, 0, 0, ' ', width);
	mvwprintw(window, 0, (width - sizeof title + 1) / 2, "%s", title);
	wcolor_set(window, STATUS_COLOURS, 0);
	mvwhline(window, 1, 0, ' ', width);
	const std::size_t sl(clients.empty() ? sizeof no_fscks : sizeof in_progress);
	const char * status(clients.empty() ? no_fscks : in_progress);
	mvwprintw(window, 1, (width - sl + 1) / 2, "%s", status);
	mvwhline(window, 2, 0, '=', width);
	wcolor_set(window, LINE_COLOURS, 0);
	int row(3);
	wmove(window, row, 0);
	for (ClientTable::const_iterator i(clients.begin()); i != clients.end(); ++i) {
		const ConnectedClient & client(i->second);
		const std::string l(client.left()), r(client.right());
		mvwhline(window, row, 0, ' ', width);
		if (static_cast<std::string::size_type>(width) >= r.length())
			mvwprintw(window, row, width - r.length(), "%s", r.c_str());
		mvwprintw(window, row, 0, "%s %s", l.c_str(), client.name.c_str());
		if (client.max) {
			const int n(client.count * width / client.max);
			mvwchgat(window, row, 0, n, a, PROGRESS_COLOURS, 0);
		}
		++row;
		wmove(window, row, 0);
	}
	wrefresh(window);
}

/* Main function ************************************************************
// **************************************************************************
*/

void
monitor_fsck_progress [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
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

	const unsigned listen_fds(query_listen_fds(envs));
	if (1U > listen_fds) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "LISTEN_FDS", std::strerror(error));
		throw EXIT_FAILURE;
	}

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	ReserveSignalsForKQueue kqueue_reservation(SIGINT, SIGTERM, SIGHUP, SIGWINCH, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGINT, SIGTERM, SIGHUP, 0);

	{
		std::vector<struct kevent> p(listen_fds + 4);
		EV_SET(&p[0], SIGINT, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, 0);
		EV_SET(&p[1], SIGTERM, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, 0);
		EV_SET(&p[2], SIGHUP, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, 0);
		EV_SET(&p[3], SIGWINCH, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, 0);
		for (unsigned i(0U); i < listen_fds; ++i)
			EV_SET(&p[4 + i], LISTEN_SOCKET_FILENO + i, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, 0);
		if (0 > kevent(queue.get(), p.data(), p.size(), 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	ClientTable clients;

	WINDOW * window(initscr());
	initialize_colours();

	try {
		std::vector<struct kevent> p(listen_fds + 4 + 1024);
		for (;;) {
			if (halt_signalled) {
				throw EXIT_SUCCESS;
			}
			if (window_resized) {
				window_resized = false;
				struct winsize size;
				if (tcgetwinsz_nointr(STDOUT_FILENO, size) == 0) {
					resize_term(size.ws_row, size.ws_col);
					// We need to repaint the parts of stdscr that have just been exposed.
					clearok(window, 1);
					repaint(window, clients);
				}
			}
			const int rc(kevent(queue.get(), 0, 0, p.data(), p.size(), 0));
			if (0 > rc) {
				if (EINTR == errno) continue;
exit_error:
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
				throw EXIT_FAILURE;
			}
			for (size_t i(0); i < static_cast<std::size_t>(rc); ++i) 
			{
				const struct kevent & e(p[i]);
				if (EVFILT_SIGNAL == e.filter) {
					handle_signal (e.ident);
					continue;
				} else
				if (EVFILT_READ != e.filter) {
					continue;
				}
				const int fd(static_cast<int>(e.ident));
				const bool hangup(EV_EOF & e.flags);
				if (static_cast<unsigned>(fd) < LISTEN_SOCKET_FILENO + listen_fds && static_cast<unsigned>(fd) >= LISTEN_SOCKET_FILENO) {
					sockaddr_storage remoteaddr;
					socklen_t remoteaddrsz = sizeof remoteaddr;
					const int s(accept(fd, reinterpret_cast<sockaddr *>(&remoteaddr), &remoteaddrsz));
					if (0 > s) goto exit_error;

					if (clients.empty())
						wrefresh(window);

					struct kevent o;
					EV_SET(&o, s, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, 0);
					if (0 > kevent(queue.get(), &o, 1, 0, 0, 0)) {
						const int error(errno);
						std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
						throw EXIT_FAILURE;
					}

					clients[s];
				} else {
					ConnectedClient & client(clients[fd]);
					char buf[8U * 1024U];
					const ssize_t c(read(fd, buf, sizeof buf));
					if (c > 0)
						client.lines += std::string(buf, buf + c);
					if (hangup && !c && !client.lines.empty())
						client.lines += '\n';
					std::string line;
					for (;;) {
						if (client.lines.empty()) break;
						const std::string::size_type n(client.lines.find('\n'));
						if (std::string::npos == n) break;
						line = client.lines.substr(0, n);
						client.lines = client.lines.substr(n + 1, std::string::npos);
					}
					if (!line.empty())
						client.parse(line);
					if (hangup && !c) {
						struct kevent o;
						EV_SET(&o, fd, EVFILT_READ, EV_DELETE|EV_DISABLE, 0, 0, 0);
						if (0 > kevent(queue.get(), &o, 1, 0, 0, 0)) {
							const int error(errno);
							std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
							throw EXIT_FAILURE;
						}
						close(fd);
						clients.erase(fd);

						if (clients.empty()) {
							repaint(window, clients);
							endwin();
						}
					}
					if (!clients.empty())
						repaint(window, clients);
				}
			}
		} 
	} catch (...) {
		if (!clients.empty()) endwin();
		throw;
	}
}
