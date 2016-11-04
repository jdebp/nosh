/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cerrno>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include "fdutils.h"

/// Safe version of blocking connect()
/// Unfortunately, signals can interrupt connect() and there are no sane SA_RESTART semantics.
/// Try re-issuing the connect() call and an error is raised because a connection is already in progress.
/// If a signal interrupts blocking connect(), we have to execute the same code as if the socket were non-blocking.
int
socket_connect (
	int s,
	const  void * a,
	std::size_t l
) {
	const int r(connect(s, static_cast<const sockaddr *>(a), l));
	if (r >= 0) return r;
	if (EAGAIN != errno && EINTR != errno && EINPROGRESS != errno) return r;
	pollfd p;
	p.fd = s;
	p.events = POLLOUT|POLLERR;
	for (;;) {
		p.revents = 0;
		const int rc(poll(&p, 1, -1));
		if (0 > rc) {
			if (EINTR == errno) continue;
			return rc;
		}
		if (p.revents) break;
	}
	int i;
	socklen_t il = sizeof i;
	const int rc(getsockopt(s, SOL_SOCKET, SO_ERROR, &i, &il));
	if (0 > rc) return rc;
	if (0 == i) return 0;	// Success, connected.
	errno = i;
	return -1;
}
