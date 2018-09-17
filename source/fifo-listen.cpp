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
#include <pwd.h>
#include <grp.h>
#include "utils.h"
#include "popt.h"
#include "fdutils.h"
#include "ProcessEnvironment.h"
#include "listen.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
fifo_listen ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	signed long uid(-1), gid(-1), mode(0600);
	bool has_uid(false), has_gid(false), has_mode(false);
	const char * user(0), * group(0);
	bool systemd_compatibility(false);
	try {
		popt::bool_definition systemd_compatibility_option('\0', "systemd-compatibility", "Set the $LISTEN_FDS and $LISTEN_PID environment variables for compatibility with systemd.", systemd_compatibility);
		popt::signed_number_definition uid_option('u', "uid", "number", "Specify the UID for the FIFO filename.", uid, 0);
		popt::signed_number_definition gid_option('g', "gid", "number", "Specify the GID for the FIFO filename.", gid, 0);
		popt::signed_number_definition mode_option('m', "mode", "number", "Specify the permissions for the FIFO filename.", mode, 0);
		popt::string_definition user_option('\0', "user", "name", "Specify the user for the FIFO filename.", user);
		popt::string_definition group_option('\0', "group", "name", "Specify the group for the FIFO filename.", group);
		popt::definition * top_table[] = {
			&systemd_compatibility_option,
			&uid_option,
			&gid_option,
			&mode_option,
			&user_option,
			&group_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{path} {prog}");

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
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing listen path name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * listenpath(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	mkfifoat(AT_FDCWD, listenpath, mode);
#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)
	const int s(open_readwriteexisting_at(AT_FDCWD, listenpath));
#else
	const int s(open_read_at(AT_FDCWD, listenpath));
#endif
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
	} else
	if (user || group) {
		struct passwd * u(user ? getpwnam(user) : 0);
		struct group * g(group ? getgrnam(group) : 0);
		endgrent();
		endpwent();
		if (user && !u) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, user, "No such user");
			throw EXIT_FAILURE;
		}
		if (group && !g) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, group, "No such group");
			throw EXIT_FAILURE;
		}
		if (0 > fchown(s, u ? u->pw_uid : -1, g ? g->gr_gid : -1)) goto exit_error;
	}

	int fd_index(systemd_compatibility ? query_listen_fds_passthrough(envs) : 0);
	if (LISTEN_SOCKET_FILENO + fd_index != s) {
		if (0 > dup2(s, LISTEN_SOCKET_FILENO + fd_index)) goto exit_error;
		close(s);
	}
	set_close_on_exec(LISTEN_SOCKET_FILENO + fd_index, false);
	++fd_index;

	if (systemd_compatibility) {
		char buf[64];
		snprintf(buf, sizeof buf, "%d", fd_index);
		envs.set("LISTEN_FDS", buf);
		snprintf(buf, sizeof buf, "%u", getpid());
		envs.set("LISTEN_PID", buf);
	}
}
