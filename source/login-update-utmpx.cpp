/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <vector>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cctype>
#include <stdint.h>
#include <unistd.h>
#include "config/hasutmpx.h"
#if defined(HAS_UTMPX)
#include <utmpx.h>
#include <sys/time.h>
#if defined(__LINUX__) || defined(__linux__)
#include <grp.h>
#endif
#endif
#include "utils.h"
#include "fdutils.h"
#include "FileDescriptorOwner.h"
#include "popt.h"

#if defined(HAS_UTMPX)
#if !defined(SHUTDOWN_TIME)
#define SHUTDOWN_TIME DOWN_TIME
#endif
#endif

/* Utilities ****************************************************************
// **************************************************************************
*/

namespace {

#if defined(HAS_UTMPX)
inline
void
gettimeofday (
	struct utmpx & u
) {
#if defined(__LINUX__) || defined(__linux__)
	// gettimeofday() doesn't work directly because the member structure isn't actually a timeval.
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	u.ut_tv.tv_sec = tv.tv_sec;
	u.ut_tv.tv_usec = tv.tv_usec;
#else
	gettimeofday(&u.ut_tv, nullptr);
#endif
}
#endif

}

/* Main function ************************************************************
// **************************************************************************
*/

void
login_update_utmpx [[gnu::noreturn]] (
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));

	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{boot|reboot|shutdown}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) die_missing_argument(prog, envs, "event");
        const char * command(args.front());
        args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

#if defined(HAS_UTMPX)
        struct utmpx u = {};
#endif

        if (0 == std::strcmp("boot", command) || 0 == std::strcmp("reboot", command)) {
#if defined(HAS_UTMPX)
                u.ut_type = BOOT_TIME;
#if defined(__LINUX__) || defined(__linux__)
		std::strncpy(u.ut_user, "reboot", sizeof u.ut_user);
		// Letting group "utmp" have write access is a bodge that allows terminal emulators to write to the login database.
		FileDescriptorOwner ufd(open_readwritecreate_at(AT_FDCWD, _PATH_UTMP, 0664));
		FileDescriptorOwner wfd(open_readwritecreate_at(AT_FDCWD, _PATH_WTMP, 0664));
		struct group * g(getgrnam("utmp"));
		if (g) {
			fchown(ufd.get(), -1, g->gr_gid);
			fchown(wfd.get(), -1, g->gr_gid);
		}
		endgrent();
#endif
#endif
        } else
        if (0 == std::strcmp("shutdown", command)) {
#if defined(HAS_UTMPX)
#if defined(__LINUX__) || defined(__linux__)
                u.ut_type = RUN_LVL;
		u.ut_pid = '0';
		std::strncpy(u.ut_user, "shutdown", sizeof u.ut_user);
#else
                u.ut_type = SHUTDOWN_TIME;
#endif
#endif
        } else
        {
                die_invalid_argument(prog, envs, command, "Unrecognized command");
        }

#if defined(HAS_UTMPX)
        gettimeofday(u);

	setutxent();
        const utmpx * rc(pututxline(&u));
        const int error(errno);
        endutxent();
#if defined(HAS_UPDWTMPX)
	updwtmpx(_PATH_WTMP, &u);
#endif
	errno = error;

        if (!rc) {
		die_errno(prog, envs, command);
        }
#endif

        throw EXIT_SUCCESS;
}
