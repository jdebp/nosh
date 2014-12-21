/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cerrno>
#include <termios.h>
#include "ttyutils.h"

int
tcsetattr_nointr (
	int fd,
	int mode,
	const struct termios & t
) {
	for (;;) {
		const int rc(tcsetattr(fd, mode, &t));
		if (rc >= 0 || EINTR != errno) return rc;
	}
}
