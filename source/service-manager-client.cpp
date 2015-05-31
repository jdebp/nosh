/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstring>
#include <csignal>
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
		buf, static_cast<socklen_t>(sizeof buf),
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

void
unload (
	const char * prog,
	int socket_fd,
	int supervise_dir_fd
) {
	service_manager_rpc_message m;
	m.command = m.UNLOAD;
	int fds[1] = { supervise_dir_fd };
	do_rpc_call(prog, socket_fd, &m, sizeof m, fds, sizeof fds/sizeof *fds);
}

static
int
send_control_command (
	int supervise_dir_fd,
	char command
) {
	const int control_fd(open_writeexisting_at(supervise_dir_fd, "control"));
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
	int supervise_dir_fd
) {
	return send_control_command(supervise_dir_fd, 'u');
}

int
stop (
	int supervise_dir_fd
) {
	return send_control_command(supervise_dir_fd, 'd');
}

int
terminate_daemon (
	int supervise_dir_fd
) {
	return send_control_command(supervise_dir_fd, 't');
}

int
kill_daemon (
	int supervise_dir_fd
) {
	return send_control_command(supervise_dir_fd, 'k');
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

static void sig_ignore ( int ) {}

bool
wait_ok (
	const int supervise_dir_fd,
	int timeout
) {
	if (0 > timeout) {
		const int ok_fd(open_writeexisting_or_wait_at(supervise_dir_fd, "ok"));
		if (0 <= ok_fd) close(ok_fd);
		return 0 <= ok_fd;
	} else if (0 == timeout) {
		const int ok_fd(open_writeexisting_at(supervise_dir_fd, "ok"));
		if (0 <= ok_fd) close(ok_fd);
		return 0 <= ok_fd;
	} else {
#if 0
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
#else
		struct sigaction a, o;
		a.sa_flags=0;
		sigemptyset(&a.sa_mask);
		a.sa_handler=sig_ignore;
		sigaction(SIGALRM,&a,&o);
		unsigned int old(alarm((timeout + 999) / 1000));
		const int ok_fd(open_writeexisting_or_wait_at(supervise_dir_fd, "ok"));
		if (0 <= ok_fd) close(ok_fd);
		alarm(old);
		sigaction(SIGALRM,&o,NULL);
		return 0 <= ok_fd;
#endif
	}
}

int	/// \returns status \retval -1 error \retval 0 not running \retval 1 running
running_status (
	const int supervise_dir_fd
) {
	const int status_fd(open_read_at(supervise_dir_fd, "status"));
	if (0 > status_fd) return -1;
	char status[20];
	const int n(read(status_fd, status, sizeof status));
	close(status_fd);
	if (18 > n) return -1;
	if (20 <= n) {
		return encore_status_running == status[18] ? 1 : 0;
	} else {
		return status[12] || status[13] || status[14] || status[15] ? 1 : 0;
	}
}

int	/// \returns status \retval -1 error \retval 0 not stopped \retval 1 stopped
stopped_status (
	const int supervise_dir_fd
) {
	const int status_fd(open_read_at(supervise_dir_fd, "status"));
	if (0 > status_fd) return -1;
	char status[20];
	const int n(read(status_fd, status, sizeof status));
	close(status_fd);
	if (18 > n) return -1;
	if (20 <= n) {
		return encore_status_stopped == status[18] ? 1 : 0;
	} else {
		return !status[12] && !status[13] && !status[14] && !status[15] ? 1 : 0;
	}
}

void
make_service (
	const int bundle_dir_fd
) {
	mkdirat(bundle_dir_fd, "service", 0755);
}

void
make_orderings_and_relations (
	const int bundle_dir_fd
) {
	mkdirat(bundle_dir_fd, "after", 0755);
	mkdirat(bundle_dir_fd, "before", 0755);
	mkdirat(bundle_dir_fd, "wants", 0755);
	mkdirat(bundle_dir_fd, "conflicts", 0755);
	mkdirat(bundle_dir_fd, "wanted-by", 0755);
	mkdirat(bundle_dir_fd, "required-by", 0755);
	mkdirat(bundle_dir_fd, "stopped-by", 0755);
}

void
make_supervise (
	const int bundle_dir_fd
) {
	mkdirat(bundle_dir_fd, "supervise", 0700);
}

void
make_supervise_fifos (
	const int supervise_dir_fd
) {
	mkfifoat(supervise_dir_fd, "ok", 0666);
	mkfifoat(supervise_dir_fd, "control", 0600);
}

int
open_service_dir (
	const int bundle_dir_fd
) {
	int service_dir_fd(open_dir_at(bundle_dir_fd, "service/"));
	if ((0 > service_dir_fd) && (ENOENT == errno)) service_dir_fd = dup(bundle_dir_fd);
	return service_dir_fd;
}

int
open_supervise_dir (
	const int bundle_dir_fd
) {
	int supervise_dir_fd(open_dir_at(bundle_dir_fd, "supervise/"));
	if ((0 > supervise_dir_fd) && (ENOENT == errno)) supervise_dir_fd = dup(bundle_dir_fd);
	return supervise_dir_fd;
}

bool
is_initially_up (
	const int service_dir_fd
) {
	struct stat down_file_s;
	return 0 > fstatat(service_dir_fd, "down", &down_file_s, 0) && ENOENT == errno;
}

bool
is_done_after_exit (
	const int service_dir_fd
) {
	struct stat remain_file_s;
	return 0 > fstatat(service_dir_fd, "remain", &remain_file_s, 0) && ENOENT == errno;
}
