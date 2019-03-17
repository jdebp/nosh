/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _XOPEN_SOURCE_EXTENDED
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <clocale>
#include <cerrno>
#include <stdint.h>
#include <sys/stat.h>
#include "kqueue_common.h"
#include <unistd.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "BaseTUI.h"
#include "CharacterCell.h"
#include "InputMessage.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "SignalManagement.h"
#include "VirtualTerminalBackEnd.h"
#if defined(__LINUX__) || defined(__linux__)
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

/* Full-screen TUI **********************************************************
// **************************************************************************
*/

namespace {
struct Realizer :
	public BaseTUI
{
	Realizer(bool, VirtualTerminalBackEnd &);

	bool exit_signalled() const { return terminate_signalled||interrupt_signalled||hangup_signalled; }
	void handle_signal (int);
protected:
	sig_atomic_t terminate_signalled, interrupt_signalled, hangup_signalled;
	const bool wrong_way_up;	///< causes coordinate transforms between c and vt
	VirtualTerminalBackEnd & vt;

	void setup_colours();
	virtual void redraw();
	virtual void unicode_keypress(wint_t);
	virtual void ncurses_keypress(wint_t);

	static uint32_t MessageFromNCursesKey (uint32_t c);
	static short colour_pair (unsigned char f, unsigned char b);
	static unsigned char cga_colour_from (const CharacterCell::colour_type &);
	static attr_t attributes_from (CharacterCell::attribute_type a);
};
}

Realizer::Realizer(
	bool wwu,
	VirtualTerminalBackEnd & v
) :
	terminate_signalled(false),
	interrupt_signalled(false),
	hangup_signalled(false),
	wrong_way_up(wwu),
	vt(v)
{
	setup_colours();
}

// ncursesw has a terrible colour model.
// Basically it's the "HP like" one that the termcap doco talks about.
// To allow for arbitrary foreground+background combinations, we have to create a colour pairing for each possible combination.
// Then we have to map each combination of foreground and background into the appropriate colour pair.

// ncursesw gives us no guarantee that its standard colour macros have values less than 8.
static const short CGAtoncurses[8] = {
	COLOR_BLACK,
	COLOR_BLUE,
	COLOR_GREEN,
	COLOR_CYAN,
	COLOR_RED,
	COLOR_MAGENTA,
	COLOR_YELLOW,
	COLOR_WHITE
};

inline
short
Realizer::colour_pair (
	unsigned char f,
	unsigned char b
) {
#if defined(NCURSES_VERSION)
	// As an extension ncurses allows use of colour pair 0.
	return CGAtoncurses[f] | (CGAtoncurses[b] << 3);
#else
	// curses reserves colour pair 0 for white on black.
	return 1 + (CGAtoncurses[f] | (CGAtoncurses[b] << 3));
#endif
}

// ncursesw doesn't have a real 32-bit colour mechanism.
// The RGB system that it does have is in fact a colour palette, and unusable for our purposes.

// So we map the 32-bit colours used in the display buffer into 3-bit RGB (which is them mapped to the 3-bit BGR used by ncursesw) using a "greatest intensity wins" approach.
inline
unsigned char
Realizer::cga_colour_from (
	const CharacterCell::colour_type & c
) {
	enum {
		CGA_BLACK,
		CGA_BLUE,
		CGA_GREEN,
		CGA_CYAN,
		CGA_RED,
		CGA_MAGENTA,
		CGA_YELLOW,
		CGA_WHITE
	};

	if (c.red < c.green) {
		// Something with no red.
		return	c.green < c.blue ? CGA_BLUE :
			c.blue < c.green ? CGA_GREEN :
			CGA_CYAN;
	} else
	if (c.green < c.red) {
		// Something with no green.
		return	c.red < c.blue ? CGA_BLUE :
			c.blue < c.red ? CGA_RED :
			CGA_MAGENTA;
	} else
	{
		// Something with equal red and green.
		return	c.red < c.blue ? CGA_BLUE :
			c.blue < c.red ? CGA_YELLOW :
			c.red ? CGA_WHITE :
			CGA_BLACK;
	}
}

inline
attr_t
Realizer::attributes_from (
	CharacterCell::attribute_type a
) {
	return	(a & CharacterCell::BOLD ? A_BOLD : 0) |
		(a & CharacterCell::UNDERLINES ? A_UNDERLINE : 0) |
		(a & CharacterCell::BLINK ? A_BLINK : 0) |
		(a & CharacterCell::INVERSE ? A_REVERSE : 0) |
		(a & CharacterCell::INVISIBLE ? A_INVIS : 0) |
		(a & CharacterCell::FAINT ? A_DIM : 0) |
		0;
}

void
Realizer::setup_colours(
) {
	for (unsigned char f(0U); f < 8U; ++f)
		for (unsigned char b(0U); b < 8U; ++b) {
			const short p(colour_pair(f,b));
			if (p)
				init_pair(p, CGAtoncurses[f], CGAtoncurses[b]);
			else
				setup_default_colours(CGAtoncurses[f], CGAtoncurses[b]);
		}
}

/// \brief Pull the display buffer from the vt into the ncurses window, but don't output anything.
inline
void
Realizer::redraw (
) {
	werase(window);
	const unsigned cols(vt.query_w()), rows(vt.query_h());
	wresize(window, rows, cols);
	for (unsigned source_row(0); source_row < rows; ++source_row) {
		const unsigned dest_row(wrong_way_up ? rows - source_row - 1U : source_row);
		for (unsigned col(0); col < cols; ++col) {
			const CharacterCell & c(vt.at(source_row, col));
			wchar_t ws[2] = { static_cast<wchar_t>(c.character), L'\0' };
			const unsigned char fg_cga(cga_colour_from(c.foreground));
			const unsigned char bg_cga(cga_colour_from(c.background));
			cchar_t cc;
			setcchar(&cc, ws, attributes_from(c.attributes), colour_pair(fg_cga, bg_cga), 0);
			// This doesn't necessarily do the right thing with control, combining, and zero-width code points, but they are unlikely to occur in the buffer in the first place as the terminal emulator will have dealt with them.
			mvwadd_wch(window, dest_row, col, &cc);
		}
	}
	wmove(window, wrong_way_up ? rows - vt.query_cursor_y() - 1U : vt.query_cursor_y(), vt.query_cursor_x());
	if (!(CursorSprite::VISIBLE & vt.query_cursor_attributes()))
		set_cursor_visibility(0);
	else if (!(CursorSprite::BLINK & vt.query_cursor_attributes()))
		set_cursor_visibility(1);
	else
		set_cursor_visibility(2);
}

inline
void
Realizer::handle_signal (
	int signo
) {
	switch (signo) {
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
		default:	BaseTUI::handle_signal(signo); break;
	}
}

inline
uint32_t
Realizer::MessageFromNCursesKey (
	uint32_t c
) {
	if (c >= KEY_F(0) && c < KEY_F(64)) {
		const unsigned f(c - KEY_F(0));
		switch (f) {
			// Usually, termcap entries for the first few function keys are the sequences sent by the keypad function keys, not the function keys in the function key row.
			// We're dealing in the "internals" of a terminal, as it were, here; so we want to send along the "original" keys, and allow for a complete function key row.
			case 1:		return MessageForExtendedKey(EXTENDED_KEY_PAD_F1, 0);
			case 2:		return MessageForExtendedKey(EXTENDED_KEY_PAD_F2, 0);
			case 3:		return MessageForExtendedKey(EXTENDED_KEY_PAD_F3, 0);
			case 4:		return MessageForExtendedKey(EXTENDED_KEY_PAD_F4, 0);
			case 5:		return MessageForExtendedKey(EXTENDED_KEY_PAD_F5, 0);
			default:	return MessageForFunctionKey(c - KEY_F(0), 0);
		}
	} else switch (c) {
		case KEY_DOWN:		return MessageForExtendedKey(EXTENDED_KEY_DOWN_ARROW, 0);
		case KEY_UP:		return MessageForExtendedKey(EXTENDED_KEY_UP_ARROW, 0);
		case KEY_LEFT:		return MessageForExtendedKey(EXTENDED_KEY_LEFT_ARROW, 0);
		case KEY_SLEFT:		return MessageForExtendedKey(EXTENDED_KEY_LEFT_ARROW, INPUT_MODIFIER_LEVEL2);
		case KEY_RIGHT:		return MessageForExtendedKey(EXTENDED_KEY_RIGHT_ARROW, 0);
		case KEY_SRIGHT:	return MessageForExtendedKey(EXTENDED_KEY_RIGHT_ARROW, INPUT_MODIFIER_LEVEL2);
		case KEY_LL:		return MessageForExtendedKey(EXTENDED_KEY_LL_ARROW, 0);
		case KEY_HOME:		return MessageForExtendedKey(EXTENDED_KEY_PAD_HOME, 0);
		case KEY_SHOME:		return MessageForExtendedKey(EXTENDED_KEY_PAD_HOME, INPUT_MODIFIER_LEVEL2);
		case KEY_END:		return MessageForExtendedKey(EXTENDED_KEY_PAD_END, 0);
		case KEY_SEND:		return MessageForExtendedKey(EXTENDED_KEY_PAD_END, INPUT_MODIFIER_LEVEL2);
		case KEY_NPAGE:		return MessageForExtendedKey(EXTENDED_KEY_PAD_PAGE_DOWN, 0);	
		case KEY_PPAGE:		return MessageForExtendedKey(EXTENDED_KEY_PAD_PAGE_UP, 0);
		case KEY_ENTER:		return MessageForExtendedKey(EXTENDED_KEY_PAD_ENTER, 0);
		case KEY_A1:		return MessageForExtendedKey(EXTENDED_KEY_PAD_HOME, 0);
		case KEY_A3:		return MessageForExtendedKey(EXTENDED_KEY_PAD_PAGE_UP, 0);
		case KEY_B2:		return MessageForExtendedKey(EXTENDED_KEY_PAD_CENTRE, 0);
		case KEY_C1:		return MessageForExtendedKey(EXTENDED_KEY_PAD_END, 0);
		case KEY_C3:		return MessageForExtendedKey(EXTENDED_KEY_PAD_PAGE_DOWN, 0);
		case KEY_BTAB:		return MessageForExtendedKey(EXTENDED_KEY_BACKTAB, 0);
		case KEY_BACKSPACE:	return MessageForExtendedKey(EXTENDED_KEY_BACKSPACE, 0);
		case KEY_DL:		return MessageForExtendedKey(EXTENDED_KEY_DEL_LINE, 0);
		case KEY_SDL:		return MessageForExtendedKey(EXTENDED_KEY_DEL_LINE, INPUT_MODIFIER_LEVEL2);
		case KEY_IL:		return MessageForExtendedKey(EXTENDED_KEY_INS_LINE, 0);
		case KEY_DC:		return MessageForExtendedKey(EXTENDED_KEY_DEL_CHAR, 0);
		case KEY_SDC:		return MessageForExtendedKey(EXTENDED_KEY_DEL_CHAR, INPUT_MODIFIER_LEVEL2);
		case KEY_IC:		return MessageForExtendedKey(EXTENDED_KEY_INS_CHAR, 0);
		case KEY_SIC:		return MessageForExtendedKey(EXTENDED_KEY_INS_CHAR, INPUT_MODIFIER_LEVEL2);
#if 0
		case KEY_EIC:		return MessageForExtendedKey(EXTENDED_KEY_, 0);	
#endif
		case KEY_FIND:		return MessageForExtendedKey(EXTENDED_KEY_FIND, 0);
		case KEY_SFIND:		return MessageForExtendedKey(EXTENDED_KEY_FIND, INPUT_MODIFIER_LEVEL2);
		case KEY_HELP:		return MessageForExtendedKey(EXTENDED_KEY_HELP, 0);
		case KEY_SHELP:		return MessageForExtendedKey(EXTENDED_KEY_HELP, INPUT_MODIFIER_LEVEL2);
		case KEY_CANCEL:	return MessageForExtendedKey(EXTENDED_KEY_CANCEL, 0);
		case KEY_SCANCEL:	return MessageForExtendedKey(EXTENDED_KEY_CANCEL, INPUT_MODIFIER_LEVEL2);
		case KEY_COPY:		return MessageForExtendedKey(EXTENDED_KEY_COPY, 0);	
		case KEY_SCOPY:		return MessageForExtendedKey(EXTENDED_KEY_COPY, INPUT_MODIFIER_LEVEL2);
		case KEY_UNDO:		return MessageForExtendedKey(EXTENDED_KEY_UNDO, 0);
		case KEY_SUNDO:		return MessageForExtendedKey(EXTENDED_KEY_UNDO, INPUT_MODIFIER_LEVEL2);
		case KEY_SELECT:	return MessageForExtendedKey(EXTENDED_KEY_SELECT, 0);
		case KEY_NEXT:		return MessageForExtendedKey(EXTENDED_KEY_NEXT, 0);	
		case KEY_SNEXT:		return MessageForExtendedKey(EXTENDED_KEY_NEXT, INPUT_MODIFIER_LEVEL2);
		case KEY_PREVIOUS:	return MessageForExtendedKey(EXTENDED_KEY_PREVIOUS, 0);
		case KEY_SPREVIOUS:	return MessageForExtendedKey(EXTENDED_KEY_PREVIOUS, INPUT_MODIFIER_LEVEL2);
		case KEY_PRINT:		return MessageForExtendedKey(EXTENDED_KEY_PRINT, 0);	// This is not Print Screen.
		case KEY_SPRINT:	return MessageForExtendedKey(EXTENDED_KEY_PRINT, INPUT_MODIFIER_LEVEL2);	// This is not Print Screen.
		case KEY_BEG:		return MessageForExtendedKey(EXTENDED_KEY_BEGIN, 0);
		case KEY_SBEG:		return MessageForExtendedKey(EXTENDED_KEY_BEGIN, INPUT_MODIFIER_LEVEL2);
		case KEY_CLOSE:		return MessageForExtendedKey(EXTENDED_KEY_CLOSE, 0);
		case KEY_COMMAND:	return MessageForExtendedKey(EXTENDED_KEY_COMMAND, 0);
		case KEY_SCOMMAND:	return MessageForExtendedKey(EXTENDED_KEY_COMMAND, INPUT_MODIFIER_LEVEL2);
		case KEY_CREATE:	return MessageForExtendedKey(EXTENDED_KEY_CREATE, 0);
		case KEY_SCREATE:	return MessageForExtendedKey(EXTENDED_KEY_CREATE, INPUT_MODIFIER_LEVEL2);
		case KEY_EXIT:		return MessageForExtendedKey(EXTENDED_KEY_EXIT, 0);
		case KEY_SEXIT:		return MessageForExtendedKey(EXTENDED_KEY_EXIT, INPUT_MODIFIER_LEVEL2);
		case KEY_MARK:		return MessageForExtendedKey(EXTENDED_KEY_MARK, 0);
		case KEY_MESSAGE:	return MessageForExtendedKey(EXTENDED_KEY_MESSAGE, 0);
		case KEY_SMESSAGE:	return MessageForExtendedKey(EXTENDED_KEY_MESSAGE, INPUT_MODIFIER_LEVEL2);
		case KEY_MOVE:		return MessageForExtendedKey(EXTENDED_KEY_MOVE, 0);
		case KEY_SMOVE:		return MessageForExtendedKey(EXTENDED_KEY_MOVE, INPUT_MODIFIER_LEVEL2);
		case KEY_OPEN:		return MessageForExtendedKey(EXTENDED_KEY_OPEN, 0);
		case KEY_OPTIONS:	return MessageForExtendedKey(EXTENDED_KEY_OPTIONS, 0);
		case KEY_SOPTIONS:	return MessageForExtendedKey(EXTENDED_KEY_OPTIONS, INPUT_MODIFIER_LEVEL2);	
		case KEY_REDO:		return MessageForExtendedKey(EXTENDED_KEY_REDO, 0);
		case KEY_SREDO:		return MessageForExtendedKey(EXTENDED_KEY_REDO, INPUT_MODIFIER_LEVEL2);	
		case KEY_REFERENCE:	return MessageForExtendedKey(EXTENDED_KEY_REFERENCE, 0);
		case KEY_REFRESH:	return MessageForExtendedKey(EXTENDED_KEY_REFRESH, 0);	
		case KEY_REPLACE:	return MessageForExtendedKey(EXTENDED_KEY_REPLACE, 0);
		case KEY_SREPLACE:	return MessageForExtendedKey(EXTENDED_KEY_REPLACE, INPUT_MODIFIER_LEVEL2);
		case KEY_RESTART:	return MessageForExtendedKey(EXTENDED_KEY_RESTART, 0);
		case KEY_RESUME:	return MessageForExtendedKey(EXTENDED_KEY_RESUME, 0);
		case KEY_SRSUME:	return MessageForExtendedKey(EXTENDED_KEY_RESUME, INPUT_MODIFIER_LEVEL2);
		case KEY_SAVE:		return MessageForExtendedKey(EXTENDED_KEY_SAVE, 0);
		case KEY_SSAVE:		return MessageForExtendedKey(EXTENDED_KEY_SAVE, INPUT_MODIFIER_LEVEL2);
		case KEY_SUSPEND:	return MessageForExtendedKey(EXTENDED_KEY_SUSPEND, 0);
		case KEY_SSUSPEND:	return MessageForExtendedKey(EXTENDED_KEY_SUSPEND, INPUT_MODIFIER_LEVEL2);
		case KEY_CLEAR:		return MessageForExtendedKey(EXTENDED_KEY_CLR_SCR, 0);
		case KEY_EOS:		return MessageForExtendedKey(EXTENDED_KEY_CLR_EOS, 0);
		case KEY_EOL:		return MessageForExtendedKey(EXTENDED_KEY_CLR_EOL, 0);
		case KEY_SEOL:		return MessageForExtendedKey(EXTENDED_KEY_CLR_EOL, INPUT_MODIFIER_LEVEL2);
		case KEY_SF:		return MessageForExtendedKey(EXTENDED_KEY_SCROLL_DOWN, 0);
		case KEY_SR:		return MessageForExtendedKey(EXTENDED_KEY_SCROLL_UP, 0);
		case KEY_STAB:		return MessageForExtendedKey(EXTENDED_KEY_SET_TAB, 0);
		case KEY_CTAB:		return MessageForExtendedKey(EXTENDED_KEY_CLEAR_TAB, 0);
		case KEY_CATAB:		return MessageForExtendedKey(EXTENDED_KEY_CLEAR_TABS, 0);
		case KEY_MOUSE:		// not convertible yet
		case KEY_RESIZE:	// not applicable
		case KEY_EVENT:		// not applicable
		default:
			return 0;
	}
}

void
Realizer::unicode_keypress(
	wint_t c
) {
	if (c < 0x01000000) {
		const uint32_t b(MessageForUCS3(c));
		vt.WriteInputMessage(b);
	}
}

void
Realizer::ncurses_keypress(
	wint_t k
) {
	const uint32_t b(MessageFromNCursesKey(k));
	vt.WriteInputMessage(b);
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_ncurses_realizer [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool wrong_way_up(false);
	bool display_only(false);
	try {
		popt::bool_definition wrong_way_up_option('\0', "wrong-way-up", "Display from bottom to top.", wrong_way_up);
		popt::bool_definition display_only_option('\0', "display-only", "Only render the display; do not send input.", display_only);
		popt::definition * top_table[] = {
			&wrong_way_up_option,
			&display_only_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{dirname}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing file name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * dirname(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}
	std::vector<struct kevent> ip;

	FileDescriptorOwner vt_dir_fd(open_dir_at(AT_FDCWD, dirname));
	if (0 > vt_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, dirname, std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileDescriptorOwner buffer_fd(open_read_at(vt_dir_fd.get(), "display"));
	if (0 > buffer_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileStar buffer_file(fdopen(buffer_fd.get(), "r"));
	if (!buffer_file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	buffer_fd.release();
	FileDescriptorOwner input_fd(display_only ? -1 : open_writeexisting_at(vt_dir_fd.get(), "input"));
	if (!display_only && input_fd.get() < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	vt_dir_fd.release();

	// Without this, ncursesw operates in 8-bit compatibility mode.
	std::setlocale(LC_ALL, "");

	if (!display_only)
		append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
	ReserveSignalsForKQueue kqueue_reservation(SIGWINCH, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, 0);
	append_event(ip, SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);

	VirtualTerminalBackEnd vt(dirname, buffer_file.release(), input_fd.release());
	append_event(ip, vt.query_buffer_fd(), EVFILT_VNODE, EV_ADD|EV_ENABLE|EV_CLEAR, NOTE_WRITE, 0, 0);
	append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_ADD|EV_DISABLE, 0, 0, 0);

	Realizer realizer(wrong_way_up, vt);

	const struct timespec immediate_timeout = { 0, 0 };

	while (true) {
		if (realizer.exit_signalled())
			break;
		realizer.handle_resize_event();
		realizer.handle_refresh_event();
		realizer.handle_update_event();

		struct kevent p[3];
		const int rc(kevent(queue, ip.data(), ip.size(), p, sizeof p/sizeof *p, vt.query_reload_needed() ? &immediate_timeout : 0));
		ip.clear();

		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
#if defined(__LINUX__) || defined(__linux__)
			if (EINVAL == error) continue;	// This works around a Linux bug when an inotify queue overflows.
			if (0 == error) continue;	// This works around another Linux bug.
#endif
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}

		if (0 == rc) {
			if (vt.query_reload_needed()) {
				vt.reload();
				realizer.set_refresh_needed();
			}
			continue;
		}

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_SIGNAL:
					realizer.handle_signal(e.ident);
					break;
				case EVFILT_VNODE:
					if (vt.query_buffer_fd() == static_cast<int>(e.ident)) 
						vt.set_reload_needed();
					break;
				case EVFILT_READ:
					if (STDIN_FILENO == e.ident)
						realizer.handle_stdin(e.data);
					break;
				case EVFILT_WRITE:
					if (vt.query_input_fd() == static_cast<int>(e.ident))
						vt.FlushMessages();
					break;
			}
		}

		if (vt.MessageAvailable()) {
			if (!vt.query_polling_for_write()) {
				append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_ENABLE, 0, 0, 0);
				vt.set_polling_for_write(true);
			}
		} else {
			if (vt.query_polling_for_write()) {
				append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_DISABLE, 0, 0, 0);
				vt.set_polling_for_write(false);
			}
		}
	}

	throw EXIT_SUCCESS;
}
