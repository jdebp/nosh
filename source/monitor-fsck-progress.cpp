/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#define _BSD_SOURCE 1
#include <map>
#include <vector>
#include <limits>
#include <iostream>
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

namespace {

const char title_text[] = "nosh package parallel fsck monitor";
const char in_progress[] = "fsck in progress";
const char no_fscks[] = "no fscks";

inline ColourPair C(uint_fast8_t f, uint_fast8_t b) { return ColourPair(Map256Colour(f), Map256Colour(b)); }

struct TUI :
	public TerminalCapabilities,
	public TUIOutputBase,
	public TUIInputBase
{
	TUI(ProcessEnvironment & e, const TUIOutputBase::Options & options,ClientTable & m, TUIDisplayCompositor & c);

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

private:
	char stdin_buffer[1U * 1024U];
};

}

TUI::TUI(
	ProcessEnvironment & e,
	const TUIOutputBase::Options & options,
	ClientTable & m,
	TUIDisplayCompositor & comp
) :
	TerminalCapabilities(e),
	TUIOutputBase(*this, std::cout, STDOUT_FILENO, options, comp),
	TUIInputBase(static_cast<const TerminalCapabilities &>(*this), std::cin, STDIN_FILENO),
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
		case SIGTSTP:	/* tstp_signal(); */ break;
		case SIGCONT:	continued(); break;
	}
}

void
TUI::redraw_new (
) {
	vio.CLSToSpace(ColourPair::def);

	vio.WriteNCharsAttr(0, 0, 0U, title, ' ', c.query_w());
	vio.WriteCharStrAttr7Bit(0, (c.query_w() - sizeof title_text + 1) / 2, 0U, title, title_text, sizeof title_text - 1);
	vio.WriteNCharsAttr(1, 0, 0U, status, ' ', c.query_w());
	const std::size_t sl(clients.empty() ? sizeof no_fscks : sizeof in_progress);
	const char * st(clients.empty() ? no_fscks : in_progress);
	vio.WriteCharStrAttr7Bit(1, (c.query_w() - sl + 1) / 2, 0U, status, st, sl - 1);
	vio.WriteNCharsAttr(2, 0, 0U, status, '=', c.query_w());

	long row(3);
	for (ClientTable::const_iterator i(clients.begin()); i != clients.end(); ++i, ++row) {
		if (row < 0) continue;
		if (row >= c.query_h()) break;
		const ConnectedClient & client(i->second);
		const std::string l(client.left()), r(client.right());
		vio.WriteNCharsAttr(row, 0, 0U, line, ' ', c.query_w());
		long col(0);
		vio.PrintCharStrAttrUTF8(row, col, 0U, line, l.data(), l.length());
		vio.PrintNCharsAttr(row, col, 0U, line, ' ', 1U);
		vio.PrintCharStrAttrUTF8(row, col, 0U, line, client.name.data(), client.name.length());
		vio.PrintNCharsAttr(row, col, 0U, line, ' ', 1U);
		if (col + r.length() < c.query_w())
			col = c.query_w() - r.length();
		vio.PrintCharStrAttrUTF8(row, col, 0U, line, r.c_str(), r.length());
		if (client.max) {
			const unsigned n(client.count * c.query_w() / client.max);
			vio.WriteNAttrs(row, 0, 0U, progress, n);
		}
	}
	c.move_cursor(0U, 0U);
	c.set_cursor_state(CursorSprite::BLINK, CursorSprite::BOX);
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
	TUIOutputBase::Options options;
	try {
		popt::bool_definition cursor_application_mode_option('\0', "cursor-keypad-application-mode", "Set the cursor keypad to application mode instead of normal mode.", options.cursor_application_mode);
		popt::bool_definition calculator_application_mode_option('\0', "calculator-keypad-application-mode", "Set the calculator keypad to application mode instead of normal mode.", options.calculator_application_mode);
		popt::bool_definition no_alternate_screen_buffer_option('\0', "no-alternate-screen-buffer", "Prevent switching to the XTerm alternate screen buffer.", options.no_alternate_screen_buffer);
		popt::bool_string_definition scnm_option('\0', "inversescreen", "Switch inverse screen mode on/off.", options.scnm);
		popt::tui_level_definition tui_level_option('T', "tui-level", "Specify the level of TUI character set.");
		popt::definition * tui_table[] = {
			&cursor_application_mode_option,
			&calculator_application_mode_option,
			&no_alternate_screen_buffer_option,
			&scnm_option,
			&tui_level_option,
		};
		popt::table_definition tui_table_option(sizeof tui_table/sizeof *tui_table, tui_table, "Terminal quirks options");
		popt::definition * top_table[] = {
			&tui_table_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		options.tui_level = tui_level_option.value() < options.TUI_LEVELS ? tui_level_option.value() : options.TUI_LEVELS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	const unsigned listen_fds(query_listen_fds_or_daemontools(envs));
	if (1U > listen_fds) {
		die_errno(prog, envs, "LISTEN_FDS");
	}

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		die_errno(prog, envs, "kqueue");
	}
	std::vector<struct kevent> ip;

	append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, SIGWINCH, SIGTSTP, SIGCONT, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, 0);
	append_event(ip, SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGCONT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	for (unsigned i(0U); i < listen_fds; ++i)
		append_event(ip, LISTEN_SOCKET_FILENO + i, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, nullptr);

	ClientTable clients;

	TUIDisplayCompositor compositor(false /* no software cursor */, 24, 80);
	TUI ui(envs, options, clients, compositor);

	// How long to wait with updates pending.
	const struct timespec immediate_timeout = { 0, 0 };

	std::vector<struct kevent> p(listen_fds + 4);
	while (true) {
		if (ui.exit_signalled() || ui.quit_flagged())
			break;
		ui.handle_resize_event();
		ui.handle_refresh_event();

		const int rc(kevent(queue.get(), ip.data(), ip.size(), p.data(), p.size(), ui.has_update_pending() ? &immediate_timeout : nullptr));
		ip.clear();

		if (0 > rc) {
			if (EINTR == errno) continue;
#if defined(__LINUX__) || defined(__linux__)
			if (EINVAL == errno) continue;	// This works around a Linux bug when an inotify queue overflows.
			if (0 == errno) continue;	// This works around another Linux bug.
#endif
			die_errno(prog, envs, "kevent");
		}

		if (0 == rc) {
			ui.handle_update_event();
			continue;
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
							die_errno(prog, envs, "accept");
						}

						append_event(ip, s, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, nullptr);
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
							set_event(o, fd, EVFILT_READ, EV_DELETE|EV_DISABLE, 0, 0, nullptr);
							if (0 > kevent(queue.get(), &o, 1, nullptr, 0, nullptr)) {
								die_errno(prog, envs, "kevent");
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
