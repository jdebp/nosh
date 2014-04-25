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

void init ( const char * & , std::vector<const char *> & ) ;
void sysinit ( const char * & , std::vector<const char *> & ) ;
void isolate ( const char * & , std::vector<const char *> & ) ;
void activate ( const char * & , std::vector<const char *> & ) ;
void cyclog ( const char * & , std::vector<const char *> & ) ;
void system_control ( const char * & , std::vector<const char *> & ) ;
void system_manager ( const char * & , std::vector<const char *> & ) ;
void service_manager ( const char * & , std::vector<const char *> & ) ;

extern const
struct command 
commands[] = {
	{	"system-manager",	system_manager		},
	{	"systemd",		system_manager		},
	{	"systemctl",		system_control		},
	{	"initctl",		system_control		},

	// These are the system-control subcommands used by system-manager.
	{	"init",			init			},
	{	"sysinit",		sysinit			},
	{	"isolate",		isolate			},
	{	"activate",		activate		},
	{	"start",		activate		},

	// These are spawned by system-manager.
	{	"service-manager",	service_manager		},
	{	"system-control",	system_control		},
	{	"cyclog",		cyclog			},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;
