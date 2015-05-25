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

extern void service_control ( const char * &, std::vector<const char *> & );
extern void service_is_ok ( const char * &, std::vector<const char *> & );
extern void service_is_up ( const char * &, std::vector<const char *> & );
extern void service_status ( const char * &, std::vector<const char *> & );
extern void service_show ( const char * &, std::vector<const char *> & );
extern void service_dt_scanner ( const char * &, std::vector<const char *> & );

extern const
struct command 
commands[] = {
	{	"service-control",		service_control			},
	{	"svc",				service_control			},
	{	"service-is-ok",		service_is_ok			},
	{	"svok",				service_is_ok			},
	{	"service-is-up",		service_is_up			},
	{	"svup",				service_is_up			},
	{	"service-show",			service_show			},
	{	"svshow",			service_show			},
	{	"service-status",		service_status			},
	{	"svstat",			service_status			},
	{	"service-dt-scanner",		service_dt_scanner		},
	{	"svscan",			service_dt_scanner		},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;

// There are no extra personalities over and above the built-in commands.
extern const
struct command 
personalities[] = {
	{	0,			0,			},
};
const std::size_t num_personalities = 0;
