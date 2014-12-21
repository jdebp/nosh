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

extern void console_ncurses_realizer ( const char * &, std::vector<const char *> & );
extern void console_fb_realizer ( const char * &, std::vector<const char *> & );
extern void console_multiplexor ( const char * &, std::vector<const char *> & );
extern void console_convert_kbdmap ( const char * &, std::vector<const char *> & );
extern void console_terminal_emulator ( const char * &, std::vector<const char *> & );
extern void detach_controlling_tty ( const char * &, std::vector<const char *> & );
extern void login_banner ( const char * &, std::vector<const char *> & );
extern void login_process ( const char * &, std::vector<const char *> & );
extern void login_prompt ( const char * &, std::vector<const char *> & );
extern void open_controlling_tty ( const char * &, std::vector<const char *> & );
extern void pty_get_tty ( const char * &, std::vector<const char *> & );
extern void pty_run ( const char * &, std::vector<const char *> & );
extern void ttylogin_starter ( const char * &, std::vector<const char *> & );
extern void vc_get_tty ( const char * &, std::vector<const char *> & );
extern void vc_reset_tty ( const char * &, std::vector<const char *> & );

extern const
struct command 
commands[] = {
	{	"console-ncurses-realizer",	console_ncurses_realizer	},
	{	"console-fb-realizer",		console_fb_realizer		},
	{	"console-multiplexor",		console_multiplexor		},
	{	"console-convert-kbdmap",	console_convert_kbdmap		},
	{	"console-terminal-emulator",	console_terminal_emulator	},
	{	"detach-controlling-tty",	detach_controlling_tty		},
	{	"login-banner",			login_banner			},
	{	"login-process",		login_process			},
	{	"login-prompt",			login_prompt			},
	{	"open-controlling-tty",		open_controlling_tty		},
	{	"pty-get-tty",			pty_get_tty			},
	{	"pty-run",			pty_run				},
	{	"ttylogin-starter",		ttylogin_starter		},
	{	"vc-get-tty",			vc_get_tty			},
	{	"vc-reset-tty",			vc_reset_tty			},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;
