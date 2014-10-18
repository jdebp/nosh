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
#include <cerrno>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/event.h>
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
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

static const short VGAtoncurses[8] = {
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
	// ncurses reserves colour pair 0.
	return 1 + (VGAtoncurses[f] | (VGAtoncurses[b] << 3));
}

static inline
short
colour_pair_from_attribute (
	unsigned char a
) {
	const unsigned char f((a >> 0) & 7U);
	const unsigned char b((a >> 4) & 7U);
	return colour_pair(f,b);
}

static inline
attr_t
attributes_from_attribute (
	unsigned char a
) {
	return ((a & 0x80) ? A_BLINK : 0) | ((a & 0x08) ? A_BOLD : 0);
}

static inline
void
setup_colours ()
{
	for (unsigned char f(0U); f < 8U; ++f)
		for (unsigned char b(0U); b < 8U; ++b)
			init_pair(colour_pair(f,b), VGAtoncurses[f], VGAtoncurses[b]);
}

static sig_atomic_t resize_needed(0);

static void sig_winch(int) { resize_needed = 1; }

static inline
void
redraw (
	WINDOW * window,
	int vcsa_fd
) {
	werase(window);
	lseek(vcsa_fd, SEEK_SET, 0);
	unsigned char header[4] = { 0, 0, 0, 0 };
	read(vcsa_fd, header, sizeof header);
	const unsigned rows(header[0]), cols(header[1]), x(header[2]), y(header[3]);
	for (unsigned row(0); row < rows; ++row) {
		for (unsigned col(0); col < cols; ++col) {
			unsigned char ca[2] = { 0, 0 };
			read(vcsa_fd, ca, sizeof ca);
			wchar_t ws[2] = { btowc(ca[0]), L'\0' };
			cchar_t cc;
			setcchar(&cc, ws, attributes_from_attribute(ca[1]), colour_pair_from_attribute(ca[1]), 0);
			mvwadd_wch(window, row, col, &cc);
		}
	}
	wmove(window, y, x);
	wrefresh(window);
}

void
vc_ncurses_io ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "directory");

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
		throw EXIT_FAILURE;
	}
	const char * filename(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing FIFO name.");
		throw EXIT_FAILURE;
	}
	const char * fifoname(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	const int vcsa_fd(open_read_at(AT_FDCWD, filename));
	if (vcsa_fd < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
	const int input_fd(open_writeexisting_at(AT_FDCWD, fifoname));
	if (input_fd < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fifoname, std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct sigaction sa;
	sa.sa_flags=0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler=sig_winch;
	sigaction(SIGWINCH,&sa,NULL);

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct kevent p[2];
	EV_SET(&p[0], vcsa_fd, EVFILT_VNODE, EV_ADD, NOTE_WRITE, 0, 0);
	EV_SET(&p[1], STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
	if (0 > kevent(queue, p, sizeof p/sizeof *p, 0, 0, 0)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
		throw EXIT_FAILURE;
	}

	WINDOW * window(initscr());
	raw();
	keypad(window, false);
	intrflush(window, false);
	nodelay(window, true);
	noecho();
	start_color();
	setup_colours();
	wbkgdset(stdscr, ' ' | A_DIM | COLOR_PAIR(0));

	redraw(window, vcsa_fd);
	while (true) {
		if (resize_needed) {
			struct winsize size;
			if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
				resize_term(size.ws_row, size.ws_col);
				redraw(window, vcsa_fd);
				wrefresh(curscr);
			}
			resize_needed = 0;
		}

		const int rc(kevent(queue, p, 0, p, sizeof p/sizeof *p, 0));

		if (0 > rc) {
			if (resize_needed) continue;
			const int error(errno);
			if (EINTR == error) continue;
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			if (EVFILT_VNODE == p[i].filter && vcsa_fd == p[i].ident)
				redraw(window, vcsa_fd);
			if (EVFILT_READ == p[i].filter && STDIN_FILENO == p[i].ident) {
				const int c(wgetch(window));
				if (c != ERR) {
					const char b(c);
					write(input_fd, &b, sizeof b);
				}
			}
		}
	}

	endwin();
	throw EXIT_SUCCESS;
}
