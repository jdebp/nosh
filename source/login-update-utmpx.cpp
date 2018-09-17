/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _XOPEN_SOURCE_EXTENDED
#include <vector>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cctype>
#include <stdint.h>
#include <unistd.h>
#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__)
#include <sys/time.h>
#include <utmpx.h>
#endif
#include "utils.h"
#include "fdutils.h"
#include "FileDescriptorOwner.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
login_update_utmpx [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));

	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{boot|reboot|shutdown}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing command.");
		throw static_cast<int>(EXIT_USAGE);
	}
        const char * command(args.front());
        args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__)
        struct utmpx u = { 0 };
#endif

        if (0 == std::strcmp("boot", command) || 0 == std::strcmp("reboot", command)) {
#if defined(__LINUX__) || defined(__linux__)
                u.ut_type = BOOT_TIME;
		std::strncpy(u.ut_user, "reboot", sizeof u.ut_user);
		FileDescriptorOwner ufd(open_readwritecreate_at(AT_FDCWD, _PATH_UTMP, 0644));
		FileDescriptorOwner wfd(open_readwritecreate_at(AT_FDCWD, _PATH_WTMP, 0644));
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__)
                u.ut_type = BOOT_TIME;
#endif
        } else
        if (0 == std::strcmp("shutdown", command)) {
#if defined(__LINUX__) || defined(__linux__)
                u.ut_type = RUN_LVL;
		u.ut_pid = '0';
		std::strncpy(u.ut_user, "shutdown", sizeof u.ut_user);
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__)
                u.ut_type = SHUTDOWN_TIME;
#endif
        } else
        {
                std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, command, "Unrecognized command");
                throw EXIT_FAILURE;
        }

#if defined(__LINUX__) || defined(__linux__)
	// gettimeofday() doesn't work directly because the member structure isn't actually a timeval.
	struct timeval tv;
	gettimeofday(&tv, 0);
	u.ut_tv.tv_sec = tv.tv_sec;
	u.ut_tv.tv_usec = tv.tv_usec;
#elif defined(__NetBSD__)
        gettimeofday(&u.ut_tv, 0);
#endif

#if defined(__LINUX__) || defined(__linux__) || defined(__NetBSD__)
        setutxent();
        const utmpx * rc(pututxline(&u));
        const int error(errno);
        endutxent();
	updwtmpx(_PATH_WTMP, &u);
#elif defined(__FreeBSD__) || defined(__DragonFly__)
        const utmpx * rc(pututxline(&u));
        const int error(errno);
#elif defined(__OpenBSD__)
        const bool rc(true);
        const int error(0);
#else
#error "Don't know how to update the terminal login database on your platform."
#endif

        if (!rc) {
                std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, command, std::strerror(error));
                throw EXIT_FAILURE;
        }

        throw EXIT_SUCCESS;
}
