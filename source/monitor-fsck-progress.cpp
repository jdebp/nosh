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
#include <sys/socket.h>
#include <unistd.h>
#include "kqueue_common.h"
#include "popt.h"
#include "utils.h"
#include "ttyutils.h"
#include "listen.h"
#include "FileDescriptorOwner.h"
#include "CharacterCell.h"
#include "InputMessage.h"
#include "TerminalCapabilities.h"
#include "TUIDisplayCompositor.h"
#include "TUIOutputBase.h"
#include "TUIInputBase.h"
#include "TUIVIO.h"
#include "SignalManagement.h"

/* Clients ******************************************************************
// **************************************************************************
*/

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
		const long double percent(count * 100.0L / max);
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

/* Full-screen TUI **********************************************************
// **************************************************************************
*/

static const char title_text[] = "nosh package parallel fsck monitor";
static const char in_progress[] = "fsck in progress";
static const char no_fscks[] = "no fscks";

namespace {

inline ColourPair C(uint_fast8_t f, uint_fast8_t b) { return ColourPair(Map256Colour(f), Map256Colour(b)); }

struct TUI :
	public TerminalCapabilities,
	public TUIOutputBase,
	public TUIInputBase
{
	TUI(ProcessEnvironment & e, ClientTable & m, TUIDisplayCompositor & c);

	bool quit_flagged() const { return pending_quit_event; }
	bool exit_signalled() const { return terminate_signalled||interrupt_signalled||hangup_signalled; }
	void handle_signal (int);
	void handle_stdin (int);

protected:
	sig_atomic_t terminate_signalled, interrupt_signalled, hangup_signalled, usr1_signalled, usr2_signalled;
	ClientTable & clients;
	TUIVIO vio;
	bool pending_quit_event;
	const ColourPair normal, title, status, line, progress;

	virtual void redraw_new();

	virtual void ExtendedKey(uint_fast16_t k, uint_fast8_t m);
	virtual void FunctionKey(uint_fast16_t k, uint_fast8_t m);
	virtual void UCS3(uint_fast32_t character);
	virtual void Accelerator(uint_fast32_t character);
	virtual void MouseMove(uint_fast16_t, uint_fast16_t, uint8_t);
	virtual void MouseWheel(uint_fast8_t n, int_fast8_t v, uint_fast8_t m);
	virtual void MouseButton(uint_fast8_t n, uint_fast8_t v, uint_fast8_t m);

private:
	char stdin_buffer[1U * 1024U];
};

}

TUI::TUI(
	ProcessEnvironment & e,
	ClientTable & m,
	TUIDisplayCompositor & comp
) :
	TerminalCapabilities(e),
	TUIOutputBase(*this, stdout, false, false, false, false, false, comp),
	TUIInputBase(static_cast<const TerminalCapabilities &>(*this), stdin),
	terminate_signalled(false),
	interrupt_signalled(false),
	hangup_signalled(false),
	usr1_signalled(false),
	usr2_signalled(false),
	clients(m),
	vio(comp),
	pending_quit_event(false),
	normal(C(COLOUR_WHITE, COLOUR_BLACK)),
	title(C(COLOUR_BLUE, COLOUR_WHITE)),
	status(C(COLOUR_WHITE, COLOUR_BLUE)),
	line(C(COLOUR_BLACK, COLOUR_YELLOW)),
	progress(C(COLOUR_BLACK, COLOUR_GREEN))
{
}

void
TUI::handle_stdin (
	int n		///< number of characters available; can be <= 0 erroneously
) {
	for (;;) {
		int l(read(STDIN_FILENO, stdin_buffer, sizeof stdin_buffer));
		if (0 >= l) break;
		HandleInput(stdin_buffer, l);
		if (l >= n) break;
		n -= l;
	}
	BreakInput();
}

void
TUI::handle_signal (
	int signo
) {
	switch (signo) {
		case SIGWINCH:	set_resized(); break;
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
		case SIGUSR1:	usr1_signalled = true; break;
		case SIGUSR2:	usr2_signalled = true; break;
		case SIGTSTP:	/* exit_full_screen_mode(); raise(SIGSTOP); */ break;
		case SIGCONT:	enter_full_screen_mode(); invalidate_cur(); set_update_needed(); break;
	}
}

void
TUI::redraw_new (
) {
	erase_new_to_backdrop();

	vio.WriteNCharsAttr(0, 0, 0U, title, ' ', c.query_w());
	vio.WriteCharStrAttr(0, (c.query_w() - sizeof title_text + 1) / 2, 0U, title, title_text, sizeof title_text - 1);
	vio.WriteNCharsAttr(1, 0, 0U, status, ' ', c.query_w());
	const std::size_t sl(clients.empty() ? sizeof no_fscks : sizeof in_progress);
	const char * st(clients.empty() ? no_fscks : in_progress);
	vio.WriteCharStrAttr(1, (c.query_w() - sl + 1) / 2, 0U, status, st, sl - 1);
	vio.WriteNCharsAttr(2, 0, 0U, status, '=', c.query_w());

	long row(3);
	for (ClientTable::const_iterator i(clients.begin()); i != clients.end(); ++i, ++row) {
		if (row < 0) continue;
		if (row >= c.query_h()) break;
		const ConnectedClient & client(i->second);
		const std::string l(client.left()), r(client.right());
		vio.WriteNCharsAttr(row, 0, 0U, line, ' ', c.query_w());
		long col(0);
		vio.PrintFormatted(row, col, 0U, line, "%s %s ", l.c_str(), client.name.c_str());
		if (col + r.length() < c.query_w())
			col = c.query_w() - r.length();
		vio.WriteCharStrAttr(row, col, 0U, line, r.c_str(), r.length());
		if (client.max) {
			const unsigned n(client.count * c.query_w() / client.max);
			vio.WriteNAttrs(row, 0, 0U, progress, n);
		}
	}
	c.move_cursor(0U, 0U);
	c.set_cursor_state(CursorSprite::BLINK, CursorSprite::BOX);
}

void
TUI::ExtendedKey(
	uint_fast16_t /*k*/,
	uint_fast8_t /*m*/
) {
}

void
TUI::FunctionKey(
	uint_fast16_t /*k*/,
	uint_fast8_t /*m*/
) {
}

void
TUI::UCS3(
	uint_fast32_t character
) {
	switch (character) {
		case '<':
			ExtendedKey(EXTENDED_KEY_LEFT_ARROW, 0U);
			break;
		case '>':
			ExtendedKey(EXTENDED_KEY_RIGHT_ARROW, 0U);
			break;
	}
}

void
TUI::Accelerator(
	uint_fast32_t /*character*/
) {
}

void 
TUI::MouseMove(
	uint_fast16_t /*row*/,
	uint_fast16_t /*col*/,
	uint8_t /*modifiers*/
) {
}

void 
TUI::MouseWheel(
	uint_fast8_t /*wheel*/,
	int_fast8_t /*value*/,
	uint_fast8_t /*modifiers*/
) {
}

void 
TUI::MouseButton(
	uint_fast8_t /*button*/,
	uint_fast8_t /*value*/,
	uint_fast8_t /*modifiers*/
) {
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

	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	const unsigned listen_fds(query_listen_fds_or_daemontools(envs));
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
	std::vector<struct kevent> ip;

	append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, SIGWINCH, SIGTSTP, SIGCONT, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, 0);
	append_event(ip, SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGCONT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	for (unsigned i(0U); i < listen_fds; ++i)
		append_event(ip, LISTEN_SOCKET_FILENO + i, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, 0);

	ClientTable clients;

	TUIDisplayCompositor compositor(false /* no software cursor */, 24, 80);
	TUI ui(envs, clients, compositor);

	std::vector<struct kevent> p(listen_fds + 4);
	while (true) {
		if (ui.exit_signalled() || ui.quit_flagged())
			break;
		ui.handle_resize_event();
		ui.handle_refresh_event();
		ui.handle_update_event();

		const int rc(kevent(queue.get(), ip.data(), ip.size(), p.data(), p.size(), 0));
		ip.clear();

		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
#if defined(__LINUX__) || defined(__linux__)
			if (EINVAL == error) continue;	// This works around a Linux bug when an inotify queue overflows.
			if (0 == error) continue;	// This works around another Linux bug.
#endif
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}

		for (std::size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_SIGNAL:
					ui.handle_signal(e.ident);
					break;
				case EVFILT_READ:
				{
					const int fd(static_cast<int>(e.ident));
					if (STDIN_FILENO == fd) {
						ui.handle_stdin(e.data);
					} else
					if (static_cast<unsigned>(fd) < LISTEN_SOCKET_FILENO + listen_fds && static_cast<unsigned>(fd) >= LISTEN_SOCKET_FILENO) {
						sockaddr_storage remoteaddr;
						socklen_t remoteaddrsz = sizeof remoteaddr;
						const int s(accept(fd, reinterpret_cast<sockaddr *>(&remoteaddr), &remoteaddrsz));
						if (0 > s) {
							const int error(errno);
							std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "accept", std::strerror(error));
							throw EXIT_FAILURE;
						}

						append_event(ip, s, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, 0);
						clients[s];
					} else
					{
						if (EV_ERROR & e.flags) break;
						const bool hangup(EV_EOF & e.flags);

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
						}
					}
					ui.set_refresh_needed();
					break;
				}
			}
		}
	}

	throw EXIT_SUCCESS;
}
