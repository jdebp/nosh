/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include "utils.h"
#include "popt.h"

#if !defined(__LINUX__) && !defined(__linux__)
static inline int unshare ( int ) { return 0 ; }
#endif

/* Main function ************************************************************
// **************************************************************************
*/

void
unshare ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool network(false), mount(false), ipc(false), uts(false), process(false), user(false);
	try {
		popt::bool_definition network_option('n', "network", "Unshare the network namespace.", network);
		popt::bool_definition uts_option('u', "uts", "Unshare the UTS namespace.", uts);
		popt::bool_definition mount_option('m', "mount", "Unshare the mount namespace.", mount);
		popt::bool_definition ipc_option('i', "ipc", "Unshare the IPC namespace.", ipc);
		popt::bool_definition process_option('p', "process", "Unshare the process ID namespace.", process);
		popt::bool_definition user_option('u', "user", "Unshare the user ID namespace.", user);
		popt::definition * top_table[] = {
			&network_option,
			&mount_option,
			&ipc_option,
			&uts_option,
			&process_option,
			&user_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog}");

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

	const int flags (
#if defined(__LINUX__) || defined(__linux__)
		(mount ? CLONE_NEWNS : 0) |
		(network ? CLONE_NEWNET : 0) |
		(ipc ? CLONE_NEWIPC : 0) |
		(uts ? CLONE_NEWUTS : 0) |
		(process ? CLONE_NEWPID : 0) |
		(user ? CLONE_NEWUSER : 0) |
#endif
		0
	);
	const pid_t rc(unshare(flags));
	if (-1 == rc) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
		throw EXIT_FAILURE;
	}
}
