/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include "utils.h"
#include "ProcessEnvironment.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

// This must have static storage duration as we are using it in args.
static std::string login_storage;
static const std::string dash("-");

void
command_exec ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	bool clear(false);
	bool login(false);

	const char * prog(basename_of(args[0]));
	const char * arg0(0);
	try {
		popt::string_definition arg0_option('a', "arg0", "string", "Specify arg[0].", arg0);
		popt::bool_definition clear_option('c', "clear", "Clear the environment.", clear);
		popt::bool_definition login_option('l', "login", "Prefix a dash to arg[0].", login);
		popt::definition * top_table[] = {
			&clear_option,
			&login_option,
			&arg0_option
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

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing next program.");
		throw static_cast<int>(EXIT_USAGE);
	}
	if (arg0) 
		args[0] = arg0;
	if (login && !args.empty()) {
		login_storage = dash + args[0];
		args[0] = login_storage.c_str();
	}
	if (clear)
		envs.clear();
}
