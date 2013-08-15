/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include "utils.h"
#include "popt.h"
#if !defined(_GNU_SOURCE)
extern "C" int clearenv();
#endif

/* Main function ************************************************************
// **************************************************************************
*/

static
char *
save (
	const char * var
) {
	const char * val = std::getenv(var);
	if (!val) return 0;
	const size_t len(std::strlen(val));
	char * sav(new char[len + 1]);
	if (sav) memcpy(sav, val, len + 1);
	return sav;
}

static
void
restore (
	const char * var,
	char * & val
) {
	if (val) {
		setenv(var, val, 1);
		delete[] val, val = 0;
	}
}

void
clearenv ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	bool keep_path(false);
	bool keep_user(false);
	bool keep_shell(false);
	bool keep_term(false);
	bool keep_locale(false);
	try {
		popt::bool_definition keep_path_option('p', "keep-path", "Keep the PATH and LD_LIBRARY_PATH environment variables.", keep_path);
		popt::bool_definition keep_user_option('h', "keep-user", "Keep the HOME, USER, and LOGNAME environment variables.", keep_user);
		popt::bool_definition keep_shell_option('s', "keep-shell", "Keep the SHELL environment variable.", keep_shell);
		popt::bool_definition keep_term_option('t', "keep-term", "Keep the TERM and TTY environment variables.", keep_term);
		popt::bool_definition keep_locale_option('l', "keep-locale", "Keep the LANG and LC_... environment variables.", keep_locale);
		popt::definition * top_table[] = {
			&keep_path_option,
			&keep_user_option,
			&keep_shell_option,
			&keep_term_option,
			&keep_locale_option
		};
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

	char * path(0);
	char * ld_library_path(0);
	char * shell(0);
	char * home(0);
	char * user(0);
	char * logname(0);
	char * term(0);
	char * tty(0);
	char * lang(0);
	char * lc_all(0);
	char * lc_collate(0);
	char * lc_ctype(0);
	char * lc_messages(0);
	char * lc_monetary(0);
	char * lc_numeric(0);
	char * lc_time(0);

	if (keep_path) {
		path = save("PATH");
		ld_library_path = save("LD_LIBRARY_PATH");
	}
	if (keep_user) {
		home = save("HOME");
		user = save("USER");
		logname = save("LOGNAME");
	}
	if (keep_shell)
		shell = save("SHELL");
	if (keep_term) {
		term = save("TERM");
		tty = save("TTY");
	}
	if (keep_locale) {
		lang = save("LANG");
		lc_all = save("LC_ALL");
		lc_collate = save("LC_COLLATE");
		lc_ctype = save("LC_CTYPE");
		lc_messages = save("LC_MESSAGES");
		lc_monetary = save("LC_MONETARY");
		lc_numeric = save("LC_NUMERIC");
		lc_time = save("LC_TIME");
	}

	const int rc(clearenv());
	if (0 > rc) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "clearenv", std::strerror(error));
		throw EXIT_FAILURE;
	}

	restore("PATH", path);
	restore("LD_LIBRARY_PATH", ld_library_path);
	restore("HOME", home);
	restore("USER", user);
	restore("LOGNAME", logname);
	restore("SHELL", shell);
	restore("TERM", term);
	restore("TTY", tty);
	restore("LANG", lang);
	restore("LC_ALL", lc_all);
	restore("LC_COLLATE", lc_collate);
	restore("LC_CTYPE", lc_ctype);
	restore("LC_MESSAGES", lc_messages);
	restore("LC_MONETARY", lc_monetary);
	restore("LC_NUMERIC", lc_numeric);
	restore("LC_TIME", lc_time);
}
