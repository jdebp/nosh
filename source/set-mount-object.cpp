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
#include <sys/mount.h>
#include <unistd.h>
#include "utils.h"
#include "nmount.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
set_mount_object 
#if !defined(__LINUX__) && !defined(__linux__)
	[[gnu::noreturn]] 
#endif
( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool recursive(false);
	try {
		popt::bool_definition recursive_option('n', "recursive", "Apply the state recursively.", recursive);
		popt::definition * top_table[] = {
			&recursive_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{state} {dir} {prog}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing mount object state.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * state(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing mount object directory.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * dir(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	int flags(
#if defined(__LINUX__) || defined(__linux__)
		(recursive ? MS_REC : 0) |
#endif
		0
	);
#if defined(__LINUX__) || defined(__linux__)
	if (0 == std::strcmp("shared", state))
		flags |= MS_SHARED;
	else
	if (0 == std::strcmp("slave", state))
		flags |= MS_SLAVE;
	else
	if (0 == std::strcmp("private", state))
		flags |= MS_PRIVATE;
	else 
#endif
	{
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, state, "Bad state.");
		throw EXIT_FAILURE;
	}

	struct iovec iov[] = {
		FROM,			MAKE_IOVEC(""),
		FSPATH,			{ const_cast<char *>(dir), std::strlen(dir) },
		FSTYPE,			MAKE_IOVEC(""),
	};

	if (0 > nmount(iov, sizeof iov/sizeof *iov, flags)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, dir, std::strerror(error));
		throw EXIT_FAILURE;
	}
}
