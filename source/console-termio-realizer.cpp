/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#define _XOPEN_SOURCE_EXTENDED
#include <vector>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <clocale>
#include <cerrno>
#include <stdint.h>
#include "kqueue_common.h"
#include <sys/param.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "pack.h"
#include "popt.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "InputMessage.h"
#include "TerminalCapabilities.h"
#include "TUIDisplayCompositor.h"
#include "TUIOutputBase.h"
#include "TUIInputBase.h"
#include "VirtualTerminalBackEnd.h"
#include "SignalManagement.h"

/* Realizing a virtual terminal onto a set of physical devices **************
// **************************************************************************
*/

namespace {

class Realizer :
	public TerminalCapabilities,
	public TUIOutputBase,
	public TUIInputBase
{
public:
	Realizer(const ProcessEnvironment & envs, unsigned int quadrant, bool wrong_way_up, bool bold_as_colour, bool faint_as_colour, bool cursor_application_mode, bool calculator_application_mode, bool alternate_screen_buffer, VirtualTerminalBackEnd & vt, TUIDisplayCompositor & c);
	~Realizer();

	bool exit_signalled() const { return terminate_signalled||interrupt_signalled||hangup_signalled; }
	void handle_signal (int);
	void handle_stdin (int);
	bool has_idle_work();
	void do_idle_work();

protected:
	unsigned int quadrant;	///< 0, 1, 2, or 3
	unsigned short screen_y, screen_x;
	const bool wrong_way_up;	///< causes coordinate transforms between c and vt
	const bool has_pointer;
	sig_atomic_t terminate_signalled, interrupt_signalled, hangup_signalled, usr1_signalled, usr2_signalled;
	VirtualTerminalBackEnd & vt;

	virtual void redraw_new ();
	void position_vt_visible_area ();
	void compose_new_from_vt ();

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

Realizer::Realizer(
	const ProcessEnvironment & envs,
	unsigned int q,
	bool wwu,
	bool bc,
	bool fc,
	bool cursor_application_mode,
	bool calculator_application_mode,
	bool alternate_screen_buffer,
	VirtualTerminalBackEnd & t,
	TUIDisplayCompositor & comp
) : 
	TerminalCapabilities(envs),
	TUIOutputBase(*this, stdout, bc, fc, cursor_application_mode, calculator_application_mode, alternate_screen_buffer, comp),
	TUIInputBase(static_cast<const TerminalCapabilities &>(*this), stdin),
	quadrant(q),
	screen_y(0U),
	screen_x(0U),
	wrong_way_up(wwu),
	has_pointer(has_XTerm1006Mouse),
	terminate_signalled(false),
	interrupt_signalled(false),
	hangup_signalled(false),
	usr1_signalled(false),
	usr2_signalled(false),
	vt(t)
{
}

Realizer::~Realizer()
{
}

/// \brief Clip and position the visible portion of the terminal's display buffer.
inline
void
Realizer::position_vt_visible_area (
) {
	// Glue the terminal window to the edges of the display screen buffer.
	screen_y = !(quadrant & 0x02) && vt.query_h() < c.query_h() ? c.query_h() - vt.query_h() : 0;
	screen_x = (1U == quadrant || 2U == quadrant) && vt.query_w() < c.query_w() ? c.query_w() - vt.query_w() : 0;
	// Tell the VirtualTerminal the size of the visible window; it will position it itself according to cursor placement.
	vt.set_visible_area(c.query_h() - screen_y, c.query_w() - screen_x);
}

/// \brief Render the terminal's display buffer and cursor/pointer states onto the TUI compositor.
inline
void
Realizer::compose_new_from_vt () 
{
	for (unsigned short row(0U); row < vt.query_visible_h(); ++row) {
		const unsigned short source_row(vt.query_visible_y() + row);
		const unsigned short dest_row(screen_y + (wrong_way_up ? vt.query_visible_h() - row - 1U : row));
		for (unsigned short col(0U); col < vt.query_visible_w(); ++col)
			c.poke(dest_row, col + screen_x, vt.at(source_row, vt.query_visible_x() + col));
	}
	const CursorSprite::attribute_type a(vt.query_cursor_attributes());
	// If the cursor is invisible, we are not guaranteed that the VirtualTerminal has kept the visible area around it.
	if (CursorSprite::VISIBLE & a) {
		const unsigned short cursor_y(screen_y + (wrong_way_up ? vt.query_visible_h() - vt.query_cursor_y() - 1U : vt.query_cursor_y()) - vt.query_visible_y());
		c.move_cursor(cursor_y, vt.query_cursor_x() - vt.query_visible_x() + screen_x);
	}
	c.set_cursor_state(a, vt.query_cursor_glyph());
	if (has_pointer)
		c.set_pointer_attributes(vt.query_pointer_attributes());
}

void
Realizer::redraw_new (
) {
	erase_new_to_backdrop();
	position_vt_visible_area();
	compose_new_from_vt();
}

void
Realizer::ExtendedKey(
	uint_fast16_t k,
	uint_fast8_t m
) {
	vt.WriteInputMessage(MessageForExtendedKey(k, m));
}

void
Realizer::FunctionKey(
	uint_fast16_t k,
	uint_fast8_t m
) {
	vt.WriteInputMessage(MessageForFunctionKey(k, m));
}

void
Realizer::UCS3(
	uint_fast32_t character
) {
	vt.WriteInputMessage(MessageForUCS3(character));
}

void
Realizer::Accelerator(
	uint_fast32_t character
) {
	vt.WriteInputMessage(MessageForAcceleratorKey(character));
}

void 
Realizer::MouseMove(
	uint_fast16_t row,
	uint_fast16_t col,
	uint8_t modifiers
) {
	if (c.change_pointer_row(row)) {
		if (wrong_way_up)
			row = c.query_h() - row - 1U;
		vt.WriteInputMessage(MessageForMouseRow(row, modifiers));
		set_update_needed();
	}
	if (c.change_pointer_col(col)) {
		vt.WriteInputMessage(MessageForMouseColumn(col, modifiers));
		set_update_needed();
	}
}

void 
Realizer::MouseWheel(
	uint_fast8_t wheel,
	int_fast8_t value,
	uint_fast8_t modifiers
) {
	vt.WriteInputMessage(MessageForMouseWheel(wheel,value,modifiers));
}

void 
Realizer::MouseButton(
	uint_fast8_t button,
	uint_fast8_t value,
	uint_fast8_t modifiers
) {
	vt.WriteInputMessage(MessageForMouseButton(button,value,modifiers));
}

void
Realizer::handle_stdin (
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
Realizer::handle_signal (
	int signo
) {
	switch (signo) {
		case SIGWINCH:	set_resized(); break;
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
		case SIGUSR1:	usr1_signalled = true; break;
		case SIGUSR2:	usr2_signalled = true; break;
	}
}

inline
bool
Realizer::has_idle_work(
) {
	return vt.query_reload_needed();
}

void
Realizer::do_idle_work(
) {
	if (vt.query_reload_needed()) {
		vt.reload();
		set_refresh_needed();
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_termio_realizer [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	unsigned long quadrant(3U);
	bool wrong_way_up(false);
	bool display_only(false);
	bool bold_as_colour(false);
	bool faint_as_colour(false);
	bool cursor_application_mode(false);
	bool calculator_application_mode(false);
	bool no_alternate_screen_buffer(false);

	try {
		popt::unsigned_number_definition quadrant_option('\0', "quadrant", "number", "Position the terminal in quadrant 0, 1, 2, or 3.", quadrant, 0);
		popt::bool_definition wrong_way_up_option('\0', "wrong-way-up", "Display from bottom to top.", wrong_way_up);
		popt::bool_definition display_only_option('\0', "display-only", "Only render the display; do not send input.", display_only);
		popt::bool_definition bold_as_colour_option('\0', "bold-as-colour", "Forcibly render boldface as a colour brightness change.", bold_as_colour);
		popt::bool_definition faint_as_colour_option('\0', "faint-as-colour", "Forcibly render faint as a colour brightness change.", faint_as_colour);
		popt::bool_definition cursor_application_mode_option('\0', "cursor-keypad-application-mode", "Set the cursor keypad to application mode instead of normal mode.", cursor_application_mode);
		popt::bool_definition calculator_application_mode_option('\0', "calculator-keypad-application-mode", "Set the calculator keypad to application mode instead of normal mode.", calculator_application_mode);
		popt::bool_definition no_alternate_screen_buffer_option('\0', "no-alternate-screen-buffer", "Prevent switching to the XTerm alternate screen buffer.", no_alternate_screen_buffer);
		popt::definition * top_table[] = {
			&quadrant_option,
			&wrong_way_up_option,
			&display_only_option,
			&bold_as_colour_option,
			&faint_as_colour_option,
			&cursor_application_mode_option,
			&calculator_application_mode_option,
			&no_alternate_screen_buffer_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{virtual-terminal}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing virtual terminal directory name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * vt_dirname(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}
	std::vector<struct kevent> ip;

	FileDescriptorOwner vt_dir_fd(open_dir_at(AT_FDCWD, vt_dirname));
	if (0 > vt_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, vt_dirname, std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileDescriptorOwner buffer_fd(open_read_at(vt_dir_fd.get(), "display"));
	if (0 > buffer_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, vt_dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileStar buffer_file(fdopen(buffer_fd.get(), "r"));
	if (!buffer_file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, vt_dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	buffer_fd.release();
	FileDescriptorOwner input_fd(display_only ? -1 : open_writeexisting_at(vt_dir_fd.get(), "input"));
	if (!display_only && input_fd.get() < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, vt_dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	vt_dir_fd.release();

	if (!display_only)
		append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGWINCH, SIGUSR1, SIGUSR2, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, 0);
	append_event(ip, SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);

	VirtualTerminalBackEnd vt(vt_dirname, buffer_file.release(), input_fd.release());
	append_event(ip, vt.query_buffer_fd(), EVFILT_VNODE, EV_ADD|EV_ENABLE|EV_CLEAR, NOTE_WRITE, 0, 0);
	append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_ADD|EV_DISABLE, 0, 0, 0);
	TUIDisplayCompositor compositor(false /* no software cursor */, 24, 80);
	Realizer realizer(envs, quadrant, wrong_way_up, bold_as_colour, faint_as_colour, cursor_application_mode, calculator_application_mode, !no_alternate_screen_buffer, vt, compositor);

	const struct timespec immediate_timeout = { 0, 0 };

	while (true) {
		if (realizer.exit_signalled())
			break;
		realizer.handle_resize_event();
		realizer.handle_refresh_event();
		realizer.handle_update_event();

		struct kevent p[128];
		const int rc(kevent(queue.get(), ip.data(), ip.size(), p, sizeof p/sizeof *p, realizer.has_idle_work() ? &immediate_timeout : 0));
		ip.clear();

		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}

		if (0 == rc) {
			realizer.do_idle_work();
			continue;
		}

		for (std::size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_VNODE:
					if (vt.query_buffer_fd() == static_cast<int>(e.ident)) 
						vt.set_reload_needed();
					break;
				case EVFILT_SIGNAL:
					realizer.handle_signal(e.ident);
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
