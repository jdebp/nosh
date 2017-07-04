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

// These are the built-in commands visible in the statically linked system-manager.

extern void reboot_poweroff_halt_command ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void emergency_rescue_normal_command ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void isolate ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void activate ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void cyclog ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void foreground ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void system_control ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void init ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void system_manager ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void service_manager ( const char * & , std::vector<const char *> &, ProcessEnvironment & ) ;
extern void move_to_control_group ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void set_control_group_knob ( const char * &, std::vector<const char *> &, ProcessEnvironment & );

extern const
struct command 
commands[] = {
	// These are spawned by system-manager.
	{	"service-manager",		service_manager		},
	{	"system-control",		system_control		},
	{	"cyclog",			cyclog			},
	{	"move-to-control-group",	move_to_control_group	},
	{	"foreground",			foreground		},
	{	"set-control-group-knob",	set_control_group_knob	},

	// These are the system-control subcommands used by system-manager.
	{	"init",			init			},
	{	"isolate",		isolate			},
	{	"activate",		activate		},
	{	"start",		activate		},
	{	"emergency",		emergency_rescue_normal_command	},
	{	"rescue",		emergency_rescue_normal_command	},
	{	"normal",		emergency_rescue_normal_command	},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;

extern const
struct command 
personalities[] = {
	{	"system-manager",	system_manager		},
	{	"init",			system_manager,		},
	{	"systemd",		system_manager		},
};
const std::size_t num_personalities = sizeof personalities/sizeof *personalities;
