/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "service-manager-client.h"
#include "runtime-dir.h"
#include "fdutils.h"

static inline
const char *
construct_service_manager_socket_name (
	const bool is_system,
	std::string & name_buf
) {
	if (is_system) return "/run/service-manager/control";
	name_buf = effective_user_runtime_dir() + "service-manager/control";
	return name_buf.c_str();
}

int
listen_service_manager_socket(
	const bool is_system,
	const char * prog
) {
	std::string name_buf;
	const char * const socket_name(construct_service_manager_socket_name(is_system, name_buf));
	const int socket_fd(socket_close_on_exec(AF_UNIX, SOCK_DGRAM, 0));
	if (0 > socket_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "socket", std::strerror(error));
		return socket_fd;
	}
	const int reuse_addr(1);
	if (0 > setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof reuse_addr)) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s: %s\n", prog, "setsockopt", socket_name, std::strerror(error));
	}
	sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_name, sizeof addr.sun_path);
	unlink(socket_name);
	if (0 > bind(socket_fd, reinterpret_cast<const sockaddr *>(&addr), sizeof addr)) {
		const int error(errno);
		close(socket_fd);
		std::fprintf(stderr, "%s: ERROR: %s: %s: %s\n", prog, "bind", socket_name, std::strerror(error));
		return -1;
	}
	chmod(socket_name, 0700);
	return socket_fd;
}

int
connect_service_manager_socket(
	const bool is_system,
	const char * prog
) {
	std::string name_buf;
	const char * const socket_name(construct_service_manager_socket_name(is_system, name_buf));
	const int socket_fd(socket(AF_UNIX, SOCK_DGRAM, 0));
	if (0 > socket_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "socket", std::strerror(error));
		return socket_fd;
	}
	sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_name , sizeof addr.sun_path);
	if (0 > socket_connect(socket_fd, &addr, sizeof addr)) {
		const int error(errno);
		close(socket_fd);
		std::fprintf(stderr, "%s: ERROR: %s: %s: %s\n", prog, "connect", socket_name, std::strerror(error));
		return -1;
	}
	return socket_fd;
}
