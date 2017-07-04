/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_FDUTILS_H)
#define INCLUDE_FDUTILS_H

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <cstddef>
#include <fcntl.h>
#include <unistd.h>

extern
int 
open_exec_at (
	int dir_fd, 
	const char * name
);

extern
int 
open_non_interpreted_exec_at (
	int dir_fd, 
	const char * name
);

extern inline
int 
open_dir_at (
	int fd, 
	const char * name
) {
	return openat(fd, name, 
			O_NOCTTY|O_CLOEXEC
#if defined(O_DIRECTORY)
			|O_DIRECTORY
#endif
			|O_RDONLY|O_NONBLOCK
		);
}

extern inline
int 
open_appendcreate_at (
	int fd, 
	const char * name,
	int mode
) {
	return openat(fd, name, O_NOCTTY|O_CLOEXEC|O_WRONLY|O_CREAT|O_APPEND|O_NONBLOCK, mode);
}

extern inline
int 
open_appendexisting_at (
	int fd, 
	const char * name
) {
	return openat(fd, name, O_NOCTTY|O_CLOEXEC|O_WRONLY|O_APPEND|O_NONBLOCK);
}

extern inline
int 
open_read_at (
	int fd, 
	const char * name
) {
	return openat(fd, name, O_NOCTTY|O_CLOEXEC|O_RDONLY|O_NONBLOCK);
}

extern inline
int 
open_writecreate_at (
	int fd, 
	const char * name,
	int mode
) {
	return openat(fd, name, O_NOCTTY|O_CLOEXEC|O_WRONLY|O_CREAT|O_NONBLOCK, mode);
}

extern inline
int 
open_writetrunc_at (
	int fd, 
	const char * name,
	int mode
) {
	return openat(fd, name, O_NOCTTY|O_CLOEXEC|O_WRONLY|O_CREAT|O_TRUNC|O_NONBLOCK, mode);
}

extern inline
int 
open_writetruncexisting_at (
	int fd, 
	const char * name
) {
	return openat(fd, name, O_NOCTTY|O_CLOEXEC|O_WRONLY|O_TRUNC|O_NONBLOCK);
}

extern inline
int 
open_writeexisting_at (
	int fd, 
	const char * name
) {
	return openat(fd, name, O_NOCTTY|O_CLOEXEC|O_WRONLY|O_NONBLOCK);
}

extern inline
int 
open_writeexisting_or_wait_at (
	int fd, 
	const char * name
) {
	return openat(fd, name, O_NOCTTY|O_CLOEXEC|O_WRONLY);
}

extern inline
int 
open_readwritecreate_at (
	int fd, 
	const char * name,
	int mode
) {
	return openat(fd, name, O_NOCTTY|O_CLOEXEC|O_RDWR|O_CREAT|O_NONBLOCK, mode);
}

extern inline
int 
open_readwriteexisting_at (
	int fd, 
	const char * name
) {
	return openat(fd, name, O_NOCTTY|O_CLOEXEC|O_RDWR|O_NONBLOCK);
}

extern
int
open_lockfile_at (
	int dir_fd,
	const char * name
);

extern inline
int 
lock_exclusive (
	int fd 
) {
	return flock(fd, LOCK_EX|LOCK_NB);
}

extern
int
open_lockfile_or_wait_at (
	int dir_fd,
	const char * name
);

extern inline
int 
lock_exclusive_or_wait (
	int fd 
) {
	return flock(fd, LOCK_EX);
}

extern inline
bool
is_directory (
	int fd,
	struct stat & s
) {
	return 0 <= fstat(fd, &s) && S_ISDIR(s.st_mode);
}

extern inline
bool
is_fifo (
	int fd
) {
	struct stat s;
	return 0 <= fstat(fd, &s) && S_ISFIFO(s.st_mode);
}

extern inline
bool
is_regular (
	int fd
) {
	struct stat s;
	return 0 <= fstat(fd, &s) && S_ISREG(s.st_mode);
}

extern inline
int
set_close_on_exec (
	int fd,
	bool v
) {
	int flags(fcntl(fd, F_GETFD));
	if (0 > flags) return flags;
	if (v) flags |= FD_CLOEXEC; else flags &= ~FD_CLOEXEC;
	return fcntl(fd, F_SETFD, flags);
}

extern inline
int
set_non_blocking (
	int fd,
	bool v
) {
	int flags(fcntl(fd, F_GETFL));
	if (0 > flags) return flags;
	if (v) flags |= O_NONBLOCK; else flags &= ~O_NONBLOCK;
	return fcntl(fd, F_SETFL, flags);
}

extern
int
pipe_close_on_exec (
	int fds[2]
);

extern
int
socket_close_on_exec (
	int domain,
	int type,
	int protocol
);
extern
int
socket_set_boolean_option (
	int socket,
	int level,
	int name,
	bool value
) ;
extern
int
socket_set_bind_to_any (
	int s,
	const struct addrinfo & info,
	bool v
) ;
extern
int
socket_connect (
	int s,
	const void * addr,
	std::size_t len
) ;

#endif
