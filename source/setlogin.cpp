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
#include <unistd.h>
#include <fcntl.h>
#if defined(__LINUX__) || defined(__linux__)
#include <pwd.h>
#include "FileStar.h"
#include "FileDescriptorOwner.h"
#endif
#include "popt.h"
#include "utils.h"
#include "fdutils.h"

#if defined(__LINUX__) || defined(__linux__)
static inline
int
linux_setlogin (
	const char * account
) {
	passwd * p(getpwnam(account));
	if (!p) return -1;
	uid_t uid(p->pw_uid);
	FileDescriptorOwner self_loginuid_fd(open_writetruncexisting_at(AT_FDCWD, "/proc/self/loginuid"));
	if (0 > self_loginuid_fd.get()) return -1;
	FileStar self_loginuid(fdopen(self_loginuid_fd.get(), "w"));
	if (self_loginuid) self_loginuid_fd.release();
	if (0 > std::fprintf(self_loginuid, "%u\n", uid)) return -1;
	return std::fflush(self_loginuid);
}
#endif

/* Main function ************************************************************
// **************************************************************************
*/

void
setlogin ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
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
#if defined(__LINUX__) || defined(__linux__)
	if (0 > linux_setlogin(account))
#else
	if (0 > setlogin(account))
#endif
	{
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, account, error ? std::strerror(error) : "No such account.");
		throw static_cast<int>(EXIT_TEMPORARY_FAILURE);
	}
}
