/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include "ttyname.h"
#include "ProcessEnvironment.h"

const char *
get_controlling_tty_name (const ProcessEnvironment & envs)
{
	const char * tty(envs.query("TTY"));
	// Fall back to the way that /bin/tty does this.
	if (!tty) tty = ttyname(STDIN_FILENO);
	return tty;
}
