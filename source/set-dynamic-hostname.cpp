/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#if !defined(_GNU_SOURCE)
#include <sys/syslimits.h>
#endif
#if !defined(__LINUX__) && !defined(__linux__)
#include <kenv.h>
#endif
#include <unistd.h>
#include "FileStar.h"
#include "utils.h"
#include "jail.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

static
const char * 
env_files[] = {
	"/etc/sysconfig/network",	// CentOS/Fedora (HOSTNAME=...)
	"/etc/conf.d/hostname",		// Gentoo (HOSTNAME=...)
	"/etc/rc.conf",			// BSDs (hostname=...)
};

static
const char * 
line_files[] = {
	"/etc/hostname",	// recommended, and systemd
	"/etc/HOSTNAME",	// 
};

static inline
bool
is_dynamic_hostname_set ()
{
	std::vector<char> hostname(sysconf(_SC_HOST_NAME_MAX) + 1);
	const int r(gethostname(hostname.data(), hostname.size()));
	if (0 > r) return false;
#if defined(__LINUX__) || defined(__linux__)
	// The kernel's initial default counts as not set.
	if ('(' == hostname[0]
	&&  'n' == hostname[1]
	&&  'o' == hostname[2]
	&&  'n' == hostname[3]
	&&  'e' == hostname[4]
	&&  ')' == hostname[5]
	&& '\0' == hostname[6]
	)
		return false;
#endif
	return hostname[0];
}

static inline
const char * 
get_static_hostname_env()
{
	if (const char * h = std::getenv("hostname")) 
		return h;
	if (const char * h = std::getenv("HOSTNAME")) 
		return h;
	return 0;
}

static inline
std::string
read_first_line (
	FILE * f
) {
	std::string a;
	for (;;) {
		const int c(std::fgetc(f));
		if (std::feof(f)) break;
		if ('\n' == c) break;
		a += char(c);
	}
	return a;
}

void
set_dynamic_hostname ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	bool force(false), verbose(false);
	try {
		popt::bool_definition force_option('f', "force", "Force setting the dynamic hostname even if it is already set.", force);
		popt::bool_definition verbose_option('v', "verbose", "Print messages.", verbose);
		popt::definition * top_table[] = {
			&force_option,
			&verbose_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "");

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

	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unexpected argument(s).");
		throw static_cast<int>(EXIT_USAGE);
	}

	if (am_in_jail() && !set_dynamic_hostname_is_allowed()) {
		if (verbose)
			std::fprintf(stderr, "%s: INFO: %s\n", prog, "Cannot set the dynamic hostname in this jail.");
		throw EXIT_SUCCESS;
	}

	if (!force && is_dynamic_hostname_set()) {
		if (verbose)
			std::fprintf(stderr, "%s: INFO: %s\n", prog, "Dynamic hostname is already set; use --force to override.");
		throw EXIT_SUCCESS;
	}

	unsetenv("HOSTNAME");
	unsetenv("hostname");

	const char * h(get_static_hostname_env());

	if (!h) {
		for (std::size_t fi(0); fi < sizeof line_files/sizeof *line_files; ++fi) {
			const char * filename(line_files[fi]);
			const char * var(basename_of(filename));
			FileStar f(std::fopen(filename, "r"));
			if (!f) {
				const int error(errno);
				if (ENOENT != error)
					std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, filename, std::strerror(error));
				continue;
			}
			try {
				std::string val(read_first_line(f));
				if (val.length())
					setenv(var, val.c_str(), 1);
				h = get_static_hostname_env();
			} catch (const char * r) {
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, filename, r);
			}
		}
	}
	if (!h) {
		for (std::size_t fi(0); fi < sizeof env_files/sizeof *env_files; ++fi) {
			const char * filename(env_files[fi]);
			FileStar f(std::fopen(filename, "r"));
			if (!f) {
				const int error(errno);
				if (ENOENT != error)
					std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, filename, std::strerror(error));
				continue;
			}
			try {
				std::vector<std::string> env_strings(read_file(f));
				for (std::vector<std::string>::const_iterator i(env_strings.begin()); i != env_strings.end(); ++i) {
					const std::string & s(*i);
					const std::string::size_type p(s.find('='));
					const std::string var(s.substr(0, p));
					const std::string val(p == std::string::npos ? std::string() : s.substr(p + 1, std::string::npos));
					setenv(var.c_str(), val.c_str(), 1);
				}
				h = get_static_hostname_env();
			} catch (const char * r) {
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, filename, r);
			}
		}
	}
#if !defined(__LINUX__) && !defined(__linux__)
	if (!h) {
		char val[129];
		const int n(kenv(KENV_GET, "dhcp.host-name", val, sizeof val - 1));
		if (0 < n) {
			val[n] = '\0';
			setenv("hostname", val, 1);
			h = get_static_hostname_env();
		}
	}
#endif
	if (!h) 
		h = "localhost";

	if (0 > sethostname(h, std::strlen(h))) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "sethostname", std::strerror(error));
		throw EXIT_FAILURE;
	}

	if (verbose)
		std::fprintf(stderr, "%s: INFO: %s %s\n", prog, "Dynamic hostname is", h);
	throw EXIT_SUCCESS;
}
