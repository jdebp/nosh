/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#include "ProcessEnvironment.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
getuidgid ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
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

	const uid_t uid(geteuid());
	const gid_t gid(getegid());
	std::ostringstream us, gs;
	us << uid;
	gs << gid;
	if (false) {
exit_error:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
		throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
	}
	if (!envs.set("UID", us.str())) goto exit_error;
	if (!envs.set("GID", gs.str())) goto exit_error;
	int n(getgroups(0, 0));
	if (0 > n) goto exit_error;
	std::vector<gid_t> groups(n);
	n = getgroups(groups.size(), groups.data());
	if (0 > n) goto exit_error;
	std::string gidlist;
	for (std::vector<gid_t>::const_iterator i(groups.begin()), e(groups.end()); e != i; ++i) {
		char gid_value[64];
		snprintf(gid_value, sizeof gid_value, "%u", *i);
		if (!gidlist.empty()) gidlist += ",";
		gidlist += gid_value;
	}
	if (!envs.set("GIDLIST", gidlist)) goto exit_error;
}
