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
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/param.h>
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
	return s;
}

/* issue file processing ****************************************************
// **************************************************************************
*/

static inline
unsigned long
count_users(
	const char * utmp_filename
) {
	unsigned long count(0);
#if defined(_PATH_UTMP)
	if (FILE * const file = std::fopen(utmp_filename, "r")) {
		for (;;) {
			struct utmpx u;
			const size_t n(std::fread(&u, sizeof u, 1, file));
			if (n < 1) break;
			if (USER_PROCESS == u.ut_type) ++count;
		}
		std::fclose(file);
	}
#else
	if (utmp_filename)
		setutxdb(UTXDB_ACTIVE, utmp_filename);
	while (struct utmpx * u = getutxent()) {
		if (USER_PROCESS == u->ut_type) 
			++count;
	}
	endutxent();
#endif
	return count;
}

static inline
void
write_escape (
	const struct utsname & uts,
	const char * line,
	const std::time_t & now,
	const unsigned long users,
	int c
) {
	switch (c) {
		case '\\':
		default:	std::fputc('\\', stdout); std::fputc(c, stdout); break;
		case EOF:	std::fputc('\\', stdout); break;
		case 's':	std::fputs(uts.sysname, stdout); break;
		case 'n':	std::fputs(uts.nodename, stdout); break;
		case 'r':	std::fputs(uts.release, stdout); break;
		case 'v':	std::fputs(uts.version, stdout); break;
		case 'm':	std::fputs(uts.machine, stdout); break;
#if defined(_GNU_SOURCE)
		case 'o':	std::fputs(uts.domainname, stdout); break;
#endif
		case 'l':	if (line) std::fputs(line, stdout); break;
		case 'u':
		case 'U':
			std::printf("%lu", users);
			if ('U' == c)
				std::printf(" user%s", users != 1 ? "s" : "");
			break;
		case 'd':
		case 't':
		{
			const struct std::tm tm(*localtime(&now));
			char buf[32];
			std::strftime(buf, sizeof buf, 'd' == c ? "%F" : "%T", &tm);
			std::fputs(buf, stdout);
			break;
		}
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
login_banner ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
#if defined(_PATH_UTMP)
	const char * utmp_filename = _PATH_UTMP;
#else
	const char * utmp_filename = 0;
#endif

	const char * prog(basename_of(args[0]));
	try {
		popt::string_definition utmp_filename_option('\0', "utmp-filename", "filename", "Specify an alternative utmp filename.", utmp_filename);
		popt::definition * top_table[] = {
			&utmp_filename_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "template-file prog");

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

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing issue file name.");
		throw EXIT_FAILURE;
	}
	const char * issue_filename(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	const char * tty(get_controlling_tty_name());
	const char * line(tty ? strip_dev(tty) : 0);
	struct utsname uts;
	uname(&uts);
	const unsigned long users(count_users(utmp_filename));
	const std::time_t now(std::time(0));

	if (FILE * const file = std::fopen(issue_filename, "r")) {
		for (int c(std::fgetc(file)); EOF != c; c = std::fgetc(file)) {
			if ('\\' == c) {
				c = std::fgetc(file);
				write_escape(uts, line, now, users, c);
			} else
				std::fputc(c, stdout);
		}
		std::fclose(file);
		std::fflush(stdout);
	} else {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, issue_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
}
