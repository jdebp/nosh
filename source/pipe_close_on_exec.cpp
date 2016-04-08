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
pipe_close_on_exec (
	int fds[2]
) {
#if defined(__LINUX__) || defined(__linux__)
	return pipe2(fds, O_CLOEXEC);
#else
	const int rc(pipe(fds));
	if (0 <= rc) {
		set_close_on_exec(fds[0], true);
		set_close_on_exec(fds[1], true);
	}
	return rc;
#endif
}
