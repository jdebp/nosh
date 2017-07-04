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
#include "fdutils.h"
#include "popt.h"
#include "FileDescriptorOwner.h"

/* Main function ************************************************************
// **************************************************************************
*/

// This must have static storage duration as we are using it in args.
static std::string progress_option;

static const char socket_name[] = "/run/fsck.progress";

void
monitored_fsck ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));

	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_name, sizeof addr.sun_path);
#if !defined(__LINUX__) && !defined(__linux__)
	addr.sun_len = SUN_LEN(&addr);
#endif

	FileDescriptorOwner socket_fd(socket(AF_UNIX, SOCK_STREAM, 0));
	if (0 > socket_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s\n", prog, std::strerror(error));
	} else {
		if (0 <= socket_connect(socket_fd.get(), &addr, sizeof addr)) {
#if defined(__LINUX__) || defined(__linux__)
			char buf[64];
			snprintf(buf, sizeof buf, "-C%d", socket_fd.get());
			progress_option = buf;
			std::vector<const char *>::iterator p(args.begin());
			args.insert(++p, progress_option.c_str());
			socket_fd.release();
#endif
		}
	}

	next_prog = args[0] = "fsck";
}
