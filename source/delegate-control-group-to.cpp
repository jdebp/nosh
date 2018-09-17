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
#include "FileStar.h"
#include "control_groups.h"

/* Main function ************************************************************
// **************************************************************************
*/

// These must have static storage duration as we are using them in args.
static std::string dir_arg_buffer, procs_arg_buffer, subtree_control_arg_buffer;

void
delegate_control_group_to ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "{account}");

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
	const char * owner(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	FileStar self_cgroup(open_my_control_group_info("/proc/self/cgroup"));
	if (!self_cgroup) {
		const int error(errno);
		if (ENOENT == error) throw EXIT_SUCCESS;	// This is what we'll see on a BSD.
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "/proc/self/cgroup", std::strerror(error));
		throw EXIT_FAILURE;
	}

	std::string prefix("/sys/fs/cgroup"), current;
	if (!read_my_control_group(self_cgroup, "", current)) {
		if (!read_my_control_group(self_cgroup, "name=systemd", current)) 
			return;
		prefix += "/systemd";
	}
	current = prefix + current;
	dir_arg_buffer = current + "/";
	procs_arg_buffer = current + "/cgroup.procs";
	subtree_control_arg_buffer = current + "/cgroup.subtree_control";

	args.clear();
	args.insert(args.end(), "chown");
	args.insert(args.end(), "--");
	args.insert(args.end(), owner);
	args.insert(args.end(), dir_arg_buffer.c_str());
	args.insert(args.end(), procs_arg_buffer.c_str());
	args.insert(args.end(), subtree_control_arg_buffer.c_str());
	args.insert(args.end(), 0);
	next_prog = arg0_of(args);
}
