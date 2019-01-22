/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
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
#include "ProcessEnvironment.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
vc_reset_tty ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	bool no_utf_8(false);
	bool no_tostop(false);
	bool hard_reset(false);
	bool set_text_mode(false);

	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition no_utf_8_option('\0', "no-utf8", "Do not set UTF-8 input mode.", no_utf_8);
		popt::bool_definition no_tostop_option('\0', "no-tostop", "Do not set the \"tostop\" line discipline flag.", no_tostop);
		popt::bool_definition hard_reset_option('\0', "hard-reset", "Reset the terminal before initializing it.", hard_reset);
		popt::bool_definition set_text_mode_option('\0', "text-mode", "Invoke the ioctl for setting text mode on kernel virtual console.", set_text_mode);
		popt::definition * top_table[] = {
			&no_utf_8_option,
			&no_tostop_option,
			&hard_reset_option,
			&set_text_mode_option,
		};
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

	args.insert(args.begin(), ";");
	args.insert(args.begin(), "8");
	args.insert(args.begin(), "--regtabs");
	if (hard_reset)
		args.insert(args.begin(), "--reset");
	else
		args.insert(args.begin(), "--soft-reset");
	args.insert(args.begin(), "--7bit");
	args.insert(args.begin(), "console-control-sequence");
	args.insert(args.begin(), "foreground");
	next_prog = arg0_of(args);
}
