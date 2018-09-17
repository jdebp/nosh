/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_SERVICE_MANAGER_CLIENT_H)
#define INCLUDE_SERVICE_MANAGER_CLIENT_H

#include <string>

extern bool per_user_mode;	// Shared with the system manager client API.

struct ProcessEnvironment;

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
	int service_dir_fd
);
void
make_pipe_connectable (
	const char * prog,
	int socket_fd,
	int supervise_dir_fd
);
void
make_input_activated (
	const char * prog,
	int socket_fd,
	int supervise_dir_fd
);
void
make_run_on_empty (
	const char * prog,
	int socket_fd,
	int supervise_dir_fd
);
#if 1	/// \todo TODO: Eventually we can switch off this mechanism.
void
unload (
	const char * prog,
	int socket_fd,
	int supervise_dir_fd
);
#endif
int
start (
	int supervise_dir_fd
);
int
stop (
	int supervise_dir_fd
);
int
do_not_restart (
	int supervise_dir_fd
);
int
unload_when_stopped (
	int supervise_dir_fd
);
int
continue_daemon (
	int supervise_dir_fd
);
int
terminate_daemon (
	int supervise_dir_fd
);
int
terminate_daemon_main_process (
	int supervise_dir_fd
);
int
kill_daemon (
	int supervise_dir_fd
);
int
kill_daemon_main_process (
	int supervise_dir_fd
);
int
hangup_daemon (
	int supervise_dir_fd
);
int
hangup_daemon_main_process (
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
int
after_run_status_file (
	const int status_file_fd,
	const bool include_stopped
);
int
running_status_file (
	const int status_file_fd
);
int
running_status (
	const int supervise_dir_fd
);
int
stopped_status_file (
	const int status_file_fd
);
int
stopped_status (
	const int supervise_dir_fd
);
void
make_service (
	const int bundle_dir_fd
) ;
void
make_orderings_and_relations (
	const int bundle_dir_fd
) ;
void
make_supervise (
	const int bundle_dir_fd
) ;
void
make_supervise_fifos (
	const int supervise_dir_fd
) ;
int 
open_bundle_directory ( 
	const ProcessEnvironment & envs,
	const char * prefix, 
	const char * arg, 
	std::string & path, 
	std::string & basename, 
	std::string & suffix 
) ;
int
open_service_dir (
	const int bundle_dir_fd
) ;
int
open_supervise_dir (
	const int bundle_dir_fd
) ;
bool
has_exited_run (
	const unsigned int b,
	const char status[]
) ;
bool
has_main_pid (
	const char status[]
) ;
bool
is_initially_up (
	const int service_dir_fd
) ;
bool
is_ready_after_run (
	const int service_dir_fd
) ;
bool
is_done_after_exit (
	const int service_dir_fd
) ;
bool
is_use_hangup_signal (
	const int service_dir_fd
) ;
bool
is_use_kill_signal (
	const int service_dir_fd
) ;
int 
listen_service_manager_socket (
	const bool is_system, 
	const char * prog
) ;
int 
connect_service_manager_socket (
	const bool is_system, 
	const char * prog
) ;

#endif
