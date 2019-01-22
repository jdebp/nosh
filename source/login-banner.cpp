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
#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)
#include <utmpx.h>
#elif defined(__OpenBSD__)
#include <utmp.h>
#else
#error "Don't know how to count logged in users on your platform."
#endif
#include <paths.h>
#include "popt.h"
#include "ttyname.h"
#include "FileStar.h"
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
	if (0 == strncmp(s, "/var/dev/", 9))
		return s + 9;
	return s;
}

/* issue file processing ****************************************************
// **************************************************************************
*/

static inline
unsigned long
count_users() 
{
	unsigned long count(0);
#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)
	setutxent();
	while (struct utmpx * u = getutxent()) {
		if (USER_PROCESS == u->ut_type) 
			++count;
	}
	endutxent();
#elif defined(__OpenBSD__)
	if (FILE * const file = std::fopen(_PATH_UTMP, "r")) {
		for (;;) {
			struct utmp u;
			const size_t n(std::fread(&u, sizeof u, 1, file));
			if (n < 1) break;
			if (u.ut_name[0]) ++count;
		}
		std::fclose(file);
	}
#else
#error "Don't know how to count logged in users on your platform."
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
	const std::string & pretty_sysname,
	const std::string & pretty_nodename,
	const char * domainname,
	int c
) {
	switch (c) {
		case '\\':
		default:	std::fputc('\\', stdout); std::fputc(c, stdout); break;
		case EOF:	std::fputc('\\', stdout); break;
		case 'S':	std::fputs(pretty_sysname.c_str(), stdout); break;
		case 's':	std::fputs(uts.sysname, stdout); break;
		case 'N':	std::fputs(pretty_nodename.c_str(), stdout); break;
		case 'n':	std::fputs(uts.nodename, stdout); break;
		case 'r':	std::fputs(uts.release, stdout); break;
		case 'v':	std::fputs(uts.version, stdout); break;
		case 'm':	std::fputs(uts.machine, stdout); break;
		case 'o':	if (domainname) std::fputs(domainname, stdout); break;
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

static const char etc_release_filename[] = "/etc/os-release";
static const char lib_release_filename[] = "/usr/lib/os-release";
static const char etc_machineinfo_filename[] = "/etc/machine-info";
static const char run_machineinfo_filename[] = "/run/machine-info";

void
login_banner ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = { };
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{template-file} {prog}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing issue file name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * issue_filename(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	const char * tty(get_controlling_tty_name(envs));
	const char * line(tty ? strip_dev(tty) : 0);
	struct utsname uts;
	uname(&uts);
	const unsigned long users(count_users());
	const std::time_t now(std::time(0));
	const char * domainname(envs.query("LOCALDOMAIN"));	// Convention in both the BIND and djbdns DNS clients.
#if !defined(_GNU_SOURCE)
	char domainname_buf[MAXHOSTNAMELEN];
#endif
	if (!domainname) {
#if defined(_GNU_SOURCE)
		domainname = uts.domainname;
#else
		if (0 <= getdomainname(domainname_buf, sizeof domainname_buf))
			domainname = domainname_buf;
#endif
	}
	std::string pretty_sysname(uts.sysname);
	std::string pretty_nodename(uts.nodename);

	const char * release_filename("");
	FileStar os_release(std::fopen(release_filename = etc_release_filename, "r"));
	if (!os_release) {
		if (ENOENT == errno)
			os_release = std::fopen(release_filename = lib_release_filename, "r");
	}
	if (!os_release) {
		const int error(errno);
		if (ENOENT != error)
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, release_filename, std::strerror(error));
	}
	if (os_release) {
		try {
			std::vector<std::string> env_strings(read_file(os_release));
			for (std::vector<std::string>::const_iterator i(env_strings.begin()); i != env_strings.end(); ++i) {
				const std::string & s(*i);
				const std::string::size_type p(s.find('='));
				const std::string var(s.substr(0, p));
				const std::string val(p == std::string::npos ? std::string() : s.substr(p + 1, std::string::npos));
				if ("PRETTY_NAME" == var)
					pretty_sysname = val;
			}
		} catch (const char * r) {
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, release_filename, r);
		}
		os_release = 0;
	}

	const char * machineinfo_filename("");
	FileStar machine_info(std::fopen(machineinfo_filename = etc_machineinfo_filename, "r"));
	if (!machine_info) {
		if (ENOENT == errno)
			machine_info = std::fopen(machineinfo_filename = run_machineinfo_filename, "r");
	}
	if (!machine_info) {
		const int error(errno);
		if (ENOENT != error)
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, machineinfo_filename, std::strerror(error));
	}
	if (machine_info) {
		try {
			std::vector<std::string> env_strings(read_file(machine_info));
			for (std::vector<std::string>::const_iterator i(env_strings.begin()); i != env_strings.end(); ++i) {
				const std::string & s(*i);
				const std::string::size_type p(s.find('='));
				const std::string var(s.substr(0, p));
				const std::string val(p == std::string::npos ? std::string() : s.substr(p + 1, std::string::npos));
				if ("PRETTY_HOSTNAME" == var)
					pretty_nodename = val;
			}
		} catch (const char * r) {
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, machineinfo_filename, r);
		}
		machine_info = 0;
	}

	FileStar file(std::fopen(issue_filename, "r"));
	if (!file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, issue_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
	for (int c(std::fgetc(file)); EOF != c; c = std::fgetc(file)) {
		if ('\\' == c) {
			c = std::fgetc(file);
			write_escape(uts, line, now, users, pretty_sysname, pretty_nodename, domainname, c);
		} else
			std::fputc(c, stdout);
	}
	std::fflush(stdout);
}
