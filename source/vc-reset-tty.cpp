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
#include "popt.h"
#include "utils.h"

/* terminal processing ******************************************************
// **************************************************************************
// This program only targets virtual terminals provided by system console drivers.
// So we just hardwire what we KNOW to be the value appropriate to each system.
*/

#define ESC "\033"
#define CSI ESC "["

// The string that hard resets the terminal from a jammed state, ready for initialization.
static inline
const char *
reset_string()
{
	return
#if defined(__LINUX__) || defined(__linux__)
		ESC "c"		// reset
		ESC "]R"	// (OSC) reset palette
#elif defined(__FreeBSD__) || defined(__DragonFly__)
		// This isn't quite correct, but it isn't worth fixing.
		// cons25 has fallen into desuetude and the new (2010) emulator is xterm.
		ESC "c"		// reset
		CSI "x"		// reset palette
#elif defined(__NetBSD__)
		CSI "?3l"	// set 80 columns (DECCOLM)
#else
		// OpenBSD can change the emulator at kernel configuration time and at runtime.
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
		ESC "H"		// set tab stop at current column
#elif defined(__FreeBSD__) || defined(__DragonFly__)
		// This isn't quite correct, but it isn't worth fixing.
		// cons25 has fallen into desuetude and the new (2010) emulator is xterm.
		CSI "!p"	//
		CSI "?3;4l"	// set 80 columns (DECCOLM) and jump scrolling (DECSCLM)
		CSI "4l"	// reset insert mode (i.e. set overstrike)
		ESC ">"		// set numeric keypad mode
		CSI "3g"	// clear all tab stops
		ESC "H"		// set tab stop at current column
#elif defined(__NetBSD__)
		// This isn't quite correct, but it isn't worth fixing.
		// We could attempt to auto-detect the use of cons25, but it has fallen into desuetude.
		CSI "r"		// reset window to whole screen (DECSTBM)
		CSI "25;1H"	// move to line 25 column 1
		CSI "3g"	// clear all tab stops
		ESC "H"		// set tab stop at current column
#else
		// OpenBSD can change the emulator at kernel configuration time and at runtime.
		// MacOS X doesn't have virtual consoles.
#		error "Don't know what the terminal type for your console driver is."
#endif
	;
}

/* Main function ************************************************************
// **************************************************************************
*/

void
vc_reset_tty ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	bool no_utf_8(false);
	bool no_newline(false);
	bool no_tostop(false);
	bool no_reset(false);

	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition no_newline_option('\0', "no-newline", "Do not print an initial newline.", no_newline);
		popt::bool_definition no_utf_8_option('\0', "no-utf8", "Do not set UTF-8 input mode.", no_utf_8);
		popt::bool_definition no_tostop_option('\0', "no-tostop", "Do not set the \"tostop\" line discipline flag.", no_tostop);
		popt::bool_definition no_reset_option('\0', "no-reset", "Initialize, do not reset, the terminal.", no_reset);
		popt::definition * top_table[] = {
			&no_newline_option,
			&no_utf_8_option,
			&no_tostop_option,
			&no_reset_option
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

	if (!no_reset)
		std::fputs(reset_string(), stdout);
	std::fputs(initialization_string(), stdout);
	if (!no_newline)
		std::fputc('\n', stdout);
	std::fflush(stdout);
}