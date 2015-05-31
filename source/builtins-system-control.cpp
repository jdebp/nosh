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

extern void reboot ( const char * & , std::vector<const char *> & ) ;
extern void halt ( const char * & , std::vector<const char *> & ) ;
extern void poweroff ( const char * & , std::vector<const char *> & ) ;
extern void emergency ( const char * & , std::vector<const char *> & ) ;
extern void rescue ( const char * & , std::vector<const char *> & ) ;
extern void normal ( const char * & , std::vector<const char *> & ) ;
extern void sysinit ( const char * & , std::vector<const char *> & ) ;
extern void activate ( const char * & , std::vector<const char *> & ) ;
extern void deactivate ( const char * & , std::vector<const char *> & ) ;
extern void isolate ( const char * & , std::vector<const char *> & ) ;
extern void reset ( const char * & , std::vector<const char *> & ) ;
extern void enable ( const char * & , std::vector<const char *> & ) ;
extern void disable ( const char * & , std::vector<const char *> & ) ;
extern void preset ( const char * & , std::vector<const char *> & ) ;
extern void init ( const char * & , std::vector<const char *> & ) ;
extern void cat ( const char * & , std::vector<const char *> & ) ;
extern void show ( const char * & , std::vector<const char *> & ) ;
extern void show_json ( const char * & , std::vector<const char *> & ) ;
extern void status ( const char * & , std::vector<const char *> & ) ;
extern void try_restart ( const char * & , std::vector<const char *> & ) ;
extern void is_active ( const char * & , std::vector<const char *> & ) ;
extern void is_loaded ( const char * & , std::vector<const char *> & ) ;
extern void is_enabled ( const char * & , std::vector<const char *> & ) ;
extern void convert_fstab_services ( const char * & , std::vector<const char *> & ) ;
extern void convert_systemd_units ( const char * & , std::vector<const char *> & ) ;
extern void nagios_check ( const char * & , std::vector<const char *> & ) ;
extern void load_kernel_module ( const char * & , std::vector<const char *> & ) ;
extern void unload_kernel_module ( const char * & , std::vector<const char *> & ) ;
extern void cyclog ( const char * & , std::vector<const char *> & ) ;
extern void system_control ( const char * & , std::vector<const char *> & ) ;
extern void system_manager ( const char * & , std::vector<const char *> & ) ;
extern void service_manager ( const char * & , std::vector<const char *> & ) ;
extern void setsid ( const char * & , std::vector<const char *> & ) ;
extern void system_version ( const char * & , std::vector<const char *> & ) ;
extern void unload_when_stopped ( const char * & , std::vector<const char *> & ) ;
extern void service_dt_scanner ( const char * &, std::vector<const char *> & );
extern void service_control ( const char * &, std::vector<const char *> & );
extern void service_is_enabled ( const char * &, std::vector<const char *> & );
extern void service_is_ok ( const char * &, std::vector<const char *> & );
extern void service_is_up ( const char * &, std::vector<const char *> & );
extern void service_status ( const char * &, std::vector<const char *> & );
extern void service_show ( const char * &, std::vector<const char *> & );

extern const
struct command 
commands[] = {
	// These are personalities that are also available as built-in commands in order to prevent surprises.
	// session-manager forks off instances of this:
	{	"system-control",	system_control		},
	// These simply ameliorate fumble-fingeredness:
	{	"systemctl",		system_control		},
	{	"initctl",		system_control		},
	{	"rcctl",		system_control		},
	// system-control chains to these from its subcommands:
	{	"service-is-enabled",	service_is_enabled	},
	{	"service-is-ok",	service_is_ok		},
	{	"service-is-up",	service_is_up		},
	{	"service-show",		service_show		},
	{	"service-status",	service_status		},

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
	{	"activate",			activate		},
	{	"start",			activate		},
	{	"deactivate",			deactivate		},
	{	"stop",				deactivate		},
	{	"isolate",			isolate			},
	{	"reset",			reset			},
	{	"enable",			enable			},
	{	"disable",			disable			},
	{	"preset",			preset			},
	{	"cat",				cat			},
	{	"show",				show			},
	{	"show-json",			show_json		},
	{	"status",			status			},
	{	"try-restart",			try_restart		},
	{	"condrestart",			try_restart		},
	{	"force-reload",			try_restart		},
	{	"is-active",			is_active		},
	{	"is-loaded",			is_loaded		},
	{	"is-enabled",			is_enabled		},
	{	"unload-when-stopped",		unload_when_stopped	},
	{	"convert-fstab-services",	convert_fstab_services	},
	{	"convert-systemd-units",	convert_systemd_units	},
	{	"nagios-check-service",		nagios_check		},
	{	"load-kernel-module",		load_kernel_module	},
	{	"unload-kernel-module",		unload_kernel_module	},
	{	"version",			system_version		},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;

extern const
struct command 
personalities[] = {
	{	"session-manager",	system_manager		},
	{	"service-manager",	service_manager		},
	{	"service-dt-scanner",	service_dt_scanner	},
	{	"svscan",		service_dt_scanner	},
	{	"service-control",	service_control		},
	{	"svc",			service_control		},
	{	"svok",			service_is_ok		},
	{	"svup",			service_is_up		},
	{	"svshow",		service_show		},
	{	"svstat",		service_status		},
};
const std::size_t num_personalities = sizeof personalities/sizeof *personalities;
