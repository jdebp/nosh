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

// These are the built-in commands visible in the console utilities.

extern void console_convert_kbdmap ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_decode_ecma48 ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_fb_realizer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_input_method ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_input_method_control ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_multiplexor ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_multiplexor_control ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_termio_realizer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_resize ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_clear ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_control_sequence ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_terminal_emulator ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void detach_controlling_tty ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void detach_kernel_usb_driver ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void emergency_login ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void foreground ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void line_banner ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void login_banner ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void login_process ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void login_prompt ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void login_update_utmpx ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void open_controlling_tty ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void pty_get_tty ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void pty_run ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setsid ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void ttylogin_starter ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void vc_get_tty ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void vc_reset_tty ( const char * &, std::vector<const char *> &, ProcessEnvironment & );

extern const
struct command 
commands[] = {
	// Terminals
	{	"console-convert-kbdmap",	console_convert_kbdmap		},
	{	"console-decode-ecma48",	console_decode_ecma48		},
	{	"console-fb-realizer",		console_fb_realizer		},
	{	"console-input-method",		console_input_method		},
	{	"console-input-method-control",	console_input_method_control	},
	{	"console-multiplexor",		console_multiplexor		},
	{	"console-multiplexor-control",	console_multiplexor_control	},
	{	"console-termio-realizer",	console_termio_realizer		},
	{	"console-resize",		console_resize			},
	{	"console-clear",		console_clear			},
	{	"console-control-sequence",	console_control_sequence	},
	{	"console-terminal-emulator",	console_terminal_emulator	},
	{	"emergency-login",		emergency_login			},
	{	"pty-run",			pty_run				},
	{	"resizecons",			console_resize			},
	{	"clear_console",		console_clear			},
	{	"setterm",			console_control_sequence	},
	{	"chvt",				console_multiplexor_control	},
	{	"ttylogin-starter",		ttylogin_starter		},
	{	"login-update-utmpx",		login_update_utmpx		},
	{	"utx",				login_update_utmpx		},

	// Chain-loading non-terminals
	{	"detach-controlling-tty",	detach_controlling_tty		},
	{	"detach-kernel-usb-driver",	detach_kernel_usb_driver	},
	{	"line-banner",			line_banner			},
	{	"login-banner",			login_banner			},
	{	"login-process",		login_process			},
	{	"login-prompt",			login_prompt			},
	{	"vc-get-tty",			vc_get_tty			},
	{	"vc-reset-tty",			vc_reset_tty			},
	{	"open-controlling-tty",		open_controlling_tty		},
	{	"pty-get-tty",			pty_get_tty			},

	// These are commonly used in chains with the above.
	{	"setsid",			setsid				},
	{	"foreground",			foreground			},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;

// There are no extra personalities over and above the built-in commands.
extern const
struct command 
personalities[] = {
	{	0,			0,			},
};
const std::size_t num_personalities = 0;
