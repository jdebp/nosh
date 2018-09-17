/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cctype>
#include "utils.h"
#include "ProcessEnvironment.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
read_conf (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool oknofile(false);
	try {
		popt::bool_definition oknofile_option('\0', "oknofile", "Do not fail if the file cannot be opened.", oknofile);
		popt::definition * top_table[] = {
			&oknofile_option
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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing file name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * file(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	FILE * f(std::fopen(file, "r"));
	if (!f) {
		const int error(errno);
		std::fprintf(stderr, "%s: %s: %s: %s\n", prog, oknofile ? "WARNING": "ERROR", file, std::strerror(error));
		if (oknofile) return;
		throw EXIT_FAILURE;
	}

	std::vector<std::string> env_strings(read_file(prog, file, f));
	for (std::vector<std::string>::const_iterator i(env_strings.begin()); i != env_strings.end(); ++i) {
		const std::string & s(*i);
		const std::string::size_type p(s.find('='));
		const std::string var(s.substr(0, p));
		const std::string val(p == std::string::npos ? std::string() : s.substr(p + 1, std::string::npos));
		if (!envs.set(var, val)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, var.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
	}
}
