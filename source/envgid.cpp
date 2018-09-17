/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if defined(__LINUX__) || defined(__linux__)
#define _BSD_SOURCE
#endif
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <unistd.h>
#include <grp.h>
#include "popt.h"
#include "utils.h"
#include "ProcessEnvironment.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
envgid ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{account} {prog}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing account name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * groupname(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	errno = 0;
	group * p(getgrnam(groupname));
	if (!p) {
exit_error:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, groupname, error ? std::strerror(error) : "No such user.");
		throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
	}
	gid_t gid(p->gr_gid);
	if (!envs.unset("GIDLIST")) goto exit_error;
	char gid_value[64];
	snprintf(gid_value, sizeof gid_value, "%u", gid);
	if (!envs.set("GID", gid_value)) goto exit_error;
}
