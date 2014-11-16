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
#include <utmpx.h>
#include <paths.h>
#include "popt.h"
#include "ttyname.h"
#include "utils.h"

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

static inline
std::string
id_from (
	const char * s
) {
#if defined(__LINUX__) || defined(__linux__)
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
	if (0 == strncmp(s, "vc", 2)) {
		std::string id(s);
		const std::string::size_type l(id.length());
		if (l > 4 && "/tty" == id.substr(l - 4))
			id = id.substr(0, l-4);
		return id;
	}
#endif
	while (*s && !std::isdigit(*s)) ++s;
	return s;
}

/* Main function ************************************************************
// **************************************************************************
*/

void
login_process ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
#if defined(_PATH_UTMP) && defined(_PATH_WTMP)
	const char * utmp_filename = _PATH_UTMP;	// guaranteed never NULL
	const char * wtmp_filename = _PATH_WTMP;	// guaranteed never NULL
#endif
	bool is_getty(false);

	const char * prog(basename_of(args[0]));
	const char * override_id(0);
	try {
#if defined(_PATH_UTMP) && defined(_PATH_WTMP)
		popt::string_definition utmp_filename_option('\0', "utmp-filename", "filename", "Specify an alternative utmp filename.", utmp_filename);
		popt::string_definition wtmp_filename_option('\0', "wtmp-filename", "filename", "Specify an alternative wtmp filename.", wtmp_filename);
#endif
		popt::string_definition id_option('i', "id", "string", "Override the TTY name and use this ID.", override_id);
		popt::bool_definition getty_option('\0', "getty", "Specify an INIT process rather than a LOGIN process.", is_getty);
		popt::definition * top_table[] = {
#if defined(_PATH_UTMP) && defined(_PATH_WTMP)
			&utmp_filename_option,
			&wtmp_filename_option,
#endif
			&id_option,
			&getty_option
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

	const char * tty(get_controlling_tty_name());
	if (!tty) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "stdin", std::strerror(error));
		throw EXIT_FAILURE;
	}
	const char * host(std::getenv("HOST"));
	const char * line(strip_dev(tty));
	const std::string id(override_id ? override_id : id_from(line));

	struct utmpx u;

#if defined(_PATH_UTMP) && defined(_PATH_WTMP)
	if (FILE * const file = std::fopen(utmp_filename, "r+")) {
		std::fpos_t start;
		const pid_t my_pid(getpid());
		for (;;) {
			std::fgetpos(file, &start);
			const size_t n(std::fread(&u, sizeof u, 1, file));
			if (n < 1) break;
			if (my_pid == u.ut_pid || 0 == std::strncmp(u.ut_line, line, sizeof u.ut_line)) break;
		}
		if (my_pid != u.ut_pid) {
			std::memset(&u, '\0', sizeof u);
			u.ut_pid = my_pid;
			std::strncpy(u.ut_id, id.c_str(), sizeof u.ut_id);
		}
		std::strncpy(u.ut_user, is_getty ? "GETTY" : "LOGIN", sizeof u.ut_user);
		std::strncpy(u.ut_line, line, sizeof u.ut_line);
		if (host)
			std::strncpy(u.ut_host, host, sizeof u.ut_host);
		u.ut_tv.tv_sec = std::time(0);
		u.ut_tv.tv_usec = 0;
		u.ut_type = is_getty ? INIT_PROCESS : LOGIN_PROCESS;
		std::fsetpos(file, &start);
		std::fwrite(&u, sizeof u, 1, file);
		std::fclose(file);
	} else {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, utmp_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (FILE * const file = std::fopen(wtmp_filename, "a")) {
		std::fwrite(&u, sizeof u, 1, file);
		std::fclose(file);
	} else {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, wtmp_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
#else
	std::memset(&u, '\0', sizeof u);
	u.ut_pid = getpid();
	std::strncpy(u.ut_id, id.c_str(), sizeof u.ut_id);
	std::strncpy(u.ut_user, is_getty ? "GETTY" : "LOGIN", sizeof u.ut_user);
	std::strncpy(u.ut_line, line, sizeof u.ut_line);
	if (host)
		std::strncpy(u.ut_host, host, sizeof u.ut_host);
	u.ut_tv.tv_sec = std::time(0);
	u.ut_tv.tv_usec = 0;
	u.ut_type = is_getty ? INIT_PROCESS : LOGIN_PROCESS;

	setutxent();
	pututxline(&u);
	endutxent();
#endif
}
