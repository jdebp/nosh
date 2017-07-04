/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <cerrno>
#include <unistd.h>
#include "utils.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
console_clear [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool c17bit(false);
	bool c18bit(false);
	try {
		popt::bool_definition c17bit_option('7', "7bit", "Use 7-bit C1 characters.", c17bit);
		popt::bool_definition c18bit_option('8', "8bit", "Use 8-bit C1 characters instead of UTF-8.", c18bit);
		popt::definition * top_table[] = {
			&c17bit_option,
			&c18bit_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * const csi(c17bit ? "\x1B[" : c18bit ? "\x9B" : "\xC2\x9B");
	// This order is important for PuTTY for some reason.
	std::cout << csi << "2J";			// ED
	std::cout << csi << "3J";			// xterm/PuTTY/Linux kernel extension
	throw EXIT_SUCCESS;
}
