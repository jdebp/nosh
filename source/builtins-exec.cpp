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

extern void appendpath ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void background ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void chdir ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void chroot ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void clearenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void command_exec ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_flat_table_viewer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_docbook_xml_viewer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void create_control_group ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void cyclog ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void delegate_control_group_to ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void envdir ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void envgid ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void envuidgid ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void export_to_rsyslog ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void false_command ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void fdmove ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void fdredir ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void fifo_listen ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void find_default_jvm ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void find_matching_jvm ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void follow_log_directories ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void foreground ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void getuidgid ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void hardlimit ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void klog_read ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void line_banner ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void local_datagram_socket_listen ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void local_reaper ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void local_seqpacket_socket_accept ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void local_seqpacket_socket_listen ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void local_stream_socket_accept ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void local_stream_socket_connect ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void local_stream_socket_listen ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void machineenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void make_private_fs ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void make_read_only_fs ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void monitor_fsck_progress ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void monitored_fsck ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void move_to_control_group ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void nosh ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void oom_kill_protect ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void pause ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void pipe ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void plug_and_play_event_handler ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void prependpath ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void printenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void read_conf ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void recordio ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void set_control_group_knob ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void set_dynamic_hostname ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void set_mount_object ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setgid_fromenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setlock ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setlogin ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setpgrp ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setsid ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setuidgid ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setuidgid_fromenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setup_machine_id ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void erase_machine_id ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void softlimit ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void syslog_read ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void tai64n ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void tai64nlocal ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void tcp_socket_accept ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void tcp_socket_connect ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void tcp_socket_listen ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void tcpserver ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void time_env_add ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void time_env_set ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void time_env_set_if_earlier ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void time_env_unset_if_later ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void time_pause_until ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void time_print_tai64n ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void true_command ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void ucspi_socket_rules_check ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void udp_socket_connect ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void udp_socket_listen ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void ulimit ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void umask ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void unsetenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void unshare ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void userenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void userenv_fromenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void ifconfig ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
#if defined(__LINUX__) || defined(__linux__)
extern void netlink_datagram_socket_listen ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
#endif

extern const
struct command 
commands[] = {
	{	"exec",					command_exec			},
	{	"nosh",					nosh				},

	// Chain-loading non-terminals
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
	{	"printenv",				printenv			},
	{	"userenv",				userenv				},
	{	"userenv-fromenv",			userenv_fromenv			},
	{	"machineenv",				machineenv			},
	{	"ulimit",				ulimit				},
	{	"hardlimit",				hardlimit			},
	{	"softlimit",				softlimit			},
	{	"setlogin",				setlogin			},
	{	"envgid",				envgid				},
	{	"envuidgid",				envuidgid			},
	{	"getuidgid",				getuidgid			},
	{	"setuidgid",				setuidgid			},
	{	"setuidgid-fromenv",			setuidgid_fromenv		},
	{	"setgid-fromenv",			setgid_fromenv			},
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
	{	"unshare",				unshare				},
	{	"set-mount-object",			set_mount_object		},
	{	"make-private-fs",			make_private_fs			},
	{	"make-read-only-fs",			make_read_only_fs		},
	{	"tcpserver",				tcpserver			},
	{	"fifo-listen",				fifo_listen			},
	{	"tcp-socket-listen",			tcp_socket_listen		},
	{	"udp-socket-listen",			udp_socket_listen		},
	{	"local-datagram-socket-listen",		local_datagram_socket_listen	},
	{	"local-stream-socket-listen",		local_stream_socket_listen	},
	{	"local-seqpacket-socket-listen",	local_seqpacket_socket_listen	},
#if defined(__LINUX__) || defined(__linux__)
	{	"netlink-datagram-socket-listen",	netlink_datagram_socket_listen	},
#endif
	{	"tcp-socket-accept",			tcp_socket_accept		},
	{	"local-stream-socket-accept",		local_stream_socket_accept	},
	{	"local-seqpacket-socket-accept",	local_seqpacket_socket_accept	},
	{	"ucspi-socket-rules-check",		ucspi_socket_rules_check	},
	{	"local-stream-socket-connect",		local_stream_socket_connect	},
	{	"tcp-socket-connect",			tcp_socket_connect		},
	{	"udp-socket-connect",			udp_socket_connect		},
	{	"monitored-fsck",			monitored_fsck			},
	{	"plug-and-play-event-handler",		plug_and_play_event_handler	},
	{	"oom-kill-protect",			oom_kill_protect		},
	{	"local-reaper",				local_reaper			},
	{	"create-control-group",			create_control_group		},
	{	"move-to-control-group",		move_to_control_group		},
	{	"delegate-control-group-to",		delegate_control_group_to	},
	{	"set-control-group-knob",		set_control_group_knob		},
	{	"find-default-jvm",		        find_default_jvm		},
	{	"find-matching-jvm",		        find_matching_jvm		},
	{	"time-env-set",				time_env_set			},
	{	"time-env-set-if-earlier",		time_env_set_if_earlier		},
	{	"time-env-unset-if-later",		time_env_unset_if_later 	},
	{	"time-env-add",				time_env_add			},
	{	"time-pause-until",			time_pause_until		},

	// Terminals
	{	"pause",				pause				},
	{	"tai64n",				tai64n				},
	{	"tai64nlocal",				tai64nlocal			},
	{	"set-dynamic-hostname",			set_dynamic_hostname		},
	{	"setup-machine-id",			setup_machine_id		},
	{	"erase-machine-id",			erase_machine_id		},
	{	"export-to-rsyslog",			export_to_rsyslog		},
	{	"follow-log-directories",		follow_log_directories		},
	{	"syslog-read",				syslog_read			},
	{	"klog-read",				klog_read			},
	{	"cyclog",				cyclog				},
	{	"monitor-fsck-progress",		monitor_fsck_progress		},
	{	"console-flat-table-viewer",		console_flat_table_viewer	},
	{	"console-docbook-xml-viewer",		console_docbook_xml_viewer	},
	{	"time-print-tai64n",			time_print_tai64n		},
	{	"ifconfig",				ifconfig			},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;

extern const
struct command 
personalities[] = {
};
const std::size_t num_personalities = sizeof personalities/sizeof *personalities;
