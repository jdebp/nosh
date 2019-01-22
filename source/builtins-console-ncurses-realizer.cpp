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

// These are the built-in commands visible in the console ncurses-using utilities.

extern void console_ncurses_realizer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );

extern const
struct command 
commands[] = {
	// Terminals
	{	"console-ncurses-realizer",	console_ncurses_realizer	},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;

extern const
struct command 
personalities[] = {
};
const std::size_t num_personalities = sizeof personalities/sizeof *personalities;
