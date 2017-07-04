/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <sys/types.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <cerrno>
#include "fdutils.h"

static inline
unsigned int
page_size()
{
	const long rc(sysconf(_SC_PAGE_SIZE));
	if (0 > rc) 
#if defined(PAGE_SIZE)
		return PAGE_SIZE;
#else
		return 4096;
#endif
	return static_cast<unsigned int>(rc);
}

static inline
bool
is_interpreter_magic (
	int fd
) {
	std::string buf(page_size(), '\0');
	const ssize_t n(pread(fd, const_cast<char *>(buf.data()), buf.length(), 0));

	// Minimum possible size, including non-whitespace and a terminator.
	if (n < 4) return false;

	if (buf[0] != '#' || buf[1] != '!') return false;

	int i(2);

	// Skip leading whitespace
	while (i < n) 
		if (buf[i] && std::isspace(buf[i])) ++i; else break;
	const int beg(i);

	// Skip non-terminators
	while (i < n) 
		if (std::isspace(buf[i])) break; else ++i;

	return i < n && i > beg;
}

extern
int 
open_non_interpreted_exec_at (
	int dir_fd, 
	const char * name
) {
	// Witness this SUS/POSIX madness.
	int fd(openat(dir_fd, name, O_NOCTTY|O_CLOEXEC|O_RDONLY|O_NONBLOCK));
	if (0 <= fd) {
		if (is_interpreter_magic(fd)) {
			close(fd);
			errno = ENOEXEC;
			return -1;
		}
	}
#if defined(O_EXEC)
	const int efd(openat(dir_fd, name, O_NOCTTY|O_CLOEXEC|O_EXEC|O_NONBLOCK));
	if (0 <= efd) {
		if (0 <= fd) close(fd);
		fd = efd;
	}
#endif
	return fd;
}

extern
int 
open_exec_at (
	int dir_fd, 
	const char * name
) {
	// Witness this SUS/POSIX madness.
	int fd(openat(dir_fd, name, O_NOCTTY|O_CLOEXEC|O_RDONLY|O_NONBLOCK));
	if (0 <= fd) {
		if (is_interpreter_magic(fd)) {
			// Executable scripts need to see a valid /dev/fd/N that must be readable.
			set_close_on_exec(fd, false);
			return fd;
		}
	}
#if defined(O_EXEC)
	const int efd(openat(dir_fd, name, O_NOCTTY|O_CLOEXEC|O_EXEC|O_NONBLOCK));
	if (0 <= efd) {
		if (0 <= fd) close(fd);
		fd = efd;
	}
#endif
	return fd;
}
