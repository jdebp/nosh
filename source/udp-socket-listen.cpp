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
#include <netdb.h>
#include <unistd.h>
#include <grp.h>
#include "utils.h"
#include "popt.h"
#include "listen.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
udp_socket_listen ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	bool no_reuse_address(false);
	bool numeric(false);
	bool check_interfaces(false);
#if defined(IPV6_V6ONLY)
	bool combine4and6(false);
#endif
	bool systemd_compatibility(false);
	try {
		popt::bool_definition no_reuse_address_option('\0', "no-reuse-address", "Disallow rapid re-use of a local IP address and port.", no_reuse_address);
		popt::bool_definition numeric_option('n', "numeric", "Save on name resolution and assume host and port are numeric.", numeric);
		popt::bool_definition check_interfaces_option('\0', "check-interfaces", "Disallow IPv4 or IPv6 if no interface supports them.", check_interfaces);
#if defined(IPV6_V6ONLY)
		popt::bool_definition combine4and6_option('\0', "combine4and6", "Allow IPv6 sockets to talk IPv4 to mapped addresses.", combine4and6);
#endif
		popt::bool_definition systemd_compatibility_option('\0', "systemd-compatibility", "Set the $LISTEN_FDS and $LISTEN_PID environment variables for compatibility with systemd.", systemd_compatibility);
		popt::definition * top_table[] = {
			&no_reuse_address_option,
			&numeric_option,
			&check_interfaces_option,
#if defined(IPV6_V6ONLY)
			&combine4and6_option,
#endif
			&systemd_compatibility_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "localhost localport prog");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing listen host name.");
		throw EXIT_FAILURE;
	}
	const char * listenhost(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing listen port number.");
		throw EXIT_FAILURE;
	}
	const char * listenport(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	// FIXME: Can we even get a SIGPIPE from listen()?
	sigset_t original_signals;
	sigprocmask(SIG_SETMASK, 0, &original_signals);
	sigset_t masked_signals(original_signals);
	sigaddset(&masked_signals, SIGPIPE);
	sigprocmask(SIG_SETMASK, &masked_signals, 0);

	addrinfo * info(0), hints = {0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = 0;
	hints.ai_flags = AI_PASSIVE;
	if (numeric) hints.ai_flags |= AI_NUMERICHOST|AI_NUMERICSERV;
	if (check_interfaces) hints.ai_flags |= AI_ADDRCONFIG;
	const int rc(getaddrinfo(listenhost, listenport, &hints, &info));
	if (0 != rc) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s %s: %s\n", prog, listenhost, listenport, EAI_SYSTEM == rc ? std::strerror(error) : gai_strerror(rc));
		throw EXIT_FAILURE;
	}

	const int s(socket(info->ai_family, SOCK_DGRAM, 0));
	if (0 > s) {
exit_error:
		const int error(errno);
		if (info) freeaddrinfo(info);
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
		throw EXIT_FAILURE;
	}
	const int reuse_addr_i(!no_reuse_address);
	if (0 > setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse_addr_i, sizeof reuse_addr_i)) goto exit_error;
	if (AF_INET6 == info->ai_family) {
#if defined(IPV6_V6ONLY)
		const int v6only(!combine4and6);
		if (0 > setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof v6only)) goto exit_error;
#endif
	}
	if (0 > bind(s, info->ai_addr, info->ai_addrlen)) goto exit_error;
	if (0 > listen(s, 128)) goto exit_error;

	if (0 > dup2(s, LISTEN_SOCKET_FILENO)) goto exit_error;
	if (LISTEN_SOCKET_FILENO != s) close(s);

	if (systemd_compatibility) {
		setenv("LISTEN_FDS", "1", 1);
		char pid[64];
		snprintf(pid, sizeof pid, "%u", getpid());
		setenv("LISTEN_PID", pid, 1);
	}

	sigprocmask(SIG_SETMASK, &original_signals, 0);
}
