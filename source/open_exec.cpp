/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <sys/types.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include "fdutils.h"

static inline
int
page_size()
{
	const int rc(sysconf(_SC_PAGE_SIZE));
	if (0 > rc) 
#if defined(PAGE_SIZE)
		return PAGE_SIZE;
#else
		return 4096;
#endif
	return rc;
}

static inline
bool
is_interpreter_magic (
	int fd
) {
	std::string buf(page_size(), '\0');
	const int n(pread(fd, const_cast<char *>(buf.data()), buf.length(), 0));

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
open_exec_at (
	int dir_fd, 
	const char * name
) {
	// Witness this SUS/POSIX madness.
	int fd(openat(dir_fd, name, O_NOCTTY|O_CLOEXEC|O_RDONLY|O_NONBLOCK));
	if (0 > fd) {
#if defined(O_EXEC)
		return openat(dir_fd, name, O_NOCTTY|O_CLOEXEC|O_EXEC|O_NONBLOCK);
#else
		return fd;
#endif
	}
	if (is_interpreter_magic(fd)) 
		// Executable scripts need to see a valid /dev/fd/N that must be readable.
		set_close_on_exec(fd, false);	
	else {
#if defined(O_EXEC)
		const int efd(openat(dir_fd, name, O_NOCTTY|O_CLOEXEC|O_EXEC|O_NONBLOCK));
		if (0 <= efd) {
			close(fd);
			fd = efd;
		}
#endif
	}
	return fd;
}
