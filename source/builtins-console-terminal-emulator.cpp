/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstddef>
#include "builtins.h"
#include "config/haswscons.h"
#include "config/hasevdev.h"

/* Table of commands ********************************************************
// **************************************************************************
*/

// These are the built-in commands visible in the console utilities.

extern void appendpath ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void builtins ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void chdir_home ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_clear ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_control_sequence ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_convert_kbdmap ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_decode_ecma48 ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
#if defined(HAS_EVDEV)
extern void console_evdev_realizer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
#endif
extern void console_fb_realizer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_input_method ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_input_method_control ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_kvt_realizer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_multiplexor ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_multiplexor_control ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_pcat_keyboard_realizer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
#if defined(__FreeBSD__) || defined (__DragonFly__) || defined(__OpenBSD__)
extern void console_pcat_mouse_realizer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
#endif
#if defined(__LINUX__) || defined(__linux__)
extern void console_ps2_mouse_realizer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
#endif
extern void console_resize ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_terminal_emulator ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_termio_realizer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
#if !defined(__LINUX__) && !defined(__linux__)
extern void console_ugen_hid_realizer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void console_uhid_realizer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
#endif
#if defined(HAS_WSCONS)
extern void console_wscons_realizer ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
#endif
extern void detach_controlling_tty ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void detach_kernel_usb_driver ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void fdmove ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void fdredir ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void foreground ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void framebuffer_dump ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void line_banner ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void local_reaper ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void login_banner ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void login_envuidgid ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void login_envuidgid_nopam ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void login_giveown_controlling_terminal ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void login_monitor_active ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void login_process ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void login_prompt ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void login_shell ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void login_update_utmpx ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void nvt_client ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void open_controlling_tty ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void prependpath ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void pty_get_tty ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void pty_run ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setsid ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setuidgid ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void setuidgid_fromenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void ttylogin_starter ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void unsetenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void userenv_fromenv ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void vc_get_tty ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void vc_reset_tty ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void list_logins ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
extern void system_version ( const char * & , std::vector<const char *> &, ProcessEnvironment & );

const
struct command
commands[] = {
	// Terminals
	{	"builtins",				builtins			},
	{	"version",				system_version			},
	{	"console-clear",			console_clear			},
	{	"console-control-sequence",		console_control_sequence	},
	{	"console-convert-kbdmap",		console_convert_kbdmap		},
	{	"console-decode-ecma48",		console_decode_ecma48		},
#if defined(HAS_EVDEV)
	{	"console-evdev-realizer",		console_evdev_realizer		},
#endif
	{	"console-fb-realizer",			console_fb_realizer		},
	{	"console-input-method",			console_input_method		},
	{	"console-input-method-control",		console_input_method_control	},
	{	"console-kvt-realizer",			console_kvt_realizer		},
	{	"console-multiplexor",			console_multiplexor		},
	{	"console-multiplexor-control",		console_multiplexor_control	},
	{	"console-pcat-keyboard-realizer",	console_pcat_keyboard_realizer	},
	{	"console-pcat-kbd-realizer",		console_pcat_keyboard_realizer	},	// compatibility name for services
#if defined(__FreeBSD__) || defined (__DragonFly__) || defined(__OpenBSD__)
	{	"console-pcat-mouse-realizer",		console_pcat_mouse_realizer	},
#endif
#if defined(__LINUX__) || defined(__linux__)
	{	"console-ps2-mouse-realizer",		console_ps2_mouse_realizer	},
#endif
	{	"console-resize",			console_resize			},
	{	"console-terminal-emulator",		console_terminal_emulator	},
	{	"console-termio-realizer",		console_termio_realizer		},
#if !defined(__LINUX__) && !defined(__linux__)
	{	"console-ugen-hid-realizer",		console_ugen_hid_realizer	},
	{	"console-uhid-realizer",		console_uhid_realizer		},
#endif
#if defined(HAS_WSCONS)
	{	"console-wscons-realizer",		console_wscons_realizer		},
#endif
	{	"pty-run",				pty_run				},
	{	"framebuffer-dump",			framebuffer_dump		},
	{	"resizecons",				console_resize			},
	{	"clear_console",			console_clear			},
	{	"setterm",				console_control_sequence	},
	{	"chvt",					console_multiplexor_control	},
	{	"ttylogin-starter",			ttylogin_starter		},
	{	"login-shell",				login_shell			},
	{	"login-update-utmpx",			login_update_utmpx		},
	{	"utx",					login_update_utmpx		},
	{	"list-logins",				list_logins			},
	{	"nvt-client",				nvt_client			},

	// Chain-loading non-terminals
	{	"detach-controlling-tty",		detach_controlling_tty		},
	{	"detach-controlling-terminal",		detach_controlling_tty		},
	{	"detach-kernel-usb-driver",		detach_kernel_usb_driver	},
	{	"line-banner",				line_banner			},
	{	"login-banner",				login_banner			},
	{	"login-envuidgid",			login_envuidgid			},
	{	"login-envuidgid-nopam",		login_envuidgid_nopam		},
	{	"login-giveown-controlling-terminal",	login_giveown_controlling_terminal	},
	{	"login-monitor-active",			login_monitor_active		},
	{	"login-process",			login_process			},
	{	"login-prompt",				login_prompt			},
	{	"open-controlling-tty",			open_controlling_tty		},
	{	"open-controlling-terminal",		open_controlling_tty		},
	{	"pty-get-tty",				pty_get_tty			},
	{	"pty-allocate",				pty_get_tty			},
	{	"vc-get-tty",				vc_get_tty			},
	{	"vc-get-terminal",			vc_get_tty			},
	{	"vc-reset-tty",				vc_reset_tty			},
	{	"vc-reset-terminal",			vc_reset_tty			},

	// These are commonly used in chains with the above.
	{	"appendpath",			appendpath			},
	{	"chdir-home",			chdir_home			},
	{	"fdmove",			fdmove				},
	{	"fdredir",			fdredir				},
	{	"foreground",			foreground			},
	{	"local-reaper",			local_reaper			},
	{	"prependpath",			prependpath			},
	{	"setsid",			setsid				},
	{	"setuidgid",			setuidgid			},
	{	"setuidgid-fromenv",		setuidgid_fromenv		},
	{	"unsetenv",			unsetenv			},
	{	"userenv-fromenv",		userenv_fromenv			},
};
const std::size_t num_commands = sizeof commands/sizeof *commands;

const
struct command
personalities[] = {
// There are no extra personalities over and above the built-in commands.
};
const std::size_t num_personalities = sizeof personalities/sizeof *personalities;
