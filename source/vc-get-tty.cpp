/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#if !defined(_GNU_SOURCE)
#include <sys/syslimits.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>
#include <grp.h>
#include "popt.h"
#include "utils.h"
#include "ProcessEnvironment.h"

/* terminal processing ******************************************************
// **************************************************************************
*/

// This program only targets virtual terminals provided by system console drivers.
// So we just hardwire what we KNOW to be the TERM value appropriate to each system.
static inline
const char *
default_term()
{
#if defined(__LINUX__) || defined(__linux__)
	return "linux";
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	// As of 2010 the FreeBSD console itself is "teken", and this type is in terminfo as of 2017.
	// There is a termcap definition for teken supplied in the external configuration import subsystem.
	return "teken";
#elif defined(__NetBSD__)
	// This is the case for consoles configured as "vt100".
	return "pcvtXX";
#elif defined(__OpenBSD__)
	// This is the case for consoles configured as "vt220".
	return "pccon";
#else
	// MacOS X doesn't even have virtual consoles.
#	error "Don't know what the terminal type for your console driver is."
#endif
}

// Get the UID to which the TTY should be reset.
static inline uid_t get_tty_uid() { return geteuid(); }

// Get the GID to which the TTY should be reset.
static inline
gid_t
get_tty_gid()
{
	// If there's a group named "tty", assume the sysop wants all TTYs to belong to it at TTY reset.
	if (struct group * g = getgrnam("tty"))
		return g->gr_gid;
	// Or this program could be set-GID "tty".
	return getegid();
}

// This is the equivalent of grantpt() for virtual consoles.
static inline
int
grantvc (
	const char * tty
) {
	int rc;
	rc = chown(tty, get_tty_uid(), get_tty_gid());
	if (0 != rc) return rc;
	rc = chmod(tty, 0600);
	if (0 != rc) return rc;
	return rc;

}

// This is the equivalent of unlockpt() for virtual consoles.
static inline
int
unlockvc (
	const char * /*tty*/
) {
	return 0;
}

/* Main function ************************************************************
// **************************************************************************
*/

void
vc_get_tty ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	bool full_hostname(false);
	bool user_space_virtual_terminal(false);
	const char * term = default_term();

	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition full_hostname_option('\0', "long-hostname", "Use the full hostname.", full_hostname);
		popt::string_definition term_option('\0', "term", "type", "Specify an alternative TERM environment variable value.", term);
		popt::definition * top_table[] = {
			&full_hostname_option,
			&term_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{virtual-console-id} {prog}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing TTY name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * tty_base(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	std::string tty_str;
	const char * tty(tty_base);
	if ('/' != tty[0]) {
		// Attempt to expand into either /dev/XXX, /var/dev/XXX, or /run/dev/XXX .
		struct stat buf;
		tty_str = "/dev/";
		tty_str += tty_base;
		if (0 == stat(tty_str.c_str(), &buf)) 
			tty = tty_str.c_str();
		else {
			tty_str = "/var/dev/";
			tty_str += tty_base;
			if (0 == stat(tty_str.c_str(), &buf)) {
				user_space_virtual_terminal = true;
				tty = tty_str.c_str();
			} else {
				tty_str = "/run/dev/";
				tty_str += tty_base;
				if (0 == stat(tty_str.c_str(), &buf)) {
					user_space_virtual_terminal = true;
					tty = tty_str.c_str();
				}
			}
		}
	}
	std::vector<char> hostname(sysconf(_SC_HOST_NAME_MAX) + 1);
	if (0 != gethostname(hostname.data(), hostname.size())) 
		hostname[0] = '\0';
	else {
		hostname[hostname.size() - 1] = '\0';
		if (!full_hostname) {
			if (char * dot = std::strchr(hostname.data(), '.'))
				*dot = '\0';
		}
	}

	if (0 != grantvc(tty) || 0 != unlockvc(tty)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, tty, std::strerror(error));
		throw EXIT_FAILURE;
	}

	// Export the tty name to the environment so that later programs can get hold of it.
	envs.set("TTY", tty);

	// Export the hostname, similarly.
	envs.set("HOST", hostname.data());

	envs.set("TERM", term);
	if (user_space_virtual_terminal)
		envs.set("COLORTERM", "truecolor");
}
