/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <climits>
#include <csignal>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "utils.h"
#include "popt.h"
#include "fdutils.h"
#include "listen.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
fifo_listen ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	signed long uid(-1), gid(-1), mode(0600);
	bool has_uid(false), has_gid(false), has_mode(false);
	bool systemd_compatibility(false);
	try {
		popt::bool_definition systemd_compatibility_option('\0', "systemd-compatibility", "Set the $LISTEN_FDS and $LISTEN_PID environment variables for compatibility with systemd.", systemd_compatibility);
		popt::signed_number_definition uid_option('u', "uid", "number", "Specify the UID for the FIFO filename.", uid, 0);
		popt::signed_number_definition gid_option('g', "gid", "number", "Specify the GID for the FIFO filename.", gid, 0);
		popt::signed_number_definition mode_option('m', "mode", "number", "Specify the permissions for the FIFO filename.", mode, 0);
		popt::definition * top_table[] = {
			&systemd_compatibility_option,
			&uid_option,
			&gid_option,
			&mode_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "path prog");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		has_uid = uid_option.is_set();
		has_gid = gid_option.is_set();
		has_mode = mode_option.is_set();
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing listen path name.");
		throw EXIT_FAILURE;
	}
	const char * listenpath(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	mkfifoat(AT_FDCWD, listenpath, mode);
	const int s(open_read_at(AT_FDCWD, listenpath));
	if (0 > s) {
exit_error:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (has_mode) {
		if (0 > fchmod(s, mode)) goto exit_error;
	}
	if (has_uid || has_gid) {
		if (0 > fchown(s, uid, gid)) goto exit_error;
	}

	if (LISTEN_SOCKET_FILENO != s) {
		if (0 > dup2(s, LISTEN_SOCKET_FILENO)) goto exit_error;
		close(s);
	}
	set_close_on_exec(LISTEN_SOCKET_FILENO, false);

	if (systemd_compatibility) {
		setenv("LISTEN_FDS", "1", 1);
		char pid[64];
		snprintf(pid, sizeof pid, "%u", getpid());
		setenv("LISTEN_PID", pid, 1);
	}
}
