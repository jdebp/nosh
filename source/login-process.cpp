/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <cctype>
#include <unistd.h>
#include "config/hasutmpx.h"
#if defined(HAS_UTMPX)
#include <utmpx.h>
#include <sys/time.h>
#endif
#include "config/hasupdwtmpx.h"
#include "popt.h"
#include "ttyname.h"
#include "utils.h"
#include "ProcessEnvironment.h"

/* Utilities ****************************************************************
// **************************************************************************
*/

namespace {

#if defined(HAS_UTMPX)
// We are only even using utmpx at all on these systems, when they support it.
#if defined(__LINUX__) || defined(__linux__) || defined(__NetBSD__)
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
#endif

}

/* Main function ************************************************************
// **************************************************************************
*/

void
login_process (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	const char * override_id(nullptr);
	try {
		popt::string_definition id_option('i', "id", "string", "Override the TTY name and use this ID.", override_id);
		popt::definition * top_table[] = {
			&id_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	const char * line(get_line_name(envs));
	if (!line) {
		die_errno(prog, envs, "stdin");
	}

#if defined(HAS_UTMPX)

	// The LOGIN_PROCESS record type is only meaningful on a subset of the systems that have utmpx.
#if defined(__LINUX__) || defined(__linux__) || defined(__NetBSD__)
	// This is the old way of doing IDs, where multiple processes are involved.
	// The new way involves the same process writing the live and dead records, so it just remembers some random string of bytes.
	const std::string id(override_id ? override_id : id_field_from(line));

	struct utmpx u = {};
	u.ut_type = LOGIN_PROCESS;
	std::strncpy(u.ut_id, id.c_str(), sizeof u.ut_id);
	u.ut_pid = getpid();
	u.ut_session = getsid(0);
	std::strncpy(u.ut_user, "LOGIN", sizeof u.ut_user);
	std::strncpy(u.ut_line, line, sizeof u.ut_line);
	if (const char * host = envs.query("HOST"))
		std::strncpy(u.ut_host, host, sizeof u.ut_host);
	gettimeofday(u);

	setutxent();
	pututxline(&u);
#if defined(HAS_UPDWTMPX)
	updwtmpx(_PATH_WTMP, &u);
#endif
	endutxent();
#else
	static_cast<void>(line);	// Silence a compiler warning.
#endif

#else
	static_cast<void>(line);	// Silence a compiler warning.
#endif
}
