/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <climits>
#include <csignal>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <grp.h>
#include "utils.h"
#include "popt.h"
#include "listen.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
local_datagram_socket_listen ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	signed long uid(-1), gid(-1), mode(0700);
	bool has_uid(false), has_gid(false), has_mode(false);
	bool systemd_compatibility(false);
	try {
		popt::bool_definition systemd_compatibility_option('\0', "systemd-compatibility", "Set the $LISTEN_FDS and $LISTEN_PID environment variables for compatibility with systemd.", systemd_compatibility);
		popt::signed_number_definition uid_option('u', "uid", "number", "Specify the UID for the bound socket filename.", uid, 0);
		popt::signed_number_definition gid_option('g', "gid", "number", "Specify the GID for the bound socket filename.", gid, 0);
		popt::signed_number_definition mode_option('m', "mode", "number", "Specify the permissions for the bound socket filename.", mode, 0);
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

	// FIXME: Can we even get a SIGPIPE from listen()?
	sigset_t original_signals;
	sigprocmask(SIG_SETMASK, 0, &original_signals);
	sigset_t masked_signals(original_signals);
	sigaddset(&masked_signals, SIGPIPE);
	sigprocmask(SIG_SETMASK, &masked_signals, 0);

	sockaddr_un addr;
	addr.sun_family = AF_LOCAL;
	const size_t listenpath_len(std::strlen(listenpath));
	if (listenpath_len >= sizeof addr.sun_path) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Listen path name is too long.");
		throw EXIT_FAILURE;
	}
	memcpy(addr.sun_path, listenpath, listenpath_len + 1);
#if !defined(__LINUX__) && !defined(__linux__)
	addr.sun_len = SUN_LEN(&addr);
#endif

	const int s(socket(AF_LOCAL, SOCK_DGRAM, 0));
	if (0 > s) {
exit_error:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
		throw EXIT_FAILURE;
	}
	unlink(listenpath);
	if (0 > bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof addr)) goto exit_error;
	if (has_uid || has_gid) {
		if (0 > chown(listenpath, uid, gid)) goto exit_error;
	}
	if (has_mode) {
		if (0 > chmod(listenpath, mode)) goto exit_error;
	}

	if (0 > dup2(s, LISTEN_SOCKET_FILENO)) goto exit_error;
	if (LISTEN_SOCKET_FILENO != s) close(s);

	if (systemd_compatibility) {
		setenv("LISTEN_FDS", "1", 1);
		char pid[64];
		snprintf(pid, sizeof pid, "%u", getpid());
		setenv("LISTEN_PID", pid, 1);
	}

	sigprocmask(SIG_SETMASK, &original_signals, 0);
}
