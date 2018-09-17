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
#if defined(__LINUX__) || defined(__linux__)
#include <sys/time.h>
#include <utmpx.h>
#endif
#include <paths.h>
#include "popt.h"
#include "ttyname.h"
#include "utils.h"
#include "ProcessEnvironment.h"

/* Filename manipulation ****************************************************
// **************************************************************************
*/

static inline
const char *
strip_dev (
	const char * s
) {
	if (0 == strncmp(s, "/dev/", 5))
		return s + 5;
	if (0 == strncmp(s, "/run/dev/", 9))
		return s + 9;
	return s;
}

#if defined(__LINUX__) || defined(__linux__)
static inline
std::string
id_from (
	const char * s
) {
	if (0 == strncmp(s, "tty", 3)
	||  0 == strncmp(s, "tts", 3) 
	||  0 == strncmp(s, "pty", 3) 
	||  0 == strncmp(s, "pts", 3)) {
		std::string id(s);
		const std::string::size_type l(id.length());
		if (l > 4 && "/tty" == id.substr(l - 4))
			id = id.substr(0, l-4);
		return id;
	}
	// This is a nosh convention for user-space virtual terminals.
	// In theory, this is the one convention that would also be used on FreeBSD.
	if (0 == strncmp(s, "vc", 2)) {
		std::string id(s);
		const std::string::size_type l(id.length());
		if (l > 4 && "/tty" == id.substr(l - 4))
			id = id.substr(0, l-4);
		return id;
	}
	while (*s && !std::isdigit(*s)) ++s;
	return s;
}
#endif

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
	const char * override_id(0);
	try {
		popt::string_definition id_option('i', "id", "string", "Override the TTY name and use this ID.", override_id);
		popt::definition * top_table[] = {
			&id_option
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

	const char * tty(get_controlling_tty_name(envs));
	if (!tty) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "stdin", std::strerror(error));
		throw EXIT_FAILURE;
	}
	const char * line(strip_dev(tty));

#if defined(__LINUX__) || defined(__linux__)
	const std::string id(override_id ? override_id : id_from(line));

	struct utmpx u;
	std::memset(&u, '\0', sizeof u);
	u.ut_type = LOGIN_PROCESS;
	std::strncpy(u.ut_id, id.c_str(), sizeof u.ut_id);
	u.ut_pid = getpid();
	u.ut_session = getsid(0);
	std::strncpy(u.ut_user, "LOGIN", sizeof u.ut_user);
	std::strncpy(u.ut_line, line, sizeof u.ut_line);
	if (const char * host = envs.query("HOST"))
		std::strncpy(u.ut_host, host, sizeof u.ut_host);
	// gettimeofday() doesn't work directly because the member structure isn't actually a timeval.
	struct timeval tv;
	gettimeofday(&tv, 0);
	u.ut_tv.tv_sec = tv.tv_sec;
	u.ut_tv.tv_usec = tv.tv_usec;

	setutxent();
	pututxline(&u);
	endutxent();
#elif defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__)
	static_cast<void>(line);	// Silence a compiler warning.
#else
#error "Don't know how to record a login process on your platform."
#endif
}
