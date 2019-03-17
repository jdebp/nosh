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

// These are the built-in commands visible in the BSD/SystemV compatibility utilities.

extern void reboot_poweroff_halt_command ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void rcctl ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void wrap_system_control_subcommand ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void shutdown ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void telinit ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void runlevel ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void service ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void chkconfig ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void initctl ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void svcadm ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void invoke_rcd ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void update_rcd ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void rc_update ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void initctl_read ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void foreground ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;

extern const
struct command 
commands[] = {
	// These are personalities that are also available as built-in commands.
	{	"shutdown",		shutdown				},
	{	"reboot",		reboot_poweroff_halt_command		},
	{	"halt",			reboot_poweroff_halt_command		},
	{	"haltsys",		reboot_poweroff_halt_command		},
	{	"poweroff",		reboot_poweroff_halt_command		},
	{	"powercycle",		reboot_poweroff_halt_command		},
	{	"fastboot",		reboot_poweroff_halt_command		},
	{	"fastreboot",		reboot_poweroff_halt_command		},
	{	"fasthalt",		reboot_poweroff_halt_command		},
	{	"fastpoweroff",		reboot_poweroff_halt_command		},
	{	"fastpowercycle",	reboot_poweroff_halt_command		},
	{	"emergency",		wrap_system_control_subcommand		},
	{	"rescue",		wrap_system_control_subcommand		},
	{	"normal",		wrap_system_control_subcommand		},
	{	"telinit",		telinit					},
	{	"runlevel",		runlevel				},
	{	"init",			telinit					},
	{	"service",		service					},
	{	"chkconfig",		chkconfig				},
	{	"initctl",		initctl					},
	{	"svcadm",		svcadm					},
	{	"invoke-rc.d",		invoke_rcd				},
	{	"update-rc.d",		update_rcd				},
	{	"rc-update",		rc_update				},
	{	"rc-service",		service					},
	{	"rcctl",		rcctl					},
	{	"initctl-read",		initctl_read				},

	// These are spawned by other commands.
	{	"foreground",		foreground				},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;

// There are no extra personalities over and above the built-in commands.
extern const
struct command 
personalities[] = {
	{	0,			0,			},
};
const std::size_t num_personalities = 0;
