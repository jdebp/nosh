/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include "popt.h"
#include "utils.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
line_banner ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool nvt(false);
	try {
		popt::bool_definition nvt_option('\0', "NVT", "Use the Network Virtual Terminal newline convention.", nvt);
		popt::definition * top_table[] = {
			&nvt_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{banner} {prog}");
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

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing banner.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * banner(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	std::fputs(banner, stdout);
	if (nvt) std::fputc('\r', stdout);
	std::fputc('\n', stdout);
	std::fflush(stdout);
}
