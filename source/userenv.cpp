/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <paths.h>
#include <pwd.h>
#include "utils.h"
#include "popt.h"

static inline
void
set (
	const char * var,
	const char * val
) {
	if (val)
		setenv(var, val, 1);
	else
		unsetenv(var);
}

/* Main function ************************************************************
// **************************************************************************
*/

void
userenv ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[0] = {};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "prog");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	const uid_t uid(geteuid());
	const gid_t gid(getegid());
	std::ostringstream us, gs;
	us << uid;
	gs << gid;
	set("UID", us.str().c_str());
	set("GID", gs.str().c_str());
	if (const passwd * pw = getpwuid(uid)) {
		set("HOME", pw->pw_dir);
		set("SHELL", *pw->pw_shell ? pw->pw_shell : _PATH_BSHELL);
		set("USER", pw->pw_name);
		set("LOGNAME", pw->pw_name);
	} else {
		unsetenv("HOME");
		unsetenv("SHELL");
		unsetenv("USER");
		unsetenv("LOGNAME");
	}
	endpwent();
}
