/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cerrno>
#include <termios.h>
#include <sys/ioctl.h>
#include "utils.h"

int
tcsetwinsz_nointr (
	int fd,
	const struct winsize & w
) {
	for (;;) {
		const int rc(ioctl(fd, TIOCSWINSZ, &w));
		if (rc >= 0 || EINTR != errno) return rc;
	}
}
