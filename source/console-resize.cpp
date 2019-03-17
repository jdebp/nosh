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

// This must have static storage duration as we are using it in args.
static std::string columns_arg;

/* Main function ************************************************************
// **************************************************************************
*/

void
console_resize ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool lines_only(false);
	bool c1_7bit(false);
	bool c1_8bit(false);
	try {
		popt::bool_definition lines_only_option('l', "lines", "Only change the number of lines.", lines_only);
		popt::bool_definition c1_7bit_option('7', "7bit", "Use 7-bit C1 characters.", c1_7bit);
		popt::bool_definition c1_8bit_option('8', "8bit", "Use 8-bit C1 characters instead of UTF-8.", c1_8bit);
		popt::definition * top_table[] = {
			&lines_only_option,
			&c1_7bit_option,
			&c1_8bit_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "[COLS×]{ROWS}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing size.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * p(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}
	// We don't duplicate the numerical argument checking that console-control-sequence will do.
	// Rather, we just split the string at the × character.
	if (!lines_only) {
		const char *times(std::strrchr(p, static_cast<char>(0xD7)));
		if (!times) {
			times = std::strrchr(p, 'X');
			if (!times)
				times = std::strrchr(p, 'x');
		}
		if (!times) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, p, "Missing ×.");
			throw static_cast<int>(EXIT_USAGE);
		}
		columns_arg = std::string(p, times);
		p = ++times;
	}
	args.insert(args.end(), "console-control-sequence");
	if (c1_7bit) args.insert(args.end(), "--7bit");
	if (c1_8bit) args.insert(args.end(), "--8bit");
	if (!lines_only) {
		args.insert(args.end(), "--columns");
		args.insert(args.end(), columns_arg.c_str());
	}
	args.insert(args.end(), "--rows");
	args.insert(args.end(), p);
	next_prog = arg0_of(args);
}
