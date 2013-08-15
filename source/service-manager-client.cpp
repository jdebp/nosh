/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstring>
#include <cstdio>
#include <cerrno>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "fdutils.h"
#include "service-manager-client.h"
#include "service-manager.h"

static
void
do_rpc_call (
	const char * prog,
	int socket_fd,
	const void * base,
	size_t len,
	const int fds[],
	size_t count_fds
) {
	struct iovec v[1] = { { const_cast<void*>(base), len } };
	char buf[CMSG_SPACE(count_fds * sizeof *fds)];
	struct msghdr msg = {
		0, 0,
		v, sizeof v/sizeof *v,
		buf, sizeof buf,
		0
	};
	struct cmsghdr *cmsg(CMSG_FIRSTHDR(&msg));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(count_fds * sizeof *fds);
	memcpy(CMSG_DATA(cmsg), fds, count_fds * sizeof *fds);
	for (unsigned retries(0U); retries < 5U; ++retries) {
		const int rc(sendmsg(socket_fd, &msg, 0));
		if (0 <= rc) break;
		const int error(errno);
		if (ENOBUFS != error) {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
			break;
		}
		sleep(1);
	}
}

/* Service Manager control API RPC wrappers *********************************
// **************************************************************************
*/

void
plumb (
	const char * prog,
	int socket_fd,
	int out_supervise_dir_fd,
	int in_supervise_dir_fd
) {
	service_manager_rpc_message m;
	m.command = m.PLUMB;
	int fds[2] = { out_supervise_dir_fd, in_supervise_dir_fd };
	do_rpc_call(prog, socket_fd, &m, sizeof m, fds, sizeof fds/sizeof *fds);
}

void
load (
	const char * prog,
	int socket_fd,
	const char * name,
	int supervise_dir_fd,
	int service_dir_fd,
	bool want_pipe,
	bool run_on_empty
) {
	service_manager_rpc_message m;
	m.command = m.LOAD;
	m.want_pipe = want_pipe;
	m.run_on_empty = run_on_empty;
	std::strncpy(m.name, name, sizeof m.name);
	int fds[2] = { supervise_dir_fd, service_dir_fd };
	do_rpc_call(prog, socket_fd, &m, sizeof m, fds, sizeof fds/sizeof *fds);
}

void
make_input_activated (
	const char * prog,
	int socket_fd,
	int supervise_dir_fd
) {
	service_manager_rpc_message m;
	m.command = m.MAKE_INPUT_ACTIVATED;
	int fds[1] = { supervise_dir_fd };
	do_rpc_call(prog, socket_fd, &m, sizeof m, fds, sizeof fds/sizeof *fds);
}

static
int
start_stop_common (
	const char * prog,
	int supervise_dir_fd,
	char command
) {
	int control_fd(open_writeexisting_at(supervise_dir_fd, "control"));
	if (0 > control_fd) return -1;
	while (true) {
		const int n(write(control_fd, &command, 1));
		if (0 > n) {
			const int error(errno);
			if (EINTR == error) continue;
			close(control_fd);
			return -1;
		}
		break;
	}
	close(control_fd);
	return 0;
}

int
start (
	const char * prog,
	int supervise_dir_fd
) {
	return start_stop_common(prog, supervise_dir_fd, 'u');
}

int
stop (
	const char * prog,
	int supervise_dir_fd
) {
	return start_stop_common(prog, supervise_dir_fd, 'd');
}

bool
is_ok (
	const int supervise_dir_fd
) {
	const int ok_fd(open_writeexisting_at(supervise_dir_fd, "ok"));
	const bool r(0 <= ok_fd);
	if (r) close(ok_fd);
	return r;
}

bool
wait_ok (
	const int supervise_dir_fd,
	int timeout
) {
	if (0 > timeout) {
		const int ok_fd(open_writeexisting_or_wait_at(supervise_dir_fd, "ok"));
		if (0 <= ok_fd) close(ok_fd);
		return 0 <= ok_fd;
	} else {
		for (;;) {
			const int ok_fd(open_writeexisting_at(supervise_dir_fd, "ok"));
			if (0 <= ok_fd) {
				close(ok_fd);
				return true;
			}
			if (0 == timeout) return false;
			sleep(1);
			timeout = (timeout < 1000) ? 0 : timeout - 1000;
		}
	}
}
