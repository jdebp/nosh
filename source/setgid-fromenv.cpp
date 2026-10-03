/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <grp.h>
#include "popt.h"
#include "utils.h"
#include "ProcessEnvironment.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
setgid_fromenv (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, nullptr, "Main options", "{prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	std::vector<gid_t> groups;
	gid_t gid(-1);
	if (const char * text = envs.query("GID")) {
		const char * old(text);
		gid = std::strtoul(text, const_cast<char **>(&text), 0);
		if (text == old || *text)
			die_invalid(prog, envs, old, "Not a number.");
	} else
		die_missing_environment_variable(prog, envs, "GID");
	if (const char * p = envs.query("GIDLIST")) {
		for (const char * e(nullptr); *p; p = e) {
			e = std::strchr(p, ',');
			if (!e) e = std::strchr(p, '\0');
			const std::string v(p, e);
			if (*e) ++e;
			const char * text(v.c_str()), * old(text);
			const gid_t g(std::strtoul(text, const_cast<char **>(&text), 0));
			if (text == old || *text)
				die_invalid(prog, envs, old, "Not a number.");
			groups.push_back(g);
		}
	}
	if (groups.empty())
		groups.push_back(gid);
	if (0 > setgroups(groups.size(), groups.data())) {
exit_error:
		message_fatal_errno(prog, envs, "setgroups");
		throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
	}
	if (0 > setgid(gid)) goto exit_error;
}
