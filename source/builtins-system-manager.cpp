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

void reboot ( const char * & , std::vector<const char *> & ) ;
void halt ( const char * & , std::vector<const char *> & ) ;
void poweroff ( const char * & , std::vector<const char *> & ) ;
void emergency ( const char * & , std::vector<const char *> & ) ;
void rescue ( const char * & , std::vector<const char *> & ) ;
void normal ( const char * & , std::vector<const char *> & ) ;
void sysinit ( const char * & , std::vector<const char *> & ) ;
void activate ( const char * & , std::vector<const char *> & ) ;
void deactivate ( const char * & , std::vector<const char *> & ) ;
void isolate ( const char * & , std::vector<const char *> & ) ;
void isolate ( const char * & , std::vector<const char *> & ) ;
void enable ( const char * & , std::vector<const char *> & ) ;
void disable ( const char * & , std::vector<const char *> & ) ;
void init ( const char * & , std::vector<const char *> & ) ;
void show ( const char * & , std::vector<const char *> & ) ;
void status ( const char * & , std::vector<const char *> & ) ;
void try_restart ( const char * & , std::vector<const char *> & ) ;
void convert_systemd_units ( const char * & , std::vector<const char *> & ) ;
void cyclog ( const char * & , std::vector<const char *> & ) ;
void system_control ( const char * & , std::vector<const char *> & ) ;
void system_manager ( const char * & , std::vector<const char *> & ) ;
void service_manager ( const char * & , std::vector<const char *> & ) ;
void setsid ( const char * & , std::vector<const char *> & ) ;

extern const
struct command 
commands[] = {
	{	"system-manager",	system_manager		},
	{	"systemd",		system_manager		},

	// These are the system-control subcommands.
	{	"init",			init			},
	{	"reboot",		reboot			},
	{	"halt",			halt			},
	{	"poweroff",		poweroff		},
	{	"fastboot",		reboot			},
	{	"fastreboot",		reboot			},
	{	"fasthalt",		halt			},
	{	"fastpoweroff",		poweroff		},
	{	"emergency",		emergency		},
	{	"rescue",		rescue			},
	{	"single",		rescue			},
	{	"normal",		normal			},
	{	"default",		normal			},
	{	"sysinit",		sysinit			},
	{	"cyclog",		cyclog			},
	{	"activate",		activate		},
	{	"start",		activate		},
	{	"deactivate",		deactivate		},
	{	"stop",			deactivate		},
	{	"isolate",		isolate			},
	{	"enable",		enable			},
	{	"load",			enable			},
	{	"disable",		disable			},
	{	"unload",		disable			},
	{	"init",			init			},
	{	"show",			show			},
	{	"status",		status			},
	{	"try-restart",		try_restart		},
	{	"preset",		convert_systemd_units	},
	{	"convert-systemd-units",convert_systemd_units	},

	// These are spawned by system-manager.
	{	"cyclog",		cyclog			},
	{	"system-control",	system_control		},
	{	"systemctl",		system_control		},
	{	"service-manager",	service_manager		},

	// These are used by debugging.
	{	"setsid",		setsid			},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;
