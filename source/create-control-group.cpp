/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

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
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "{cgroup}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing control group name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * group(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	FileStar self_cgroup(open_my_control_group_info("/proc/self/cgroup"));
	if (!self_cgroup) {
		const int error(errno);
		if (ENOENT == error) 	// This is what we'll see on a BSD.
			throw EXIT_SUCCESS;
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "/proc/self/cgroup", std::strerror(error));
		throw EXIT_FAILURE;
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
		const int error(errno);
		if (EEXIST != error) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, current.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	throw EXIT_SUCCESS;
}
