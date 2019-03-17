/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#include "ttyutils.h"

// These must have static storage duration as we are using them in args.
static std::string p_arg;
static std::list<std::string> o_args, p_args;

/* Main function ************************************************************
// **************************************************************************
*/

void
ps ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	std::vector<const char *> next_args;
	bool all_users(false), session_leaders(false), tree(false), wide(false), environment(false);
	bool jobs_format(false), long_format(false), user_format(false);
	popt::string_list_definition::list_type fields, processes;
	try {
		bool f(false);
		popt::bool_definition all_users_option('a', 0, "all users", all_users);
		popt::bool_definition session_leaders_option('x', 0, "session leaders", session_leaders);
		popt::bool_definition tree_option('d', "tree", "tree field", tree);
		popt::bool_definition environment_option('e', "environment", "environment field", environment);
		popt::bool_definition of_swapped_out_option('f', 0, "compatibility option, ignored", f);
		popt::bool_definition wide_option('w', "wide", "compatibility option, ignored", wide);
		popt::string_list_definition o_option('o', "output", "name(s)", "select field(s)", fields);
		popt::string_list_definition p_option('p', "process-id", "IDs", "select only these process(es)", processes);
		popt::bool_definition jobs_option('j', "jobs", "jobs format", jobs_format);
		popt::bool_definition long_option('l', "long", "long format", long_format);
		popt::bool_definition user_option('u', "user", "user format", user_format);
		popt::definition * top_table[] = {
			&all_users_option,
			&session_leaders_option,
			&p_option,
			&tree_option,
			&environment_option,
			&o_option,
			&jobs_option,
			&long_option,
			&user_option,
			&wide_option,
			&of_swapped_out_option,
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
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	if (all_users ^ session_leaders)
		std::fprintf(stderr, "%s: WARNING: %s\n", prog, "-a and -x imply each other.");
	const bool a(all_users || session_leaders);
	if (a == !processes.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Either -p or -a -x is required, and not both.");
		throw static_cast<int>(EXIT_USAGE);
	}

	o_args.clear();
	for (popt::string_list_definition::list_type::const_iterator p(fields.begin()), e(fields.end()); p != e; ++p) {
		const char * s(p->c_str());
		for (;;) {
			if (const char * comma = std::strchr(s, ',')) {
				o_args.push_back(std::string(s, comma));
				s = comma + 1;
			} else
			{
				o_args.push_back(std::string(s));
				break;
			}
		}
	}
	if (o_args.empty() || "command" == o_args.back() || "args" == o_args.back()) {
		if (tree) {
			// The inserted column goes before, rather than after, command/args.
			popt::string_list_definition::list_type::iterator e(o_args.end());
			if (!o_args.empty()) --e;
			o_args.insert(e, "tree");
		}
		if (environment) o_args.push_back("envs");
	}
	p_args.clear();
	for (popt::string_list_definition::list_type::const_iterator p(processes.begin()), e(processes.end()); p != e; ++p) {
		const char * s(p->c_str());
		for (;;) {
			if (const char * comma = std::strchr(s, ',')) {
				p_args.push_back(std::string(s, comma));
				s = comma + 1;
			} else
			{
				p_args.push_back(std::string(s));
				break;
			}
		}
	}

	args.insert(args.end(), "list-process-table");
	if (jobs_format) {
		args.insert(args.end(), "--format");
		args.insert(args.end(), "jobs");
	}
	if (long_format) {
		args.insert(args.end(), "--format");
		args.insert(args.end(), "long");
	}
	if (user_format) {
		args.insert(args.end(), "--format");
		args.insert(args.end(), "user");
	}
	for (std::list<std::string>::const_iterator p(o_args.begin()), e(o_args.end()); p != e; ++p) {
		args.insert(args.end(), "--output");
		args.insert(args.end(), p->c_str());
	}
	for (std::list<std::string>::const_iterator p(p_args.begin()), e(p_args.end()); p != e; ++p)
		args.insert(args.end(), p->c_str());
	next_prog = arg0_of(args);
}
