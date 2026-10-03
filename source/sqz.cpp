/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <iostream>
#include <unistd.h>
#include "popt.h"
#include "FileStar.h"
#include "utils.h"
#include "ProcessEnvironment.h"

namespace {

inline
void sqz (
	unsigned & newlines,
	FILE * f
) {
	for (int c = std::fgetc(f); EOF != c; c = std::fgetc(f))
		if ('\n' != c) {
			std::cout.put(static_cast<char>(c));
			newlines = 0;
		} else
		{
			if (newlines < 2U)
				std::cout.put(static_cast<char>(c));
			++newlines;
		}
}

}

/* Main function ************************************************************
// **************************************************************************
*/

void
sqz [[gnu::noreturn]] (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, nullptr, "Main options", "[file(s)]");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	unsigned newlines(0U);
	if (args.empty()) {
		sqz(newlines, stdin);
		if (std::ferror(stdin)) die_errno(prog, envs, "<stdin>");
	} else
	{
		while (!args.empty()) {
			const char * file(args.front());
			args.erase(args.begin());
			FileStar f(std::fopen(file, "r"));
			if (!f) die_errno(prog, envs, file);
			sqz(newlines, f);
			if (std::ferror(f)) die_errno(prog, envs, file);
		}
	}
	std::cout.flush();

	throw EXIT_SUCCESS;
}
