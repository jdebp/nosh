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
	const char * prog,
	int supervise_dir_fd
);
int
stop (
	const char * prog,
	int supervise_dir_fd
);
bool
is_ok (
	const int dir_fd
);
bool
wait_ok (
	const int supervise_dir_fd,
	int timeout
);

#endif
