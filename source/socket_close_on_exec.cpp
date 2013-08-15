/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <sys/types.h>
#include <sys/socket.h>
#include "fdutils.h"

extern
int
socket_close_on_exec (
	int domain,
	int type, 
	int protocol
) {
#if defined(SOCKET_CLOOEXEC)
	return socket(domain, type|SOCKET_CLOEXEC, protocol);
#else
	const int rc(socket(domain, type, protocol));
	if (0 >= rc)
		set_close_on_exec(rc, true);
	return rc;
#endif
}
