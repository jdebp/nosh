/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <pwd.h>
#include "popt.h"
#include "utils.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
envuidgid ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "account prog");

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
		throw EXIT_FAILURE;
	}
	const char * account(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	passwd * p(getpwnam(account));
	if (!p) {
exit_error:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, account, std::strerror(error));
		throw 111;	// Bernstein daemontools compatibility
	}
	char uid[64], gid[64];
	snprintf(uid, sizeof uid, "%u", p->pw_uid);
	snprintf(gid, sizeof gid, "%u", p->pw_gid);

	if (0 >setenv("UID", uid, 1)) goto exit_error;
	if (0 >setenv("GID", gid, 1)) goto exit_error;
}
