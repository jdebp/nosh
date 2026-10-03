/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cctype>
#include <unistd.h>
#include <termios.h>
#include "config/hasshadow.h"
#if defined(HAS_SHADOW)
#	include <shadow.h>
#endif
#include "config/haskenv.h"
#if defined(HAS_KENV)
#	include <kenv.h>
#endif
#include <pwd.h>
#include "popt.h"
#include "utils.h"
#include "ProcessEnvironment.h"
#include "DefaultEnvironment.h"
#include "ttyutils.h"

namespace {

// This is not a utility library function because this is the only place in the entire toolset where echo is specifically disabled for password input.
// We do not want to bring in the rest of the ttyutils library just for this.
termios
disable_echo (
	const termios & ti
) {
	termios t(ti);
	t.c_lflag &= ~(ECHO|ECHOE|ECHOK|ECHOCTL|ECHOKE|ECHONL);
	return t;
}

const char SH[] = "sh";

}

/* Main function ************************************************************
// **************************************************************************
*/

void
emergency_login (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, nullptr, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	const char * shell(envs.query("SHELL"));
#if defined(HAS_KENV)
	char kenv_buf[PATH_MAX + 1];
	const int n(kenv(KENV_GET, "init_shell", kenv_buf, sizeof kenv_buf - 1));
	if (0 < n) {
		kenv_buf[n] = '\0';
		shell = kenv_buf;
	}
#endif

	struct passwd * p(getpwnam("root"));
	if (!p || p->pw_uid != 0) p = getpwuid(0);
	if (p) {
#if defined(HAS_SHADOW)
		struct spwd * const s(getspnam(p->pw_name));
		if (s) {
			const char * const passwd(s->sp_pwdp);
#else
			const char * const passwd(p->pw_passwd);
#endif
			if (passwd && *passwd) {
				for (;;) {
					termios original_attr;
					char pass[1024];
					std::fputs("Emergency superuser password:", stdout);
					std::fflush(stdout);
					if (0 <= tcgetattr_nointr(STDIN_FILENO, original_attr))
						tcsetattr_nointr(STDIN_FILENO, TCSADRAIN, disable_echo(original_attr));
					const char * r(std::fgets(pass, sizeof pass, stdin));
					std::putc('\n', stdout);
					std::fflush(stdout);
					tcsetattr_nointr(STDIN_FILENO, TCSADRAIN, original_attr);
					if (!r) {
						std::fprintf(stderr, "%s: FATAL: %s\n", prog, "EOF");
#if defined(HAS_SHADOW)
						endspent();
#endif
						endpwent();
						throw EXIT_FAILURE;
					}
					const std::size_t l(std::strlen(pass));
					if (l > 0 && '\n' == pass[l - 1]) pass[l - 1] = '\0';
					const char *encrypted(crypt(pass, passwd));
					std::memset(pass, '\0', sizeof pass);
					if (!std::strcmp(encrypted, passwd)) break;
					std::fputs("Wrong superuser password.\n", stderr);
				}
			}
#if defined(HAS_SHADOW)
			endspent();
		}
#endif
		if (!shell) {
			if (p->pw_shell && *p->pw_shell)
				shell = strdup(p->pw_shell);
		}
	}
	endpwent();

	if (shell && *shell) {
		execlp(shell, SH, static_cast<const char *>(nullptr));
		message_error_errno(prog, envs, shell);
	}

	shell = DefaultEnvironment::UserLogin::SHELL;
	if (shell && *shell) {
		execlp(shell, SH, static_cast<const char *>(nullptr));
		message_error_errno(prog, envs, shell);
	}

	args.push_back(SH);
	next_prog = arg0_of(args);
}
