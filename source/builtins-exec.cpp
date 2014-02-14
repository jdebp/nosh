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
extern void vc_get_tty ( const char * &, std::vector<const char *> & );
extern void vc_reset_tty ( const char * &, std::vector<const char *> & );
extern void open_controlling_tty ( const char * &, std::vector<const char *> & );
extern void pty_get_tty ( const char * &, std::vector<const char *> & );
extern void pty_run ( const char * &, std::vector<const char *> & );
extern void login_banner ( const char * &, std::vector<const char *> & );
extern void login_process ( const char * &, std::vector<const char *> & );
extern void login_prompt ( const char * &, std::vector<const char *> & );
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
extern void line_banner ( const char * &, std::vector<const char *> & );
extern void fdmove ( const char * &, std::vector<const char *> & );
extern void fdredir ( const char * &, std::vector<const char *> & );
extern void read_conf ( const char * &, std::vector<const char *> & );
extern void setlock ( const char * &, std::vector<const char *> & );
extern void local_socket_listen ( const char * &, std::vector<const char *> & );
extern void local_socket_accept ( const char * &, std::vector<const char *> & );
extern void appendpath ( const char * &, std::vector<const char *> & );
extern void prependpath ( const char * &, std::vector<const char *> & );
extern void recordio ( const char * &, std::vector<const char *> & );
extern void true_command ( const char * &, std::vector<const char *> & );
extern void false_command ( const char * &, std::vector<const char *> & );
extern void set_dynamic_hostname ( const char * &, std::vector<const char *> & );
extern void setup_machine_id ( const char * &, std::vector<const char *> & );
extern void pipe ( const char * &, std::vector<const char *> & );
extern void ucspi_socket_rules_check ( const char * &, std::vector<const char *> & );
extern void pause ( const char * &, std::vector<const char *> & );

extern const
struct command 
commands[] = {
	{	"exec",				command_exec			},
	{	"nosh",				nosh				},
	{	"cd",				chdir				},
	{	"chdir",			chdir				},
	{	"chroot",			chroot				},
	{	"umask",			umask				},
	{	"vc-get-tty",			vc_get_tty			},
	{	"vc-reset-tty",			vc_reset_tty			},
	{	"open-controlling-tty",		open_controlling_tty		},
	{	"login-banner",			login_banner			},
	{	"login-process",		login_process			},
	{	"login-prompt",			login_prompt			},
	{	"pty-get-tty",			pty_get_tty			},
	{	"pty-run",			pty_run				},
	{	"setsid",			setsid				},
	{	"setpgrp",			setpgrp				},
	{	"setenv",			setenv				},
	{	"unsetenv",			unsetenv			},
	{	"envdir",			envdir				},
	{	"clearenv",			clearenv			},
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
	{	"local-socket-accept",		local_socket_accept		},
	{	"local-socket-listen",		local_socket_listen		},
	{	"appendpath",			appendpath			},
	{	"prependpath",			prependpath			},
	{	"recordio",			recordio			},
	{	"true",				true_command			},
	{	"false",			false_command			},
	{	"set-dynamic-hostname",		set_dynamic_hostname		},
	{	"setup-machine-id",		setup_machine_id		},
	{	"pipe",				pipe				},
	{	"ucspi-socket-rules-check",	ucspi_socket_rules_check	},
	{	"pause",			pause				},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;
