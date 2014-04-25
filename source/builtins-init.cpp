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

void reboot_poweroff_halt_command ( const char * & , std::vector<const char *> & ) ;
void wrap_system_control_subcommand ( const char * & , std::vector<const char *> & ) ;
void telinit ( const char * & , std::vector<const char *> & ) ;
void init ( const char * & , std::vector<const char *> & ) ;
void service ( const char * & , std::vector<const char *> & ) ;
void chkconfig ( const char * & , std::vector<const char *> & ) ;
void invoke_rcd ( const char * & , std::vector<const char *> & ) ;
void update_rcd ( const char * & , std::vector<const char *> & ) ;

extern const
struct command 
commands[] = {
	{	"reboot",		reboot_poweroff_halt_command		},
	{	"halt",			reboot_poweroff_halt_command		},
	{	"poweroff",		reboot_poweroff_halt_command		},
	{	"fastboot",		reboot_poweroff_halt_command		},
	{	"fastreboot",		reboot_poweroff_halt_command		},
	{	"fasthalt",		reboot_poweroff_halt_command		},
	{	"fastpoweroff",		reboot_poweroff_halt_command		},
	{	"emergency",		wrap_system_control_subcommand		},
	{	"rescue",		wrap_system_control_subcommand		},
	{	"normal",		wrap_system_control_subcommand		},
	{	"telinit",		telinit					},
	{	"init",			init					},
	{	"service",		service,				},
	{	"chkconfig",		chkconfig,				},
	{	"invoke-rc.d",		invoke_rcd,				},
	{	"update-rc.d",		update_rcd,				},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;
