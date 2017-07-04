/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#if defined(__LINUX__) || defined(__linux__)
#include <sys/kd.h>
#endif
#include "popt.h"
#include "utils.h"
#include "ttyutils.h"

/* terminal processing ******************************************************
// **************************************************************************
// This program only targets virtual terminals provided by system console drivers.
// So we just hardwire what we KNOW to be the value appropriate to each system.
*/

#define ESC "\033"
#define CSI ESC "["
#if defined(__LINUX__) || defined(__linux__)
#define ST  ESC "\\"
#define OSC ESC "]"
#endif

// The string that hard resets the terminal from a jammed state, ready for initialization.
static inline
const char *
reset_string()
{
	return
#if defined(__LINUX__) || defined(__linux__)
		ESC "c"		// reset
		OSC "R" ST	// reset palette (modified to be ECMA-48 conformant)
#elif defined(__FreeBSD__) || defined(__DragonFly__)
		// This isn't quite correct, but it isn't worth fixing.
		// cons25 has fallen into desuetude and the new (2010) emulator is xterm.
		ESC "c"		// reset
		CSI "x"		// reset palette
#elif defined(__NetBSD__)
		CSI "?3l"	// set 80 columns (DECCOLM)
#elif defined(__OpenBSD__)
		ESC "c"		// reset
#else
		// MacOS X doesn't have virtual consoles.
#		error "Don't know what the terminal type for your console driver is."
#endif
	;
}

// The string that merely (re-)initializes the terminal from an unknown state.
static inline
const char *
initialization_string()
{
	return
#if defined(__LINUX__) || defined(__linux__)
		CSI "3g"	// clear all tab stops
#elif defined(__FreeBSD__) || defined(__DragonFly__)
		// This isn't quite correct, but it isn't worth fixing.
		// cons25 has fallen into desuetude and the new (2010) emulator is xterm.
		CSI "!p"	// soft reset (DECSTR)
		CSI "?3;4l"	// set 80 columns (DECCOLM) and jump scrolling (DECSCLM)
		CSI "4l"	// reset insert mode (i.e. set overstrike)
		ESC ">"		// set numeric keypad mode
		CSI "3g"	// clear all tab stops
#elif defined(__NetBSD__)
		// This isn't quite correct, but it isn't worth fixing.
		// We could attempt to auto-detect the use of cons25, but it has fallen into desuetude.
		CSI "r"		// reset window to whole screen (DECSTBM)
		CSI "3g"	// clear all tab stops
#elif defined(__OpenBSD__)
		CSI "3g"	// clear all tab stops
#else
		// MacOS X doesn't have virtual consoles.
#		error "Don't know what the terminal type for your console driver is."
#endif
	;
}

// The string that sets the default tabstops.
static inline
std::string
default_tabs_string ()
{
	static winsize size;
	std::string r;
	if (0 <= tcgetwinsz_nointr(STDOUT_FILENO, size)) {
		r += '\r';
		for (unsigned i(8U); i < size.ws_col; i += 8U) {	// Each new tabstop is set AFTER 8 more columns.
			r += 
				" "" "" "" "" "" "" "" "	// Hard tabs are always every 8 columns.
				ESC "H"		// set tab stop at current column
			;
		}
		r += '\r';
	}
	return r;
}

/* Main function ************************************************************
// **************************************************************************
*/

void
vc_reset_tty ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	bool no_utf_8(false);
	bool no_newline(false);
	bool no_tostop(false);
	bool hard_reset(false);
	bool set_text_mode(false);

	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition no_newline_option('\0', "no-newline", "Do not print an initial newline.", no_newline);
		popt::bool_definition no_utf_8_option('\0', "no-utf8", "Do not set UTF-8 input mode.", no_utf_8);
		popt::bool_definition no_tostop_option('\0', "no-tostop", "Do not set the \"tostop\" line discipline flag.", no_tostop);
		popt::bool_definition hard_reset_option('\0', "hard-reset", "Reset the terminal before initializing it.", hard_reset);
		popt::bool_definition set_text_mode_option('\0', "text-mode", "Invoke the ioctl for setting text mode on kernel virtual console.", set_text_mode);
		popt::definition * top_table[] = {
			&no_newline_option,
			&no_utf_8_option,
			&no_tostop_option,
			&hard_reset_option,
			&set_text_mode_option
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

	if (!isatty(STDOUT_FILENO)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "stdout", std::strerror(error));
		throw EXIT_FAILURE;
	}

	tcsetattr_nointr(STDOUT_FILENO, TCSAFLUSH, sane(no_tostop, no_utf_8));

	if (set_text_mode) {
#if defined(__LINUX__) || defined(__linux__)
		if (0 > ioctl(STDOUT_FILENO, KDSETMODE, KD_TEXT)) {
			const int error(errno);
			std::fprintf(stderr, "%s: WARNING: %s: %s: %s\n", prog, "KDSETMODE", "stdout", std::strerror(error));
		}
#endif
	}
	if (hard_reset)
		std::fputs(reset_string(), stdout);
	std::fputs(initialization_string(), stdout);
	std::fflush(stdout);
	std::fputs(default_tabs_string().c_str(), stdout);
	std::fflush(stdout);
	if (!no_newline)
		std::fputc('\n', stdout);
	std::fflush(stdout);
}
