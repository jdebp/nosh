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
move_to_control_group ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "{cgroup} {prog}");

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
	next_prog = arg0_of(args);

	FileStar self_cgroup(open_my_control_group_info("/proc/self/cgroup"));
	if (!self_cgroup) {
		const int error(errno);
		if (ENOENT == error) return;	// This is what we'll see on a BSD.
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "/proc/self/cgroup", std::strerror(error));
		throw EXIT_FAILURE;
	}

	std::string prefix("/sys/fs/cgroup"), current;
	if (!read_my_control_group(self_cgroup, "", current)) {
		if (!read_my_control_group(self_cgroup, "name=systemd", current)) 
			return;
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

	const FileDescriptorOwner cgroup_procs_fd(open_appendexisting_at(AT_FDCWD, (current + "/cgroup.procs").c_str()));
	if (0 > cgroup_procs_fd.get()) {
procs_file_error:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s%s: %s\n", prog, current.c_str(), "/cgroup.procs", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (0 > write(cgroup_procs_fd.get(), "0\n", 2)) goto procs_file_error;
}
