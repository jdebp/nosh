/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <stdint.h>
#include <sys/stat.h>
#if !defined(__LINUX__) && !defined(__linux__)
#include <sys/types.h>
#include "uuid.h"
#else
#include "uuid/uuid.h"
#endif
#include <unistd.h>
#include "utils.h"
#include "popt.h"
#include "host_id.h"
#include "machine_id.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
erase_machine_id [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "");

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

	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unexpected argument(s).");
		throw static_cast<int>(EXIT_USAGE);
	}

	using namespace machine_id;

	erase();

	umask(0033);
	write_fallbacks(prog);
	write_non_volatile(prog);

	const uint32_t hostid(calculate_host_id(the_machine_id));
	write_volatile_hostid(prog, hostid);
	write_non_volatile_hostid(prog, hostid);

	throw EXIT_SUCCESS;
}
