/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "utils.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

// This must have static storage duration as we are using it in args.
static std::string progress_option;

static const char socket_name[] = "/run/fsck.progress";

void
monitored_fsck ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));

	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_name, sizeof addr.sun_path);
#if !defined(__LINUX__) && !defined(__linux__)
	addr.sun_len = SUN_LEN(&addr);
#endif

	const int socket_fd(socket(AF_UNIX, SOCK_STREAM, 0));
	if (0 > socket_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s\n", prog, std::strerror(error));
	} else {
		if (0 <= connect(socket_fd, reinterpret_cast<const sockaddr *>(&addr), sizeof addr)) {
#if defined(__LINUX__) || defined(__linux__)
			char buf[64];
			snprintf(buf, sizeof buf, "-C%d", socket_fd);
			progress_option = buf;
			std::vector<const char *>::iterator p(args.begin());
			args.insert(++p, progress_option.c_str());
#else
			close(socket_fd);
#endif
		} else {
			close(socket_fd);
		}
	}

	next_prog = args[0] = "fsck";
}
