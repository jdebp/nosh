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
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <kenv.h>
#endif
#include <unistd.h>
#include "FileStar.h"
#include "utils.h"
#include "ProcessEnvironment.h"
#include "jail.h"
#include "popt.h"

static inline
bool
is_dynamic_hostname_set ()
{
	std::vector<char> hostname;
	hostname.resize(sysconf(_SC_HOST_NAME_MAX) + 1);
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
get_static_hostname_env(const ProcessEnvironment & envs)
{
	if (const char * h = envs.query("hostname")) 
		return h;
	if (const char * h = envs.query("HOSTNAME")) 
		return h;
	return 0;
}

/* Main function ************************************************************
// **************************************************************************
*/

void
set_dynamic_hostname [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
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
		throw static_cast<int>(EXIT_USAGE);
	}

	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unexpected argument(s).");
		throw static_cast<int>(EXIT_USAGE);
	}

	if (am_in_jail(envs) && !set_dynamic_hostname_is_allowed()) {
		if (verbose)
			std::fprintf(stderr, "%s: INFO: %s\n", prog, "Cannot set the dynamic hostname in this jail.");
		throw EXIT_SUCCESS;
	}

	if (!force && is_dynamic_hostname_set()) {
		if (verbose)
			std::fprintf(stderr, "%s: INFO: %s\n", prog, "Dynamic hostname is already set; use --force to override.");
		throw EXIT_SUCCESS;
	}

	const char * h(get_static_hostname_env(envs));

#if defined(__FreeBSD__) || defined(__DragonFly__)
	if (!h) {
		char val[129];
		const int n(kenv(KENV_GET, "dhcp.host-name", val, sizeof val - 1));
		if (0 < n) {
			val[n] = '\0';
			envs.set("hostname", val);
			h = get_static_hostname_env(envs);
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
