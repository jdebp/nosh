/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cerrno>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <netdb.h>
#include "fdutils.h"

int
socket_set_boolean_option (
	int s,
	int l,
	int n,
	bool v
) {
	const int optval(v);
	return setsockopt(s, l, n, &optval, sizeof optval);
}

#if defined(__FreeBSD__)
static inline
int
socket_set_boolean_option_special (
	int s,
	int l,
	int n,
	bool v
) {
	const int optval(v);
	int r(setsockopt(s, l, n, &optval, sizeof optval));
	if (0 > r && EINVAL == errno) r = 0;
	return r;
}
#endif

int
socket_set_bind_to_any (
	int s,
	const addrinfo & info,
	bool v
) {
#if defined(SO_BINDANY)
	static_cast<void>(info);	// Silence a compiler warning.
	return socket_set_boolean_option(s, SOL_SOCKET, SO_BINDANY, v);
#elif defined(__LINUX__) || defined(__linux__)
	static_cast<void>(info);	// Silence a compiler warning.
	return socket_set_boolean_option(s, SOL_IP, IP_FREEBIND, v);
#elif defined(__FreeBSD__)
	switch (info.ai_family) {
		case AF_INET:	return socket_set_boolean_option_special(s, IPPROTO_IPV4, IP_BINDANY, v);
		case AF_INET6:	return socket_set_boolean_option(s, IPPROTO_IPV6, IP_BINDANY, v);
	}
	return errno = EINVAL, -1;
#else
	switch (info.ai_family) {
		case AF_INET:	return socket_set_boolean_option(s, IPPROTO_IPV4, IP_BINDANY, v);
		case AF_INET6:	return socket_set_boolean_option(s, IPPROTO_IPV6, IP_BINDANY, v);
	}
	return errno = EINVAL, -1;
#endif
}
