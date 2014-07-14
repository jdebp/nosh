/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_SERVICE_MANAGER_CLIENT_H)
#define INCLUDE_SERVICE_MANAGER_CLIENT_H

void
plumb (
	const char * prog,
	int socket_fd,
	int out_supervise_dir_fd,
	int in_supervise_dir_fd
);
void
load (
	const char * prog,
	int socket_fd,
	const char * name,
	int supervise_dir_fd,
	int service_dir_fd,
	bool want_pipe,
	bool run_on_empty
);
void
make_input_activated (
	const char * prog,
	int socket_fd,
	int supervise_dir_fd
);
int
start (
	int supervise_dir_fd
);
int
stop (
	int supervise_dir_fd
);
int
terminate_daemon (
	int supervise_dir_fd
);
int
kill_daemon (
	int supervise_dir_fd
);
bool
is_ok (
	const int supervise_dir_fd
);
bool
wait_ok (
	const int supervise_dir_fd,
	int timeout
);
bool
is_up (
	const int supervise_dir_fd
);
void
make_supervise (
	const int bundle_dir_fd
);
void
make_supervise_fifos (
	const int supervise_dir_fd
);
int
open_service_dir (
	const int bundle_dir_fd
);
int
open_supervise_dir (
	const int bundle_dir_fd
);
bool
is_initially_up (
	const int service_dir_fd
);
bool
is_done_after_exit (
	const int service_dir_fd
);

#endif
