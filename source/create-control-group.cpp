/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <vector>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "control_groups.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
create_control_group [[gnu::noreturn]] (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, nullptr, "Main options", "{cgroup}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) {
		die_missing_argument(prog, envs, "control group name");
	}
	const char * group(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	FileStar self_cgroup(open_my_control_group_info("/proc/self/cgroup"));
	if (!self_cgroup) {
		if (ENOENT == errno) 	// This is what we'll see on a BSD.
			throw EXIT_SUCCESS;
		die_errno(prog, envs, "/proc/self/cgroup");
	}

	std::string prefix("/sys/fs/cgroup"), current;
	if (!read_my_control_group(self_cgroup, "", current)) {
		if (!read_my_control_group(self_cgroup, "name=systemd", current))
			throw EXIT_SUCCESS;
		prefix += "/systemd";
	}

	if ('/' != *group) {
		current += "/";
		current += group;
	} else {
		current = group;
	}

	current = prefix + current;

	if (0 > mkdirat(AT_FDCWD, current.c_str(), 0755)) {
		if (EEXIST != errno) {
			die_errno(prog, envs, current.c_str());
		}
	}

	throw EXIT_SUCCESS;
}
