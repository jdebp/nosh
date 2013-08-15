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
	// This isn't quite correct, but it isn't worth fixing.
	// cons25 has fallen into desuetude and the new (as of 2010) emulator is xterm.
	return "xterm";
#elif defined(__NetBSD__)
	// This is properly the case.
	// vt220 is a compatible subset, and not really correct.
	return "wsvt25";
#else
	// OpenBSD can change the emulator at kernel configuration time and at runtime.
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
	std::vector<const char *> & args
) {
	bool full_hostname(false);
	const char * term = default_term();

	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition full_hostname_option('\0', "long-hostname", "Use the full hostname.", full_hostname);
		popt::string_definition term_option('\0', "term", "type", "Specify an alternative TERM environment variable value.", term);
		popt::definition * top_table[] = {
			&full_hostname_option,
			&term_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "virtual-console-id prog");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing TTY name.");
		throw EXIT_FAILURE;
	}
	const char * tty_base(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	std::string tty_str;
	const char * tty(tty_base);
	if ('/' != tty[0]) {
		if (std::isdigit(tty[0])) {
			// If the basename begins with a digit, attempt to expand into either /dev/vc/N or /dev/ttyN .
			// This allows using N consistently whether one has udev (/dev/ttyN) or devfs (/dev/vc/N).
			struct stat buf;
			tty_str = "/dev/vc/";
			tty_str += tty_base;
			tty = tty_str.c_str();
			if (stat(tty, &buf) && ENOENT == errno) {
				tty_str = "/dev/tty";
				tty_str += tty_base;
				tty = tty_str.c_str();
			}
		} else {
			// Just treat as a filename in /dev/ .
			tty_str = "/dev/";
			tty_str += tty_base;
			tty = tty_str.c_str();
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
	setenv("TTY", tty, 1);

	// Export the hostname, similarly.
	// One program that can use this is login-process, which registers the hostname in utmp/wtmp.
	setenv("HOST", hostname.data(), 1);

	setenv("TERM", term, 1);
}
