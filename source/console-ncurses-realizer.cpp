/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
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
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#if defined(__LINUX__) || defined(__linux__)
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif
#include <term.h>
#include "utils.h"
#include "fdutils.h"
#include "ttyutils.h"
#include "popt.h"
#include "CharacterCell.h"
#include "InputMessage.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "SignalManagement.h"

/* Support routines  ********************************************************
// **************************************************************************
*/

static inline
uint32_t
MessageFromNCursesKey (
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

static inline
short
colour_pair (
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

static inline
void
setup_colours ()
{
	for (unsigned char f(0U); f < 8U; ++f)
		for (unsigned char b(0U); b < 8U; ++b) {
			const short p(colour_pair(f,b));
#if defined(NCURSES_VERSION)
			if (p)
				init_pair(p, CGAtoncurses[f], CGAtoncurses[b]);
			else
				assume_default_colors(CGAtoncurses[f], CGAtoncurses[b]);
#else
			init_pair(p, CGAtoncurses[f], CGAtoncurses[b]);
#endif
		}
}

#if 0	// This is old code and can be removed.
static inline
short
colour_pair_from_cga_attribute (
	unsigned char a
) {
	const unsigned char f((a >> 0) & 7U);
	const unsigned char b((a >> 4) & 7U);
	return colour_pair(f,b);
}

static inline
attr_t
attributes_from_cga_attribute (
	unsigned char a
) {
	return ((a & 0x80) ? A_BLINK : 0) | ((a & 0x08) ? A_BOLD : 0);
}
#endif

// ncursesw doesn't have a real 32-bit colour mechanism.
// The RGB system that it does have is in fact a colour palette, and unusable for our purposes.

// So we map the 32-bit colours used in the display buffer into 3-bit RGB (which is them mapped to the 3-bit BGR used by ncursesw) using a "greatest intensity wins" approach.
static inline
unsigned char
cga_colour_from_argb (
	unsigned char /*alpha*/,
	unsigned char red,
	unsigned char green,
	unsigned char blue
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

	if (red < green) {
		// Something with no red.
		return	green < blue ? CGA_BLUE :
			blue < green ? CGA_GREEN :
			CGA_CYAN;
	} else
	if (green < red) {
		// Something with no green.
		return	red < blue ? CGA_BLUE :
			blue < red ? CGA_RED :
			CGA_MAGENTA;
	} else
	{
		// Something with equal red and green.
		return	red < blue ? CGA_BLUE :
			blue < red ? CGA_YELLOW :
			red ? CGA_WHITE :
			CGA_BLACK;
	}
}

static inline
attr_t
attributes_from_cell(
	unsigned char a
) {
	return	(a & CharacterCell::BOLD ? A_BOLD : 0) |
		(a & CharacterCell::UNDERLINE ? A_UNDERLINE : 0) |
		(a & CharacterCell::BLINK ? A_BLINK : 0) |
		(a & CharacterCell::INVERSE ? A_REVERSE : 0) |
		(a & CharacterCell::INVISIBLE ? A_INVIS : 0) |
		(a & CharacterCell::FAINT ? A_DIM : 0) |
		0;
}

static sig_atomic_t resize_needed(false);

static
void
handle_signal (
	int signo
) {
	switch (signo) {
		case SIGWINCH:	resize_needed = true; break;
	}
}

static int old_cursor_state(-1), cursor_state(2);

enum { CELL_LENGTH = 16U, HEADER_LENGTH = 16U };

static WINDOW * window(0);

/// \brief Pull the display buffer from file into the ncurses window, but don't output anything.
static
void
redraw (
	FILE * buffer_file
) {
	werase(window);
	std::fseek(buffer_file, 4, SEEK_SET);
	uint16_t header1[4] = { 0, 0, 0, 0 };
	std::fread(header1, sizeof header1, 1U, buffer_file);
	uint8_t header2[4] = { 0, 0, 0, 0 };
	std::fread(header2, sizeof header2, 1U, buffer_file);
	std::fseek(buffer_file, HEADER_LENGTH, SEEK_SET);
	const unsigned cols(header1[0]), rows(header1[1]), x(header1[2]), y(header1[3]);
	wresize(window, rows, cols);
	const unsigned ctype(header2[0]), cattr(header2[1]);
	for (unsigned row(0); row < rows; ++row) {
		for (unsigned col(0); col < cols; ++col) {
			unsigned char b[CELL_LENGTH] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			std::fread(b, sizeof b, 1U, buffer_file);
			wchar_t ws[2] = { L'\0', L'\0' };
			memcpy(&ws[0], &b[8], 4);
			const unsigned char fg_cga(cga_colour_from_argb(b[0], b[1], b[2], b[3]));
			const unsigned char bg_cga(cga_colour_from_argb(b[4], b[5], b[6], b[7]));
			cchar_t cc;
			setcchar(&cc, ws, attributes_from_cell(b[12]), colour_pair(fg_cga, bg_cga), 0);
			// This doesn't necessarily do the right thing with control, combining, and zero-width code points, but they are unlikely to occur in the buffer in the first place as the terminal emulator will have dealt with them.
			mvwadd_wch(window, row, col, &cc);
		}
	}
	wmove(window, y, x);
	if (CursorSprite::VISIBLE != (cattr & CursorSprite::VISIBLE))
		cursor_state = 0;
	else if (CursorSprite::UNDERLINE == ctype)
		cursor_state = 1;
	else
		cursor_state = 2;
}

static int window_x(0), window_y(0);

/// \brief Actually render the window onto the terminal.
static
void
repaint ()
{
	const int screen_w(getmaxx(stdscr)), screen_h(getmaxy(stdscr));
	const int buffer_w(getmaxx(window)), buffer_h(getmaxy(window));
	const int cursor_x(getcurx(window)), cursor_y(getcury(window));
	// From the size of the buffer and the size of the screen we now calculate the window rectangle in the buffer and where to display it on the screen.
	const int window_w(buffer_w < screen_w ? buffer_w : screen_w);
	const int window_h(buffer_h < screen_h ? buffer_h : screen_h);
	if (window_y + window_h > buffer_h) window_y = buffer_h - window_h;
	if (window_x + window_w > buffer_w) window_x = buffer_w - window_w;
	// When programs repaint the screen the cursor is instantaneously all over the place, leading to the window scrolling all over the shop.
	// But some programs, like vim, make the cursor invisible during the repaint in order to reduce cursor flicker.
	// We take advantage of this by only scrolling the screen to include the cursor position if the cursor is actually visible.
	if (cursor_state > 0) {
		// The window includes the cursor position.
		if (window_y > cursor_y) window_y = cursor_y; else if (window_y + window_h <= cursor_y) window_y = cursor_y - window_h + 1;
		if (window_x > cursor_x) window_x = cursor_x; else if (window_x + window_w <= cursor_x) window_x = cursor_x - window_w + 1;
	}
	const int screen_y(window_h < screen_h ? screen_h - window_h : 0);	// Glue the window to the bottom edge of the screen.
	const int screen_x(/*window_w < screen_w ? screen_w - window_w :*/ 0);	// Glue the window to the left-hand edge of the screen.
	// Don't use wclear, as that causes unnecessary whole screen repaints with every little change.
	werase(stdscr);
	// Since we're moving the window around on the screen, we need to paint those parts of stdscr that get exposed from where the window was before.
	wnoutrefresh(stdscr);
	prefresh(window, window_y, window_x, screen_y, screen_x, screen_y + window_h - 1, screen_x + window_w - 1);
	if (old_cursor_state != cursor_state)
		curs_set(old_cursor_state = cursor_state);
}

static inline
void
resize(int row, int col) 
{
	resize_term(row, col);
	// We need to repaint the parts of stdscr that have just been exposed.
	clearok(stdscr, 1);
	repaint();
}

static inline
void
initialize()
{
	initscr();
	raw();
	noecho();
	start_color();
	setup_colours();
	wbkgdset(stdscr, ' ' | A_DIM | COLOR_PAIR(0));

	// We're using a pad here, because our design is to keep the ncursesw window the same size as the display buffer.
	// The pad displays the whole buffer if our terminal is big enough, but if our terminal is too small, it displays a moving "window" onto the display buffer.
	window = newpad(1, 1);
	keypad(window, true);
	intrflush(window, false);
	nodelay(window, true);
}

static inline
void
closedown()
{
	delwin(window);
	endwin();
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
	bool display_only(false);
	try {
		popt::bool_definition display_only_option('\0', "display-only", "Only render the display; do not send input.", display_only);
		popt::definition * top_table[] = {
			&display_only_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "dirname");

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

	FileDescriptorOwner dir_fd(open_dir_at(AT_FDCWD, dirname));
	if (0 > dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, dirname, std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileDescriptorOwner input_fd(display_only ? -1 : open_writeexisting_at(dir_fd.get(), "input"));
	if (!display_only && input_fd.get() < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileDescriptorOwner buffer_fd(open_read_at(dir_fd.get(), "display"));
	if (0 > buffer_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	const FileStar buffer_file(fdopen(buffer_fd.get(), "r"));
	if (!buffer_file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	buffer_fd.release();

	// Without this, ncursesw operates in 8-bit compatibility mode.
	std::setlocale(LC_ALL, "");

	ReserveSignalsForKQueue kqueue_reservation(SIGWINCH, 0);

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct kevent p[3];
	{
		std::size_t index(0U);
		set_event(&p[index++], fileno(buffer_file), EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_WRITE, 0, 0);
		set_event(&p[index++], SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		if (!display_only)
			set_event(&p[index++], STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
		if (0 > kevent(queue, p, index, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	initialize();

	redraw(buffer_file);
	repaint();
	while (true) {
		if (resize_needed) {
			resize_needed = false;
			struct winsize size;
			if (tcgetwinsz_nointr(STDOUT_FILENO, size) == 0) {
				resize(size.ws_row, size.ws_col);
			}
		}

		const int rc(kevent(queue, p, 0, p, sizeof p/sizeof *p, 0));

		if (0 > rc) {
			if (resize_needed) continue;
			const int error(errno);
			if (EINTR == error) continue;
#if defined(__LINUX__) || defined(__linux__)
			if (EINVAL == error) continue;	// This works around a Linux bug when an inotify queue overflows.
			if (0 == error) continue;	// This works around another Linux bug.
#endif
			closedown();
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			if (EVFILT_SIGNAL == e.filter)
				handle_signal(e.ident);
			if (EVFILT_VNODE == e.filter && fileno(buffer_file) == static_cast<int>(e.ident)) {
				redraw(buffer_file);
				repaint();
			}
			if (EVFILT_READ == e.filter && STDIN_FILENO == e.ident) {
				wint_t c;
				switch (wget_wch(window, &c)) {
					case ERR:	break;
					case OK:
						if (c < 0x01000000) {
							const uint32_t b(MessageForUCS3(c));
							write(input_fd.get(), &b, sizeof b);
						}
						break;
					case KEY_CODE_YES:
					{
						const uint32_t b(MessageFromNCursesKey(c));
						write(input_fd.get(), &b, sizeof b);
						break;
					}
				}
			}
		}
	}

	closedown();
	throw EXIT_SUCCESS;
}
