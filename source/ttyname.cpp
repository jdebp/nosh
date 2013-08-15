/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include "ttyname.h"

const char *
get_controlling_tty_name ()
{
	const char * tty(std::getenv("TTY"));
	// Fall back to the way that /bin/tty does this.
	if (!tty) tty = ttyname(STDIN_FILENO);
	return tty;
}
