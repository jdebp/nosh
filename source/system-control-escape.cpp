/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include "utils.h"
#include "popt.h"

/* System control subcommands ***********************************************
// **************************************************************************
*/

void
escape [[gnu::noreturn]] (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	const char * prefix("");
	bool alt_escape(false), ext_escape(true);
	try {
		bool no_ext_escape(false);
		popt::string_definition prefix_option('p', "prefix", "string", "Prefix each name with this (template) name.", prefix);
		popt::bool_definition alt_escape_option('\0', "alt-escape", "Use an alternative escape algorithm.", alt_escape);
		popt::bool_definition no_ext_escape_option('\0', "no-ext-escape", "Do not use extended escape sequences.", no_ext_escape);
		popt::definition * main_table[] = {
			&alt_escape_option,
			&no_ext_escape_option,
			&prefix_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "{service(s)...}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		ext_escape = !no_ext_escape;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing name(s).");
		throw static_cast<int>(EXIT_USAGE);
	}

	for (std::vector<const char *>::const_iterator b(args.begin()), e(args.end()), i(b); e != i; ++i) {
		const std::string escaped(systemd_name_escape(alt_escape, ext_escape, *i));
		if (b != i) std::cout.put(' ');
		std::cout << prefix << escaped;
	}
	std::cout.put('\n');

	throw EXIT_SUCCESS;
}

void
systemd_escape [[gnu::noreturn]] (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	std::string prefix;
	const char * suffix("");
	try {
		const char * t(0);
		popt::string_definition template_option('t', "template", "string", "Instantiate this template with each name.", t);
		popt::definition * main_table[] = {
			&template_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "{service(s)...}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		if (t) {
			if (const char * at = std::strchr(t, '@')) {
				prefix = std::string(t, at + 1);
				suffix = at + 1;
			} else
				prefix = t;
		}
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing name(s).");
		throw static_cast<int>(EXIT_USAGE);
	}

	for (std::vector<const char *>::const_iterator b(args.begin()), e(args.end()), i(b); e != i; ++i) {
		const std::string escaped(systemd_name_escape(false, false, *i));
		if (b != i) std::cout.put(' ');
		std::cout << prefix << escaped << suffix;
	}
	std::cout.put('\n');

	throw EXIT_SUCCESS;
}
