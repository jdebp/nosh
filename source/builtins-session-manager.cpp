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

// These are the built-in commands visible in the session-manager and system-control utililties.

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
void reset ( const char * & , std::vector<const char *> & ) ;
void enable ( const char * & , std::vector<const char *> & ) ;
void disable ( const char * & , std::vector<const char *> & ) ;
void preset ( const char * & , std::vector<const char *> & ) ;
void init ( const char * & , std::vector<const char *> & ) ;
void show ( const char * & , std::vector<const char *> & ) ;
void show_json ( const char * & , std::vector<const char *> & ) ;
void status ( const char * & , std::vector<const char *> & ) ;
void try_restart ( const char * & , std::vector<const char *> & ) ;
void is_active ( const char * & , std::vector<const char *> & ) ;
void convert_systemd_units ( const char * & , std::vector<const char *> & ) ;
void convert_systemd_presets ( const char * & , std::vector<const char *> & ) ;
void convert_rcconf_presets ( const char * & , std::vector<const char *> & ) ;
void nagios_check ( const char * & , std::vector<const char *> & ) ;
void cyclog ( const char * & , std::vector<const char *> & ) ;
void system_control ( const char * & , std::vector<const char *> & ) ;
void system_manager ( const char * & , std::vector<const char *> & ) ;
void service_manager ( const char * & , std::vector<const char *> & ) ;
void setsid ( const char * & , std::vector<const char *> & ) ;
void system_version ( const char * & , std::vector<const char *> & ) ;

extern const
struct command 
commands[] = {
	{	"session-manager",	system_manager		},
	{	"systemctl",		system_control		},
	{	"initctl",		system_control		},
	{	"version",		system_version		},

	// These are the system-control subcommands.
	{	"init",				init			},
	{	"reboot",			reboot			},
	{	"halt",				halt			},
	{	"poweroff",			poweroff		},
	{	"fastboot",			reboot			},
	{	"fastreboot",			reboot			},
	{	"fasthalt",			halt			},
	{	"fastpoweroff",			poweroff		},
	{	"emergency",			emergency		},
	{	"rescue",			rescue			},
	{	"single",			rescue			},
	{	"normal",			normal			},
	{	"default",			normal			},
	{	"sysinit",			sysinit			},
	{	"cyclog",			cyclog			},
	{	"activate",			activate		},
	{	"start",			activate		},
	{	"deactivate",			deactivate		},
	{	"stop",				deactivate		},
	{	"isolate",			isolate			},
	{	"reset",			reset			},
	{	"enable",			enable			},
	{	"disable",			disable			},
	{	"preset",			preset			},
	{	"show",				show			},
	{	"show-json",			show_json		},
	{	"status",			status			},
	{	"try-restart",			try_restart		},
	{	"condrestart",			try_restart		},
	{	"force-reload",			try_restart		},
	{	"is-active",			is_active		},
	{	"convert-systemd-units",	convert_systemd_units	},
	{	"convert-systemd-presets",	convert_systemd_presets	},
	{	"convert-rcconf-presets",	convert_rcconf_presets	},
	{	"nagios-check-service",		nagios_check		},

	// These are spawned by session-manager.
	{	"service-manager",	service_manager		},
	{	"system-control",	system_control		},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;
