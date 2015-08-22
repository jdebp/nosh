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
#include <linux/netlink.h>
#include <unistd.h>
#include <grp.h>
#include "utils.h"
#include "popt.h"
#include "listen.h"

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

/* Main function ************************************************************
// **************************************************************************
*/

void
netlink_datagram_socket_listen ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	unsigned long receive_buffer_size(0U);
	bool systemd_compatibility(false), pass_credentials(false), pass_security(false);
	bool raw(false), has_rcvbuf(false);
	try {
		popt::bool_definition systemd_compatibility_option('\0', "systemd-compatibility", "Set the $LISTEN_FDS and $LISTEN_PID environment variables for compatibility with systemd.", systemd_compatibility);
		popt::bool_definition pass_credentials_option('\0', "pass-credentials", "Specify that credentials can be passed over the socket.", pass_credentials);
		popt::bool_definition pass_security_option('\0', "pass-security", "Specify that security can be passed over the socket.", pass_security);
		popt::bool_definition raw_option('\0', "raw", "Specify that the socket be of the RAW type.", raw);
		popt::unsigned_number_definition receive_buffer_size_option('\0', "receive-buffer-size", "bytes", "Specify the listening receive buffer size.", receive_buffer_size, 0);
		popt::definition * top_table[] = {
			&systemd_compatibility_option,
			&pass_credentials_option,
			&pass_security_option,
			&raw_option,
			&receive_buffer_size_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "protocol group prog");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		has_rcvbuf = receive_buffer_size_option.is_set();
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing protocol name.");
		throw EXIT_FAILURE;
	}
	const char * protocol(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing group number.");
		throw EXIT_FAILURE;
	}
	const char * group(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	// FIXME: Can we even get a SIGPIPE from listen()?
	sigset_t original_signals;
	sigprocmask(SIG_SETMASK, 0, &original_signals);
	sigset_t masked_signals(original_signals);
	sigaddset(&masked_signals, SIGPIPE);
	sigprocmask(SIG_SETMASK, &masked_signals, 0);

	sockaddr_nl addr;
	addr.nl_family = AF_NETLINK;
	addr.nl_groups = -1U;
	addr.nl_pid = getpid();
	addr.nl_pad = 0;

	const int s(socket(AF_NETLINK, raw ? SOCK_RAW : SOCK_DGRAM, lookup_netlink_protocol(protocol)));
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
	{
		const int size(receive_buffer_size);
		if (0 > setsockopt(s, SOL_SOCKET, SO_RCVBUFFORCE, &size, sizeof size)
		&&  0 > setsockopt(s, SOL_SOCKET, SO_RCVBUF, &size, sizeof size)
		) 
			goto exit_error;
	}
#endif

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