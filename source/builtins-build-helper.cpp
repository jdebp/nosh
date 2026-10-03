/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstddef>
#include "builtins.h"

/* Table of commands ********************************************************
// **************************************************************************
*/

// These are the built-in commands visible in the build-helper utililty.

extern void builtins ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void convert_systemd_units ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void command_exec ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void system_version ( const char * & , std::vector<const char *> &, ProcessEnvironment & );
extern void setlock ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void clearenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void printenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void read_conf ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void build_helper ( const char * &, std::vector<const char *> &, ProcessEnvironment & );

const
struct command
commands[] = {
	{	"builtins",			builtins		},

	// These are the build-helper subcommands.
	{	"convert-systemd-units",	convert_systemd_units	},
	{	"version",			system_version		},
	{	"exec",				command_exec		},
	{	"setlock",			setlock			},
	{	"clearenv",			clearenv		},
	{	"setenv",			setenv			},
	{	"printenv",			printenv		},
	{	"read-conf",			read_conf		},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;

const
struct command
personalities[] = {
	{	"build-helper",			build_helper		},
};
const std::size_t num_personalities = sizeof personalities/sizeof *personalities;
