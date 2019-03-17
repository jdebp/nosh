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

// These are the built-in commands visible in the per-user-manager and system-control utililties.

extern void init ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void reboot_poweroff_halt_command ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void emergency_rescue_normal_command ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void activate ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void deactivate ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void isolate ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void reset ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void enable ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void disable ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void preset ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void find ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void cat ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void show ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void show_json ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void status ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void escape ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void systemd_escape ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void print_service_env ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void set_service_env ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void try_restart ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void hangup ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void is_active ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void is_loaded ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void is_enabled ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void is_service_manager_client ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void convert_fstab_services ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void get_mount_where ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void get_mount_what ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void write_volume_service_bundles ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void convert_systemd_units ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void nagios_check ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void load_kernel_module ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void unload_kernel_module ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void system_control ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void per_user_manager ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void service_manager ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void setsid ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void system_version ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void unload_when_stopped ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void service_dt_scanner ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void service_control ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void service_is_enabled ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void service_is_ok ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void service_is_up ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void service_status ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void service_show ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void chkservice ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void pipe ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void tai64nlocal ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void move_to_control_group ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void procstat ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void ps ( const char * &, std::vector<const char *> &, ProcessEnvironment & );

extern const
struct command 
commands[] = {
	// These are personalities that are also available as built-in commands in order to prevent surprises.
	// per-user-manager forks off instances of these:
	{	"system-control",		system_control		},
	{	"move-to-control-group",	move_to_control_group	},
	// These simply ameliorate fumble-fingeredness:
	{	"systemctl",			system_control		},
	{	"initctl",			system_control		},
	{	"rcctl",			system_control		},
	{	"svcadm",			system_control		},
	// system-control chains to these from its subcommands:
	{	"service-is-enabled",		service_is_enabled	},
	{	"service-is-ok",		service_is_ok		},
	{	"service-is-up",		service_is_up		},
	{	"service-show",			service_show		},
	{	"service-status",		service_status		},
	{	"pipe",				pipe			},
	{	"tai64nlocal",			tai64nlocal		},

	// These are the system-control subcommands.
	{	"reboot",			reboot_poweroff_halt_command	},
	{	"halt",				reboot_poweroff_halt_command	},
	{	"poweroff",			reboot_poweroff_halt_command	},
	{	"powercycle",			reboot_poweroff_halt_command	},
	{	"fastboot",			reboot_poweroff_halt_command	},
	{	"fastreboot",			reboot_poweroff_halt_command	},
	{	"fasthalt",			reboot_poweroff_halt_command	},
	{	"fastpoweroff",			reboot_poweroff_halt_command	},
	{	"fastpowercycle",		reboot_poweroff_halt_command	},
	{	"emergency",			emergency_rescue_normal_command	},
	{	"rescue",			emergency_rescue_normal_command	},
	{	"single",			emergency_rescue_normal_command	},
	{	"normal",			emergency_rescue_normal_command	},
	{	"default",			emergency_rescue_normal_command	},
	{	"init",				init			},
	{	"activate",			activate		},
	{	"start",			activate		},
	{	"deactivate",			deactivate		},
	{	"stop",				deactivate		},
	{	"isolate",			isolate			},
	{	"reset",			reset			},
	{	"enable",			enable			},
	{	"reenable",			enable			},
	{	"disable",			disable			},
	{	"preset",			preset			},
	{	"find",				find			},
	{	"cat",				cat			},
	{	"show",				show			},
	{	"show-json",			show_json		},
	{	"status",			status			},
	{	"escape",			escape			},
	{	"print-service-env",		print_service_env	},
	{	"set-service-env",		set_service_env		},
	{	"try-restart",			try_restart		},
	{	"try-reload-or-restart",	try_restart		},
	{	"condrestart",			try_restart		},
	{	"force-reload",			try_restart		},
	{	"hangup",			hangup			},
	{	"is-active",			is_active		},
	{	"is-loaded",			is_loaded		},
	{	"is-enabled",			is_enabled		},
	{	"is-service-manager-client",	is_service_manager_client	},
	{	"unload-when-stopped",		unload_when_stopped	},
	{	"convert-fstab-services",	convert_fstab_services	},
	{	"get-mount-where",		get_mount_where		},
	{	"get-mount-what",		get_mount_what		},
	{	"write-volume-service-bundles",	write_volume_service_bundles	},
	{	"convert-systemd-units",	convert_systemd_units	},
	{	"nagios-check-service",		nagios_check		},
	{	"load-kernel-module",		load_kernel_module	},
	{	"unload-kernel-module",		unload_kernel_module	},
	{	"version",			system_version		},
	{	"procstat",			procstat		},
	{	"ps",				ps			},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;

extern const
struct command 
personalities[] = {
	{	"per-user-manager",	per_user_manager	},
	{	"service-manager",	service_manager		},
	{	"service-dt-scanner",	service_dt_scanner	},
	{	"svscan",		service_dt_scanner	},
	{	"service-control",	service_control		},
	{	"svc",			service_control		},
	{	"svok",			service_is_ok		},
	{	"svup",			service_is_up		},
	{	"svshow",		service_show		},
	{	"svstat",		service_status		},
	{	"systemd-escape",	systemd_escape		},
	{	"chkservice",		chkservice		},
};
const std::size_t num_personalities = sizeof personalities/sizeof *personalities;
