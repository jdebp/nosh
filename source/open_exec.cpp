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
open_exec_at (
	int dir_fd, 
	const char * name
) {
#if defined(O_EXEC)
	// Witness this SUS/POSIX madness.
	// If O_EXEC is used, the process must have execute permission on the file, and fexecve() does no additional checks.
	// But if the file is a script, the script interpreter cannot then open /dev/fd/NNN for reading.
	// This is because O_EXEC causes O_RDONLY to be ignored, and /dev/fd/NNN for an O_EXEC descriptor thus cannot be opened for reading.
	// So we must first attempt an O_RDONLY open, which will fail for a --x--x--x file, then fall back to O_EXEC.
	// O_RDONLY will incorrectly succeeed for a r--r--r-- file, even though O_EXEC would fail.
	// Which is why fexecve() checks the permissions again for a descriptor opened O_RDONLY, but doesn't for one opened O_EXEC.
	const int fd(openat(dir_fd, name, O_NOCTTY|O_CLOEXEC|O_RDONLY|O_NONBLOCK));
	if (0 <= fd) return fd;
	return openat(dir_fd, name, O_NOCTTY|O_CLOEXEC|O_EXEC|O_NONBLOCK);
#else
	return openat(dir_fd, name, O_NOCTTY|O_CLOEXEC|O_RDONLY|O_NONBLOCK);
#endif
}
