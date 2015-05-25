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
extern void syslog_read ( const char * &, std::vector<const char *> & );
extern void klog_read ( const char * &, std::vector<const char *> & );
extern void fifo_listen ( const char * &, std::vector<const char *> & );
extern void cyclog ( const char * &, std::vector<const char *> & );
extern void tai64n ( const char * &, std::vector<const char *> & );
extern void tai64nlocal ( const char * &, std::vector<const char *> & );

extern const
struct command 
commands[] = {
	{	"exec",				command_exec			},
	{	"nosh",				nosh				},
	{	"cd",				chdir				},
	{	"chdir",			chdir				},
	{	"chroot",			chroot				},
	{	"umask",			umask				},
	{	"setsid",			setsid				},
	{	"setpgrp",			setpgrp				},
	{	"setenv",			setenv				},
	{	"unsetenv",			unsetenv			},
	{	"envdir",			envdir				},
	{	"clearenv",			clearenv			},
	{	"userenv",			userenv				},
	{	"tcp-socket-accept",		tcp_socket_accept		},
	{	"tcp-socket-listen",		tcp_socket_listen		},
	{	"udp-socket-listen",		udp_socket_listen		},
	{	"ulimit",			ulimit				},
	{	"softlimit",			softlimit			},
	{	"envuidgid",			envuidgid			},
	{	"setuidgid",			setuidgid			},
	{	"setuidgid-fromenv",		setuidgid_fromenv		},
	{	"line-banner",			line_banner			},
	{	"fdmove",			fdmove				},
	{	"fdredir",			fdredir				},
	{	"read-conf",			read_conf			},
	{	"setlock",			setlock				},
	{	"local-datagram-socket-listen",	local_datagram_socket_listen	},
	{	"local-stream-socket-listen",	local_stream_socket_listen	},
	{	"local-stream-socket-accept",	local_stream_socket_accept	},
	{	"appendpath",			appendpath			},
	{	"prependpath",			prependpath			},
	{	"recordio",			recordio			},
	{	"true",				true_command			},
	{	"false",			false_command			},
	{	"set-dynamic-hostname",		set_dynamic_hostname		},
	{	"setup-machine-id",		setup_machine_id		},
	{	"pipe",				pipe				},
	{	"foreground",			foreground			},
	{	"background",			background			},
	{	"ucspi-socket-rules-check",	ucspi_socket_rules_check	},
	{	"pause",			pause				},
	{	"unshare",			unshare				},
	{	"set-mount-object",		set_mount_object		},
	{	"make-private-fs",		make_private_fs			},
	{	"syslog-read",			syslog_read			},
	{	"klog-read",			klog_read			},
	{	"fifo-listen",			fifo_listen			},
	{	"cyclog",			cyclog				},
	{	"tai64n",			tai64n				},
	{	"tai64nlocal",			tai64nlocal			},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;

// There are no extra personalities over and above the built-in commands.
extern const
struct command 
personalities[] = {
	{	0,			0,			},
};
const std::size_t num_personalities = 0;
