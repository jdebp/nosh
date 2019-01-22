/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <grp.h>
#include "utils.h"
#include "fdutils.h"
#include "ProcessEnvironment.h"
#include "popt.h"
#include "listen.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
tcp_socket_listen ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	unsigned long backlog = 5U;
	bool no_reuse_address(false);
	bool reuse_port(false);
	bool bind_to_any(false);
	bool numeric_host(false);
	bool numeric_service(false);
	bool check_interfaces(false);
#if defined(IPV6_V6ONLY)
	bool combine4and6(false);
#endif
	bool systemd_compatibility(false), upstart_compatibility(false);
	try {
		popt::bool_definition no_reuse_address_option('\0', "no-reuse-address", "Disallow rapid re-use of a local IP address and port.", no_reuse_address);
		popt::bool_definition reuse_port_option('\0', "reuse-port", "Allow multiple listening sockets to share a single local IP address and port.", reuse_port);
		popt::bool_definition bind_to_any_option('\0', "bind-to-any", "Allow binding to any IP address even if it is not on any network interface.", bind_to_any);
		popt::bool_definition numeric_host_option('H', "numeric-host", "Assume that the host is an IP address.", numeric_host);
		popt::bool_definition numeric_service_option('\0', "numeric-service", "Assume that the service is a port number.", numeric_service);
		popt::bool_definition check_interfaces_option('\0', "check-interfaces", "Disallow IPv4 or IPv6 if no interface supports them.", check_interfaces);
#if defined(IPV6_V6ONLY)
		popt::bool_definition combine4and6_option('\0', "combine4and6", "Allow IPv6 sockets to talk IPv4 to mapped addresses.", combine4and6);
#endif
		popt::bool_definition upstart_compatibility_option('\0', "upstart-compatibility", "Set the $UPSTART_FDS and $UPSTART_EVENT environment variables for compatibility with upstart.", upstart_compatibility);
		popt::bool_definition systemd_compatibility_option('\0', "systemd-compatibility", "Set the $LISTEN_FDS and $LISTEN_PID environment variables for compatibility with systemd.", systemd_compatibility);
		popt::unsigned_number_definition backlog_option('b', "backlog", "number", "Specify the listening backlog.", backlog, 0);
		popt::definition * top_table[] = {
			&no_reuse_address_option,
			&reuse_port_option,
			&bind_to_any_option,
			&numeric_host_option,
			&numeric_service_option,
			&check_interfaces_option,
#if defined(IPV6_V6ONLY)
			&combine4and6_option,
#endif
			&upstart_compatibility_option,
			&systemd_compatibility_option,
			&backlog_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{localhost} {localport} {prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing listen host name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * listenhost(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing listen port number.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * listenport(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing next program.");
		throw static_cast<int>(EXIT_USAGE);
	}
	next_prog = arg0_of(args);

	addrinfo * info(0), hints = {0,0,0,0,0,0,0,0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_flags = AI_PASSIVE;
	if (numeric_host) hints.ai_flags |= AI_NUMERICHOST;
	if (numeric_service) hints.ai_flags |= AI_NUMERICSERV;
	if (check_interfaces) hints.ai_flags |= AI_ADDRCONFIG;
	const int rc(getaddrinfo(listenhost, listenport, &hints, &info));
	if (0 != rc) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s %s: %s\n", prog, listenhost, listenport, EAI_SYSTEM == rc ? std::strerror(error) : gai_strerror(rc));
		throw EXIT_FAILURE;
	}

	const int s(socket(info->ai_family, SOCK_STREAM, 0));
	if (0 > s) {
exit_error:
		const int error(errno);
		if (info) freeaddrinfo(info);
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (0 > socket_set_boolean_option(s, SOL_SOCKET, SO_REUSEADDR, !no_reuse_address)) goto exit_error;
#if defined(SO_REUSEPORT)
	if (0 > socket_set_boolean_option(s, SOL_SOCKET, SO_REUSEPORT, reuse_port)) goto exit_error;
#endif
	if (0 > socket_set_bind_to_any(s, *info, bind_to_any)) goto exit_error;
	if (AF_INET6 == info->ai_family) {
#if defined(IPV6_V6ONLY)
#	if defined(__OpenBSD__)
		if (combine4and6)
#	endif
		if (0 > socket_set_boolean_option(s, IPPROTO_IPV6, IPV6_V6ONLY, !combine4and6)) goto exit_error;
#endif
	}
	if (0 > bind(s, info->ai_addr, info->ai_addrlen)) goto exit_error;
	if (0 > listen(s, backlog)) goto exit_error;

	const int fd_index(systemd_compatibility ? query_listen_fds_passthrough(envs) : 0);
	if (LISTEN_SOCKET_FILENO + fd_index != s) {
		if (0 > dup2(s, LISTEN_SOCKET_FILENO + fd_index)) goto exit_error;
		close(s);
	}
	set_close_on_exec(LISTEN_SOCKET_FILENO + fd_index, false);

	if (upstart_compatibility) {
		envs.set("UPSTART_EVENTS", "socket");
		char fd[64];
		snprintf(fd, sizeof fd, "%u", LISTEN_SOCKET_FILENO + fd_index);
		envs.set("UPSTART_FDS", fd);
	}
	if (systemd_compatibility) {
		char buf[64];
		snprintf(buf, sizeof buf, "%d", fd_index + 1);
		envs.set("LISTEN_FDS", buf);
		snprintf(buf, sizeof buf, "%u", getpid());
		envs.set("LISTEN_PID", buf);
	}
}
