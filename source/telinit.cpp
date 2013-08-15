/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstddef>
#include <cstdlib>
#include <cerrno>
#include "utils.h"
#include "popt.h"

/* The telinit shim *********************************************************
// **************************************************************************
*/

void
telinit ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "0|1|5|6|s|h");

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

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing command name.");
		throw EXIT_FAILURE;
	}
	const char * command(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	const char * n = 0;
	switch (*command) {
		case '0':
			n = "poweroff";
			break;
		case 's':
		case 'S':
		case '1':
			n = "rescue";
			break;
		case 'm':
		case 'M':
		case '5':
			n = "normal";
			break;
		case '6':
			n = "reboot";
			break;
		case 'h':
		case 'H':
			n = "halt";
			break;
		default:
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, command, "Unsupported command.");
			throw EXIT_FAILURE;
	}

	args.insert(args.begin(), n);
	next_prog = arg0_of(args);
}
