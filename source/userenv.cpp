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
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
#include <login_cap.h>
#endif
#include "utils.h"
#include "runtime-dir.h"
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

static inline
void
set (
	const char * var,
	const std::string & val
) {
	setenv(var, val.c_str(), 1);
}

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)

static inline
void
set_if (
	const std::string & var,
	bool cond,
	const std::string & val
) {
	if (cond)
		setenv(var.c_str(), val.c_str(), 1);
	else
		unsetenv(var.c_str());
}

/// \brief Perform the login.conf substitutions before setting an environment variable.
static inline
void
set (
	const passwd * pw,
	const char * var,
	const char * val,
	bool is_path
) {
	if (!val)
		set(var, val);
	else {
		std::string s;
		while (char c = *val++) {
			if ('\\' == c) {
				c = *val++;
				if (!c) break;
				s += c;
			} else
			if ('~' == c) {
				if (pw->pw_dir) {
					s += pw->pw_dir;
					if (*val && '/' != *val && *pw->pw_dir)
						s += '/';
				}
			} else 
			if ('$' == c) {
				if (pw->pw_name)
					s += pw->pw_name;
			} else
			if (is_path && std::isspace(c)) {
				while (*val && std::isspace(*val)) ++val;
				s += ':';
			} else
				s += c;
		}
		set(var, s);
	}
}

static inline
void
set_vec (
	const passwd * pw,
	const char * vec
) {
	if (!vec) return;
	std::string var, val, * cur(&var);
	for (;;) {
		char c = *vec++;
		if (!c) {
end:
			set_if(var, &var != cur, val);
			break;
		} else
		if ('\\' == c) {
			c = *vec++;
			if (!c) goto end;
			*cur += c;
		} else
		if ('~' == c) {
			if (pw->pw_dir) {
				*cur += pw->pw_dir;
				if (*vec && '/' != *vec && *pw->pw_dir)
					*cur += '/';
			}
		} else 
		if ('$' == c) {
			if (pw->pw_name)
				*cur += pw->pw_name;
		} else
		if (',' == c) {
			set_if(var, &var != cur, val);
			cur = &var;
			var.clear();
			val.clear();
		} else
		if ('=' == c && &var == cur) {
			cur = &val;
		} else
			*cur += c;
	}
}

#if !defined(LOGIN_DEFROOTCLASS)
#define LOGIN_DEFROOTCLASS "root"
#endif

// FreeBSD's login_getpwclass() has a security hole when the class is "me".
// And OpenBSD does not have the function at all.
static inline
login_cap_t *
login_getsystemclass(
	const struct passwd * pwd
) {
	const char * c(0);
	if (pwd) {
		if (pwd->pw_class && *pwd->pw_class)
			c = pwd->pw_class;
		else
			c = 0 == pwd->pw_uid ? LOGIN_DEFROOTCLASS : LOGIN_DEFCLASS;
	}
#if defined(__OpenBSD__)
	// OpenBSD requires a const incorrectness bodge.
	return login_getclass(const_cast<char *>(c));
#else
	return login_getclass(c);
#endif
}

namespace {
/// \brief A wrapper for login_cap_t that automatically closes the database in its destructor.
struct LoginDBOwner {
#if 0 // These are unused at present.
	operator login_cap_t * () const { return d ; }
	login_cap_t * operator -> () const { return d ; }
	login_cap_t * release() { login_cap_t *dp(d); d = 0; return dp; }
#endif
	LoginDBOwner(login_cap_t * dp = 0) : d(dp) {}
	LoginDBOwner & operator= ( login_cap_t * n ) { assign(n); return *this; }
	~LoginDBOwner() { assign(0); }
	const char * getcapstr(const char *, const char *, const char *);
protected:
	login_cap_t * d;
	void assign(login_cap_t * n) { if (d) login_close(d); d = n; }
};
}

const char * 
LoginDBOwner::getcapstr(
	const char * cap,
	const char * def,
	const char * err
) {
	if (!d) return err;
#if defined(__OpenBSD__)
	// OpenBSD requires a const incorrectness bodge.
	return login_getcapstr(d, const_cast<char *>(cap), const_cast<char *>(def), const_cast<char *>(err));
#else
	return login_getcapstr(d, cap, def, err);
#endif
}

static inline
void
set_str (
	const char * var,
	const char * cap,
	const passwd * pw,
	LoginDBOwner & lc_system,
	LoginDBOwner & lc_user,
	const char * def
) {
	const char * val(lc_user.getcapstr(cap, def, 0));
	if (!val || def == val)
		val = lc_system.getcapstr(cap, def, def);
	set(pw, var, val, false);
}

static inline
void
set_pathstr (
	const char * var,
	const char * cap,
	const passwd * pw,
	LoginDBOwner & lc_system,
	LoginDBOwner & lc_user,
	const char * def
) {
	// FreeBSD's login_getpath() has botched processing of backslash.
	// And OpenBSD does not have the function at all.
	const char * val(lc_user.getcapstr(cap, def, 0));
	if (!val || def == val)
		val = lc_system.getcapstr(cap, def, def);
	set(pw, var, val, true);
}

static inline
void
set_vec (
	const char * cap,
	const passwd * pw,
	LoginDBOwner & lc
) {
	// FreeBSD's login_getpath() has a memory leak and botched processing of backslash.
	// And OpenBSD does not have the function at all.
	const char * val(lc.getcapstr(cap, 0, 0));
	if (val)
		set_vec(pw, val);
}

static inline
void
set_vec (
	const char * cap,
	const passwd * pw,
	LoginDBOwner & lc_system,
	LoginDBOwner & lc_user
) {
	set_vec(cap, pw, lc_system);
	set_vec(cap, pw, lc_user);
}

#else

namespace {
struct LoginDBOwner {};
}

static inline
void
set_str (
	const char * var,
	const char *,
	const passwd *,
	LoginDBOwner &,
	LoginDBOwner &,
	const char * def
) {
	set(var, def);
}

static inline
void
set_pathstr (
	const char * var,
	const char *,
	const passwd *,
	LoginDBOwner &,
	LoginDBOwner &,
	const char * def
) {
	set(var, def);
}

static inline
void
set_vec (
	const char *,
	const passwd *,
	LoginDBOwner &,
	LoginDBOwner &
) {
}

#endif

/* Main function ************************************************************
// **************************************************************************
*/

void
userenv ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	bool set_path(false);
	bool set_user(true);
	bool set_shell(true);
	bool set_tools(false);
	bool set_term(false);
	bool set_timezone(false);
	bool set_locale(false);
	bool set_dbus(false);
	bool set_xdg(false);
	bool set_other(false);
	try {
		popt::bool_definition set_path_option('p', "set-path", "Set the PATH and MANPATH environment variables.", set_path);
#if 0 // These are always done.
		popt::bool_definition set_user_option('h', "set-user", "Set the HOME, USER, and LOGNAME environment variables.", set_user);
		popt::bool_definition set_shell_option('s', "set-shell", "Set the SHELL environment variable.", set_shell);
#endif
		popt::bool_definition set_tools_option('e', "set-tools", "Set the EDITOR, VISUAL, and PAGER environment variables.", set_tools);
		popt::bool_definition set_term_option('t', "set-term", "Set the TERM environment variable.", set_term);
		popt::bool_definition set_timezone_option('z', "set-timezone", "Set the TZ environment variable.", set_timezone);
		popt::bool_definition set_locale_option('l', "set-locale", "Set the LANG and MM_CHARSET environment variables.", set_locale);
		popt::bool_definition set_dbus_option('l', "set-dbus", "Set the DBUS_SESSION_BUS_ADDRESS environment variable.", set_dbus);
		popt::bool_definition set_xdg_option('l', "set-xdg", "Set the XDG_RUNTIME_DIR environment variable.", set_xdg);
		popt::bool_definition set_other_option('o', "set-other", "Set other environment variables.", set_other);
		popt::definition * top_table[] = {
			&set_path_option,
#if 0 // These are always done.
			&set_user_option,
			&set_shell_option,
#endif
			&set_tools_option,
			&set_term_option,
			&set_timezone_option,
			&set_locale_option,
			&set_dbus_option,
			&set_xdg_option,
			&set_other_option
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

	const uid_t uid(geteuid());
	const gid_t gid(getegid());
	std::ostringstream us, gs;
	us << uid;
	gs << gid;
	set("UID", us.str());
	set("GID", gs.str());
	if (const passwd * pw = getpwuid(uid)) {
#if defined(__FreeBSD__) || defined(__DragonFly__)
		LoginDBOwner lc_system(login_getsystemclass(pw));
		LoginDBOwner lc_user(login_getuserclass(pw));
#elif defined(__OpenBSD__)
		LoginDBOwner lc_system(login_getsystemclass(pw));
		LoginDBOwner lc_user;
#else
		LoginDBOwner lc_system;
		LoginDBOwner lc_user;
#endif
		// These three are supersedable by setenv in login.conf.
		if (set_tools) {
			// POSIX gives us two choices of default line editor; we do not dump ed on people.
			set_str("EDITOR", "editor", pw, lc_system, lc_user, "ex");
			// POSIX gives us one choice of default visual editor.
			set_str("VISUAL", "visual", pw, lc_system, lc_user, "vi");
			set_str("PAGER", "pager", pw, lc_system, lc_user, "more");
		}
		if (set_xdg)
			set("XDG_RUNTIME_DIR", "/run/user/" + std::string(pw->pw_name) + "/");
		if (set_dbus)
			set("DBUS_SESSION_BUS_ADDRESS", "unix:path=/run/user/" + std::string(pw->pw_name) + "/bus");
		// setenv in login.conf can be superseded by all of the rest.
		if (set_other)
			set_vec("setenv", pw, lc_system, lc_user);
		if (set_path) {
			// Always default to STDPATH, even for non-superusers.
			set_pathstr("PATH", "path", pw, lc_system, lc_user, _PATH_STDPATH);
			set_pathstr("MANPATH", "manpath", pw, lc_system, lc_user, 0);
		}
		if (set_user) {
			set("HOME", pw->pw_dir);
			set("USER", pw->pw_name);
			set("LOGNAME", pw->pw_name);
		}
		if (set_shell)
			set("SHELL", *pw->pw_shell ? pw->pw_shell : _PATH_BSHELL);
		if (set_term)
			set_str("TERM", "term", pw, lc_system, lc_user, 0);
		if (set_timezone)
			set_str("TZ", "timezone", pw, lc_system, lc_user, "UTC");
		if (set_locale) {
			set_str("LANG", "lang", pw, lc_system, lc_user, "POSIX");
			set_str("MM_CHARSET", "charset", pw, lc_system, lc_user, "UTF-8");
		}
	} else {
		if (set_tools) {
			set("EDITOR", 0);
			set("PAGER", 0);
		}
		if (set_xdg)
			set("XDG_RUNTIME_DIR", 0);
		if (set_dbus)
			set("DBUS_SESSION_BUS_ADDRESS", 0);
		if (set_path) {
			// Always use STDPATH, even for non-superusers.
			set("PATH", _PATH_STDPATH);
			set("MANPATH", 0);
		}
		if (set_user) {
			set("HOME", 0);
			set("USER", 0);
			set("LOGNAME", 0);
		}
		if (set_shell)
			set("SHELL", _PATH_BSHELL);
		if (set_term)
			set("TERM", 0);
		if (set_timezone)
			set("TZ", "UTC");
		if (set_locale) {
			set("LANG", "POSIX");
			set("MM_CHARSET", "UTF-8");
		}
	}
	endpwent();
}
