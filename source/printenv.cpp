/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <iostream>
#include "utils.h"
#include "ProcessEnvironment.h"
#include "popt.h"

static inline
std::string
swap_NUL_and_LF (
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator p(s.begin()), e(s.end()); e != p; ++p) {
		char c(*p);
		if ('\n' == c)
			c = '\0';
		else if ('\0' == c)
			c = '\n';
		r += c;
	}
	return r;
}

static
std::string
process (
	const std::string & s,
	bool print0,
	bool envdir,
	bool conf
) {
	return print0 ? s : envdir ? swap_NUL_and_LF(s) : conf ? quote_for_sh(s) : s;
}

/* Main function ************************************************************
// **************************************************************************
*/

void
printenv [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool print0(false), envdir(false), conf(false), full(false);
	try {
		popt::bool_definition print0_option('0', "print0", "Output environment contents as-is, with NUL termination.", print0);
		popt::bool_definition envdir_option('\0', "envdir", "Swap LF and NUL to yield output in a form suitable for envdir.", envdir);
		popt::bool_definition conf_option('\0', "conf", "Quote output in a form suitable for read-conf.", conf);
		popt::bool_definition full_option('f', "full", "Always print variable names.", full);
		popt::definition * top_table[] = {
			&print0_option,
			&envdir_option,
			&conf_option,
			&full_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "[var]");

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

	const char eol(print0 ? '\0' : '\n');
	if (args.empty()) {
		for (ProcessEnvironment::const_iterator i(envs.begin()), e(envs.end()); e != i; ++i) {
			std::cout << process(i->first, print0, envdir, conf) << '=' << process(i->second, print0, envdir, conf) << eol;
		}
	} else {
		bool not_found(false);
		for (std::vector<const char *>::const_iterator i(args.begin()), e(args.end()); e != i; ++i) {
			const std::string var(*i);
			ProcessEnvironment::const_iterator p(envs.find(var));
			if (envs.end() != p) {
				if (full)
					std::cout << process(p->first, print0, envdir, conf) << '=';
				std::cout << process(p->second, print0, envdir, conf) << eol;
			} else
				not_found = true;
		}
		if (not_found) throw EXIT_FAILURE;
	}
	throw EXIT_SUCCESS;
}
