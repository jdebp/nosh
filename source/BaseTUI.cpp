/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#define _XOPEN_SOURCE_EXTENDED
#include <csignal>
#include <cerrno>
#include <stdint.h>
#include <sys/ioctl.h>	// for struct winsize
#include <unistd.h>
#include <fcntl.h>
#if defined(__LINUX__) || defined(__linux__)
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif
#include "curses-const-fix.h"	// Must come after curses.h .
#include <term.h>
#include "ttyutils.h"
#include "BaseTUI.h"

/* Full-screen TUI **********************************************************
// **************************************************************************
*/

BaseTUI::BaseTUI(
) :	
	window(0),
	pending_resize_event(true),
	refresh_needed(true),
	update_needed(true),
	window_x(0),
	window_y(0),
	old_cursor_visibility(-1),
	cursor_visibility(2)
{
	initscr();
	raw();
	noecho();
	start_color();
	setup_default_colours(COLOR_GREEN, COLOR_BLACK);
	wbkgdset(stdscr, ' ' | A_DIM | COLOR_PAIR(0));

	// We're using a pad here, because our design is to keep the ncursesw window the same size as the display buffer.
	// The pad displays the whole buffer if our terminal is big enough, but if our terminal is too small, it displays a moving "window" onto the display buffer.
	window = newpad(1, 1);
	keypad(window, true);
	intrflush(window, false);
	nodelay(window, true);
}

BaseTUI::~BaseTUI(
) {
	delwin(window);
	endwin();
}

void
BaseTUI::handle_signal (
	int signo
) {
	switch (signo) {
		case SIGWINCH:	pending_resize_event = true; break;
	}
}

void
BaseTUI::handle_stdin (
	int n		///< number of characters available; can be 0 erroneously
) {
	do {
		wint_t c;
		switch (wget_wch(window, &c)) {
			case ERR:
				return;
			case OK:
				unicode_keypress(c);
				break;
			case KEY_CODE_YES:
				ncurses_keypress(c);
				break;
		}
	} while (n-- > 0);
}

void
BaseTUI::handle_resize_event (
) {
	if (pending_resize_event) {
		pending_resize_event = false;
		struct winsize size;
		if (0 <= tcgetwinsz_nointr(STDOUT_FILENO, size))
			resize(size.ws_row, size.ws_col);
		refresh_needed = true;
	}
}

void
BaseTUI::handle_refresh_event (
) {
	if (refresh_needed) {
		refresh_needed = false;
		redraw();
		refresh();
		update_needed = true;
	}
}

void
BaseTUI::handle_update_event (
) {
	if (update_needed) {
		update_needed = false;
		doupdate();
	}
}

void
BaseTUI::resize(
	int row,
	int col
) {
	resize_term(row, col);
	// We need to repaint the parts of stdscr that have just been exposed.
	clearok(stdscr, 1);
	refresh_needed = true;
}

unsigned
BaseTUI::page_rows() const
{
	return getmaxy(stdscr);
}

void
BaseTUI::set_cursor_visibility(
	int s
) {
	cursor_visibility = s;
}

/// \brief Refresh all of the window buffers onto the internal ncurses update buffer.
void
BaseTUI::refresh ()
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
	if (cursor_visibility > 0) {
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
	pnoutrefresh(window, window_y, window_x, screen_y, screen_x, screen_y + window_h - 1, screen_x + window_w - 1);
	if (old_cursor_visibility != cursor_visibility)
		curs_set(old_cursor_visibility = cursor_visibility);
}

void
BaseTUI::setup_default_colours(
	short fg,
	short bg
) {
#if defined(NCURSES_VERSION)
	assume_default_colors(fg, bg);
#else
	init_pair(0, fg, bg);
#endif
}
