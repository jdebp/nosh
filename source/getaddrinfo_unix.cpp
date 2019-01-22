/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <unistd.h>
#include "utils.h"

int
getaddrinfo (
	const char * pathname,
	const addrinfo * hints,
	addrinfo ** info_list
) {
	*info_list = 0;
#if !defined(EAI_BADHINTS)
#define EAI_BADHINTS EAI_BADFLAGS
#endif

	if (!pathname)
		return EAI_NONAME;
	if (hints) {
		if (hints->ai_addrlen || hints->ai_canonname || hints->ai_addr || hints->ai_next)
			return EAI_BADHINTS;
#if defined(AI_MASK)
		if (hints->ai_flags & ~AI_MASK)
			return EAI_BADFLAGS;
#endif
		switch (hints->ai_family) {
		case AF_UNSPEC:
		case AF_UNIX:
			break;
		default:
			return EAI_FAMILY;
		}
		if (0 != hints->ai_protocol)
			return EAI_BADHINTS;
		if (SOCK_STREAM != hints->ai_socktype && SOCK_SEQPACKET != hints->ai_socktype && SOCK_DGRAM != hints->ai_socktype)
			return EAI_BADHINTS;
	}

	const std::size_t l(sizeof(**info_list) + sizeof(struct sockaddr_un));
	addrinfo * ai(static_cast<addrinfo *>(malloc(l)));
	if (!ai) return EAI_SYSTEM;
	std::memset(ai, 0, l);

	struct sockaddr_un & addr(*reinterpret_cast<sockaddr_un *>(ai + 1));
	strncpy(addr.sun_path, pathname, sizeof addr.sun_path);
	addr.sun_family = AF_UNIX;
#if !defined(__LINUX__) && !defined(__linux__)
	addr.sun_len = SUN_LEN(&addr);
#endif

	ai->ai_addr = reinterpret_cast<struct sockaddr *>(&addr);
	ai->ai_addrlen = sizeof addr;
	ai->ai_family = addr.sun_family;
	ai->ai_canonname = 0;
	ai->ai_next = 0;
	if (hints) {
		ai->ai_socktype = hints->ai_socktype;
		ai->ai_protocol = hints->ai_protocol;
	} else {
		ai->ai_socktype = 0;
		ai->ai_protocol = 0;
	}

	*info_list = ai;
	return 0;
}
