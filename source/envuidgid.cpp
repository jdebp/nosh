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
	const char * primary_group(0);
	try {
		popt::bool_definition supplementary_option('s', "supplementary", "Set the supplementary GIDs from the group database as well.", supplementary);
		popt::string_definition primary_group_option('g', "primary-group", "groupname", "Override the primary GID with this.", primary_group);
		popt::definition * top_table[] = {
			&supplementary_option,
			&primary_group_option
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
	gid_t gid(p->pw_gid);
	if (primary_group) {
		errno = 0;
		group *g(getgrnam(primary_group));
		if (!g) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, primary_group, error ? std::strerror(error) : "No such group.");
			throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
		}
		gid = g->gr_gid;
	}
	if (supplementary) {
		int n(0);	// Zero initialization is important!
		getgrouplist(account, gid, 0, &n);
		std::vector<gid_t> groups(n);
		n = groups.size();
		if (0 > getgrouplist(account, gid, groups.data(), &n)) goto exit_error;
		std::string gidlist;
		for (std::vector<gid_t>::const_iterator i(groups.begin()), e(groups.end()); e != i; ++i) {
			char gid_value[64];
			snprintf(gid_value, sizeof gid_value, "%u", *i);
			if (!gidlist.empty()) gidlist += ",";
			gidlist += gid_value;
		}
		if (!envs.set("GIDLIST", gidlist)) goto exit_error;
	} else {
		if (!envs.unset("GIDLIST")) goto exit_error;
	}
	char uid_value[64], gid_value[64];
	snprintf(uid_value, sizeof uid_value, "%u", p->pw_uid);
	snprintf(gid_value, sizeof gid_value, "%u", gid);
	if (!envs.set("UID", uid_value)) goto exit_error;
	if (!envs.set("GID", gid_value)) goto exit_error;
}
