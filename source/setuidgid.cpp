/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include "popt.h"
#include "utils.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
setuidgid ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool supplementary(false);
	try {
		popt::bool_definition supplementary_option('s', "supplementary", "Set the supplementary GIDs from the group database as well.", supplementary);
		popt::definition * top_table[] = {
			&supplementary_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "account prog");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing account name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * account(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	errno = 0;
	passwd * p(getpwnam(account));
	if (!p) {
exit_error:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, account, error ? std::strerror(error) : "No such user.");
		throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
	}
	if (supplementary) {
		if (0 > initgroups(account, p->pw_gid)) goto exit_error;
	} else {
		gid_t groups[1] = { p->pw_gid };
		if (0 > setgroups(sizeof groups/sizeof *groups, groups)) goto exit_error;
	}
	if (0 > setgid(p->pw_gid)) goto exit_error;
	if (0 > setuid(p->pw_uid)) goto exit_error;
}
