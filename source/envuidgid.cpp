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
#if defined(__LINUX__) || defined(__linux__)
#include <grp.h>
#else
#include <unistd.h>
#endif
#include <pwd.h>
#include "popt.h"
#include "utils.h"
#include "ProcessEnvironment.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
envuidgid ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
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
		int n(0);	// Zero initialization is important!
		getgrouplist(account, p->pw_gid, 0, &n);
		std::vector<gid_t> groups(n);
		n = groups.size();
		if (0 > getgrouplist(account, p->pw_gid, groups.data(), &n)) goto exit_error;
		std::string gidlist;
		for (std::vector<gid_t>::const_iterator i(groups.begin()); groups.end() != i; ++i) {
			char gid[64];
			snprintf(gid, sizeof gid, "%u", *i);
			if (!gidlist.empty()) gidlist += ",";
			gidlist += gid;
		}
		if (!envs.set("GIDLIST", gidlist)) goto exit_error;
	} else {
		if (0 > envs.unset("GIDLIST")) goto exit_error;
	}
	char uid[64], gid[64];
	snprintf(uid, sizeof uid, "%u", p->pw_uid);
	snprintf(gid, sizeof gid, "%u", p->pw_gid);
	if (!envs.set("UID", uid)) goto exit_error;
	if (!envs.set("GID", gid)) goto exit_error;
}
