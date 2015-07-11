/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstddef>
#include "utils.h"

/* Table of commands ********************************************************
// **************************************************************************
*/

// These are the built-in commands visible in nosh and exec.

extern void command_exec ( const char * &, std::vector<const char *> & );
extern void nosh ( const char * &, std::vector<const char *> & );
extern void chdir ( const char * &, std::vector<const char *> & );
extern void chroot ( const char * &, std::vector<const char *> & );
extern void umask ( const char * &, std::vector<const char *> & );
extern void setsid ( const char * &, std::vector<const char *> & );
extern void setpgrp ( const char * &, std::vector<const char *> & );
extern void setenv ( const char * &, std::vector<const char *> & );
extern void unsetenv ( const char * &, std::vector<const char *> & );
extern void envdir ( const char * &, std::vector<const char *> & );
extern void clearenv ( const char * &, std::vector<const char *> & );
extern void udp_socket_listen ( const char * &, std::vector<const char *> & );
extern void tcp_socket_listen ( const char * &, std::vector<const char *> & );
extern void tcp_socket_accept ( const char * &, std::vector<const char *> & );
extern void ulimit ( const char * &, std::vector<const char *> & );
extern void softlimit ( const char * &, std::vector<const char *> & );
extern void envuidgid ( const char * &, std::vector<const char *> & );
extern void setuidgid ( const char * &, std::vector<const char *> & );
extern void setuidgid_fromenv ( const char * &, std::vector<const char *> & );
extern void userenv ( const char * &, std::vector<const char *> & );
extern void line_banner ( const char * &, std::vector<const char *> & );
extern void fdmove ( const char * &, std::vector<const char *> & );
extern void fdredir ( const char * &, std::vector<const char *> & );
extern void read_conf ( const char * &, std::vector<const char *> & );
extern void setlock ( const char * &, std::vector<const char *> & );
extern void local_datagram_socket_listen ( const char * &, std::vector<const char *> & );
extern void local_stream_socket_listen ( const char * &, std::vector<const char *> & );
extern void local_stream_socket_accept ( const char * &, std::vector<const char *> & );
#if defined(__LINUX__) || defined(__linux__)
extern void netlink_datagram_socket_listen ( const char * &, std::vector<const char *> & );
#endif
extern void appendpath ( const char * &, std::vector<const char *> & );
extern void prependpath ( const char * &, std::vector<const char *> & );
extern void recordio ( const char * &, std::vector<const char *> & );
extern void true_command ( const char * &, std::vector<const char *> & );
extern void false_command ( const char * &, std::vector<const char *> & );
extern void set_dynamic_hostname ( const char * &, std::vector<const char *> & );
extern void setup_machine_id ( const char * &, std::vector<const char *> & );
extern void pipe ( const char * &, std::vector<const char *> & );
extern void foreground ( const char * &, std::vector<const char *> & );
extern void background ( const char * &, std::vector<const char *> & );
extern void ucspi_socket_rules_check ( const char * &, std::vector<const char *> & );
extern void pause ( const char * &, std::vector<const char *> & );
extern void unshare ( const char * &, std::vector<const char *> & );
extern void make_private_fs ( const char * &, std::vector<const char *> & );
extern void set_mount_object ( const char * &, std::vector<const char *> & );
extern void fifo_listen ( const char * &, std::vector<const char *> & );
extern void syslog_read ( const char * &, std::vector<const char *> & );
extern void klog_read ( const char * &, std::vector<const char *> & );
extern void cyclog ( const char * &, std::vector<const char *> & );
extern void tai64n ( const char * &, std::vector<const char *> & );
extern void tai64nlocal ( const char * &, std::vector<const char *> & );

extern const
struct command 
commands[] = {
	{	"exec",					command_exec			},
	{	"nosh",					nosh				},
	{	"cd",					chdir				},
	{	"chdir",				chdir				},
	{	"chroot",				chroot				},
	{	"umask",				umask				},
	{	"setsid",				setsid				},
	{	"setpgrp",				setpgrp				},
	{	"setenv",				setenv				},
	{	"unsetenv",				unsetenv			},
	{	"envdir",				envdir				},
	{	"clearenv",				clearenv			},
	{	"userenv",				userenv				},
	{	"ulimit",				ulimit				},
	{	"softlimit",				softlimit			},
	{	"envuidgid",				envuidgid			},
	{	"setuidgid",				setuidgid			},
	{	"setuidgid-fromenv",			setuidgid_fromenv		},
	{	"line-banner",				line_banner			},
	{	"fdmove",				fdmove				},
	{	"fdredir",				fdredir				},
	{	"read-conf",				read_conf			},
	{	"setlock",				setlock				},
	{	"appendpath",				appendpath			},
	{	"prependpath",				prependpath			},
	{	"recordio",				recordio			},
	{	"true",					true_command			},
	{	"false",				false_command			},
	{	"pipe",					pipe				},
	{	"foreground",				foreground			},
	{	"background",				background			},
	{	"pause",				pause				},
	{	"unshare",				unshare				},
	{	"set-mount-object",			set_mount_object		},
	{	"make-private-fs",			make_private_fs			},
	{	"fifo-listen",				fifo_listen			},
	{	"tcp-socket-listen",			tcp_socket_listen		},
	{	"udp-socket-listen",			udp_socket_listen		},
	{	"local-datagram-socket-listen",		local_datagram_socket_listen	},
	{	"local-stream-socket-listen",		local_stream_socket_listen	},
#if defined(__LINUX__) || defined(__linux__)
	{	"netlink-datagram-socket-listen",	netlink_datagram_socket_listen	},
#endif
	{	"ucspi-socket-rules-check",		ucspi_socket_rules_check	},
	{	"tai64n",				tai64n				},
	{	"tai64nlocal",				tai64nlocal			},
	{	"set-dynamic-hostname",			set_dynamic_hostname		},
	{	"setup-machine-id",			setup_machine_id		},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;

extern const
struct command 
personalities[] = {
	// These are not internal commands so that the output of ps looks prettier for long-running service processes that employ them.
	{	"tcp-socket-accept",			tcp_socket_accept		},
	{	"local-stream-socket-accept",		local_stream_socket_accept	},
	{	"syslog-read",				syslog_read			},
	{	"klog-read",				klog_read			},
	{	"cyclog",				cyclog				},
};
const std::size_t num_personalities = sizeof personalities/sizeof *personalities;
