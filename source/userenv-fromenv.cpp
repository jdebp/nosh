/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <pwd.h>
#include "utils.h"
#include "ProcessEnvironment.h"
#include "UserEnvironmentSetter.h"
#include "runtime-dir.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
userenv_fromenv ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	UserEnvironmentSetter setter(envs);
	try {
		bool no_set_user(false), no_set_shell(false);
		popt::bool_definition set_path_option('p', "set-path", "Set the PATH and MANPATH environment variables.", setter.set_path);
		popt::bool_definition no_set_user_option('\0', "no-set-user", "Do not set the HOME, USER, and LOGNAME environment variables.", no_set_user);
		popt::bool_definition no_set_shell_option('\0', "no-set-shell", "Do not set the SHELL environment variable.", no_set_shell);
		popt::bool_definition set_tools_option('e', "set-tools", "Set the EDITOR, VISUAL, and PAGER environment variables.", setter.set_tools);
		popt::bool_definition set_term_option('t', "set-term", "Set the TERM environment variable.", setter.set_term);
		popt::bool_definition set_timezone_option('z', "set-timezone", "Set the TZ environment variable.", setter.set_timezone);
		popt::bool_definition set_locale_option('l', "set-locale", "Set the LANG and MM_CHARSET environment variables.", setter.set_locale);
		popt::bool_definition set_dbus_option('d', "set-dbus", "Set the DBUS_SESSION_BUS_ADDRESS environment variable.", setter.set_dbus);
		popt::bool_definition set_xdg_option('x', "set-xdg", "Set the XDG_RUNTIME_DIR environment variable.", setter.set_xdg);
		popt::bool_definition set_other_option('o', "set-other", "Set other environment variables.", setter.set_other);
		popt::definition * top_table[] = {
			&no_set_user_option,
			&no_set_shell_option,
			&set_path_option,
			&set_tools_option,
			&set_term_option,
			&set_timezone_option,
			&set_locale_option,
			&set_dbus_option,
			&set_xdg_option,
			&set_other_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		setter.set_user = !no_set_user;
		setter.set_shell = !no_set_shell;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	uid_t uid(-1);
	if (const char * text = envs.query("UID")) {
		const char * old(text);
		uid = std::strtoul(text, const_cast<char **>(&text), 0);
		if (text == old || *text) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, old, "Not a number.");
			throw EXIT_FAILURE;
		}
	} else {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "UID", "Missing environment variable.");
		throw EXIT_FAILURE;
	}
	const passwd * pw = getpwuid(uid);
	setter.apply(pw);
	endpwent();
}
