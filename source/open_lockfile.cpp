/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <sys/types.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include "fdutils.h"

extern
int
open_lockfile_at (
	int dir_fd,
	const char * name
) {
#if defined O_EXLOCK
	return openat(dir_fd, name, O_NOCTTY|O_CLOEXEC|O_WRONLY|O_CREAT|O_APPEND|O_NONBLOCK|O_EXLOCK, 0600);
#else
	const int lock_fd(open_appendcreate_at(dir_fd, name, 0600));
	if (0 >	lock_exclusive(lock_fd)) {
		close(lock_fd);
		return -1;
	}
	return lock_fd;
#endif
}
