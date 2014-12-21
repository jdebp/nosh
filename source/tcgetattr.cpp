/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cerrno>
#include <termios.h>
#include "ttyutils.h"

int
tcgetattr_nointr (
	int fd,
	struct termios & t
) {
	for (;;) {
		const int rc(tcgetattr(fd, &t));
		if (rc >= 0 || EINTR != errno) return rc;
	}
}
