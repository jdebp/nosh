/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <climits>
#include <csignal>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#if defined(__LINUX__) || defined(__linux__)
#include <linux/netlink.h>
#else
#include <sys/un.h>
#endif
#include <unistd.h>
#include <grp.h>
#include "utils.h"
#include "fdutils.h"
#include "ProcessEnvironment.h"
#include "popt.h"
#include "listen.h"

#if defined(__LINUX__) || defined(__linux__)
static const
struct Protocol {
	const char * name;
	int prot;
} protocols[] = {
	{ "route",		NETLINK_ROUTE		},
	{ "usersock",		NETLINK_USERSOCK	},
	{ "firewall",		NETLINK_FIREWALL	},
	{ "inet-diag",		NETLINK_INET_DIAG	},
	{ "nflog",		NETLINK_NFLOG		},
	{ "xfrm",		NETLINK_XFRM		},
	{ "selinux",		NETLINK_SELINUX		},
	{ "iscsi",		NETLINK_ISCSI		},
	{ "audit",		NETLINK_AUDIT		},
	{ "fib-lookup",		NETLINK_FIB_LOOKUP	},	
	{ "connector",		NETLINK_CONNECTOR	},
	{ "netfilter",		NETLINK_NETFILTER	},
	{ "ip6-fw",		NETLINK_IP6_FW		},
	{ "dnrtmsg",		NETLINK_DNRTMSG		},
	{ "kobject-uevent",	NETLINK_KOBJECT_UEVENT	},
	{ "generic",		NETLINK_GENERIC		},
	{ "scsitransport",	NETLINK_SCSITRANSPORT	},	
	{ "ecryptfs",		NETLINK_ECRYPTFS	},
	{ "rdma",		NETLINK_RDMA		},
	{ "crypto",		NETLINK_CRYPTO		},
};

static inline
int
lookup_netlink_protocol (
	const char * protocol
) {
	for (std::size_t i(0U); i < sizeof protocols/sizeof *protocols; ++i) {
		if (0 == std::strcmp(protocol, protocols[i].name))
			return protocols[i].prot;
	}
	return -1;
}
#endif

/* Main function ************************************************************
// **************************************************************************
*/

void
netlink_datagram_socket_listen ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	unsigned long receive_buffer_size(0U);
	bool systemd_compatibility(false), upstart_compatibility(false), pass_credentials(false), pass_security(false);
	bool raw(false), has_rcvbuf(false);
	try {
		popt::bool_definition upstart_compatibility_option('\0', "upstart-compatibility", "Set the $UPSTART_FDS and $UPSTART_EVENT environment variables for compatibility with upstart.", upstart_compatibility);
		popt::bool_definition systemd_compatibility_option('\0', "systemd-compatibility", "Set the $LISTEN_FDS and $LISTEN_PID environment variables for compatibility with systemd.", systemd_compatibility);
		popt::bool_definition pass_credentials_option('\0', "pass-credentials", "Specify that credentials can be passed over the socket.", pass_credentials);
		popt::bool_definition pass_security_option('\0', "pass-security", "Specify that security can be passed over the socket.", pass_security);
		popt::bool_definition raw_option('\0', "raw", "Specify that the socket be of the RAW type.", raw);
		popt::unsigned_number_definition receive_buffer_size_option('\0', "receive-buffer-size", "bytes", "Specify the listening receive buffer size.", receive_buffer_size, 0);
		popt::definition * top_table[] = {
			&upstart_compatibility_option,
			&systemd_compatibility_option,
			&pass_credentials_option,
			&pass_security_option,
			&raw_option,
			&receive_buffer_size_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{protocol} {group} {prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		has_rcvbuf = receive_buffer_size_option.is_set();
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing protocol name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * protocol(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing group number.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * group(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	const char * end(group);
	const long groups(std::strtol(group, const_cast<char **>(&end), 0));
	if (end == group || *end) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, group, "Bad group number.");
		throw EXIT_FAILURE;
	}

#if defined(__LINUX__) || defined(__linux__)
	sockaddr_nl addr;
	addr.nl_family = AF_NETLINK;
	addr.nl_groups = groups;
	addr.nl_pid = getpid();
	addr.nl_pad = 0;

	const int s(socket(AF_NETLINK, raw ? SOCK_RAW : SOCK_DGRAM, lookup_netlink_protocol(protocol)));
#else
	sockaddr_un addr;
	addr.sun_family = AF_UNSPEC;
	errno = EAFNOSUPPORT;
	const int s(-1);
#endif
	if (0 > s) {
exit_error:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (0 > bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof addr)) goto exit_error;
#if defined(SO_PASSCRED)
	if (pass_credentials) {
		int one(1);
		if (0 > setsockopt(s, SOL_SOCKET, SO_PASSCRED, &one, sizeof one)) goto exit_error;
	}
#endif
#if defined(SO_PASSSEC)
	if (pass_security) {
		int one(1);
		if (0 > setsockopt(s, SOL_SOCKET, SO_PASSSEC, &one, sizeof one)) goto exit_error;
	}
#endif
#if defined(SO_RCVBUF)
	if (has_rcvbuf) {
		const int size(receive_buffer_size);
		if (true
#if defined(SO_RCVBUFFORCE)
		&&  0 > setsockopt(s, SOL_SOCKET, SO_RCVBUFFORCE, &size, sizeof size)
#endif
		&&  0 > setsockopt(s, SOL_SOCKET, SO_RCVBUF, &size, sizeof size)
		) 
			goto exit_error;
	}
#endif

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
