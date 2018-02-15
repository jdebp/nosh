/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <unistd.h>
#include "utils.h"
#include "nmount.h"
#include "common-manager.h"	// Because we make API mounts too.
#include "fdutils.h"
#include "popt.h"
#include "FileDescriptorOwner.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
make_read_only_fs ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool os(false), etc(false), homes(false), sysctl(false), cgroups(false);
	std::list<std::string> include_directories;
	std::list<std::string> except_directories;
	try {
		popt::bool_definition os_option('o', "os", "Make a read-only /usr.", os);
		popt::bool_definition etc_option('e', "etc", "Make a read-only /etc.", etc);
		popt::bool_definition homes_option('d', "homes", "Make a read-only /home.", homes);
		popt::bool_definition sysctl_option('s', "sysctl", "Make a read-only /sys, /proc/sys, et al..", sysctl);
		popt::bool_definition cgroups_option('c', "cgroups", "Make a read-only /sys/fs/cgroup.", cgroups);
		popt::string_list_definition include_option('i', "include", "directory", "Make this directory read-only.", include_directories);
		popt::string_list_definition except_option('x', "except", "directory", "Retain this directory as read-write.", except_directories);
		popt::definition * top_table[] = {
			&os_option,
			&etc_option,
			&homes_option,
			&sysctl_option,
			&cgroups_option,
			&include_option,
			&except_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "prog");

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

	if (os) {
#if defined(__LINUX__) || defined(__linux__)
#endif
	}
	if (etc) {
#if defined(__LINUX__) || defined(__linux__)
#else
#endif
	}
	if (homes) {
#if defined(__LINUX__) || defined(__linux__)
#endif
	}
	if (sysctl) {
#if defined(__LINUX__) || defined(__linux__)
#endif
	}
	if (cgroups) {
#if defined(__LINUX__) || defined(__linux__)
#endif
	}
	for (std::list<std::string>::const_iterator e(include_directories.end()), i(include_directories.begin()); e != i; ++i) {
#if defined(__LINUX__) || defined(__linux__)
#endif
	}
	for (std::list<std::string>::const_iterator e(except_directories.end()), i(except_directories.begin()); e != i; ++i) {
#if defined(__LINUX__) || defined(__linux__)
#endif
	}
}
