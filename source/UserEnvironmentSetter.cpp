/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <map>
#include <string>
#include <cstring>
#include <cstdlib>
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
#include <sys/types.h>
#include <login_cap.h>
#endif
#include <unistd.h>
#include <paths.h>
#include "UserEnvironmentSetter.h"
#include "ProcessEnvironment.h"

// OpenBSD requires const incorrectness bodges.
#if defined(__OpenBSD__)
namespace {
inline login_cap_t * login_getclass(const char * c) { return login_getclass(const_cast<char *>(c)); }
inline const char * login_getcapstr(login_cap_t * d, const char * cap, const char * def, const char * err) { return login_getcapstr(d, const_cast<char *>(cap), const_cast<char *>(def), const_cast<char *>(err)); }
}
#endif

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)

#if !defined(LOGIN_DEFROOTCLASS)
#define LOGIN_DEFROOTCLASS "root"
#endif
#if !defined(LOGIN_DEFCLASS)
#define LOGIN_DEFCLASS "default"
#endif

namespace {

// FreeBSD's login_getpwclass() has a security hole when the class is "me".
// And OpenBSD does not have the function at all.
inline
login_cap_t *
login_getsystemclass(
	const passwd * pwd
) {
	const char * c(0);
	if (pwd) {
		if (pwd->pw_class && *pwd->pw_class)
			c = pwd->pw_class;
		else
			c = 0 == pwd->pw_uid ? LOGIN_DEFROOTCLASS : LOGIN_DEFCLASS;
	}
	return login_getclass(c);
}

#if defined(__OpenBSD__)
// OpenBSD lacks this, too.
inline login_cap_t * login_getuserclass(const passwd *) { return 0; }
#endif

}

#else

namespace {

struct login_cap_t {} singleton_cap;
inline login_cap_t * login_getsystemclass(const passwd *) { return &singleton_cap; }
inline login_cap_t * login_getuserclass(const passwd *) { return &singleton_cap; }
inline const char * login_getcapstr(struct login_cap_t * d, const char *cap, const char *def, const char *err) { return d && cap ? def : err; }
inline void login_close(struct login_cap_t *) {}

}

#endif

/// \brief A wrapper for login_cap_t that automatically closes the database in its destructor.
struct UserEnvironmentSetter::LoginDBOwner {
#if 0 // These are unused at present.
	operator login_cap_t * () const { return d ; }
	login_cap_t * operator -> () const { return d ; }
	login_cap_t * release() { login_cap_t *dp(d); d = 0; return dp; }
	LoginDBOwner & operator= ( login_cap_t * n ) { assign(n); return *this; }
#endif
	LoginDBOwner(login_cap_t * dp = 0) : d(dp) {}
	~LoginDBOwner() { assign(0); }
	const char * getcapstr(const char *, const char *, const char *);
protected:
	login_cap_t * d;
	void assign(login_cap_t * n) { if (d) login_close(d); d = n; }
	std::string res;
private:
	LoginDBOwner & operator= (const LoginDBOwner &);
	LoginDBOwner(const LoginDBOwner &);
};

const char * 
UserEnvironmentSetter::LoginDBOwner::getcapstr(
	const char * cap,
	const char * def,
	const char * err
) {
#if defined(__OpenBSD__)
	// OpenBSD's login_getcapstr() does not have FreeBSD's NULL pointer check.
	if (!d) return err;
#endif
	const char * r(login_getcapstr(d, cap, def, err));
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	// FreeBSD's and OpenBSD's login_getcapstr() leaks memory, only sometimes.
	if (def != r && err != r && r) {
		res = r;
		free(const_cast<char *>(r));
		r = res.c_str();
	}
#endif
	return r;
}

UserEnvironmentSetter::UserEnvironmentSetter(
	ProcessEnvironment & e
) :
	set_path(false),
	set_user(false),
	set_shell(false),
	set_tools(false),
	set_term(false),
	set_timezone(false),
	set_locale(false),
	set_dbus(false),
	set_xdg(false),
	set_other(false),
	envs(e)
{
}

inline
void
UserEnvironmentSetter::set_if (
	const std::string & var,
	bool cond,
	const std::string & val
) {
	if (cond)
		envs.set(var, val);
	else
		envs.unset(var);
}

/// \brief Perform the login.conf substitutions before setting an environment variable.
inline
void
UserEnvironmentSetter::set (
	const passwd * pw,
	const char * var,
	const char * val,
	bool is_path
) {
	if (!val)
		envs.set(var, val);
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
		envs.set(var, s);
	}
}

void
UserEnvironmentSetter::set_vec (
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

void
UserEnvironmentSetter::set_str (
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

void
UserEnvironmentSetter::set_pathstr (
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

inline
void
UserEnvironmentSetter::set_vec (
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

inline
void
UserEnvironmentSetter::set_vec (
	const char * cap,
	const passwd * pw,
	LoginDBOwner & lc_system,
	LoginDBOwner & lc_user
) {
	set_vec(cap, pw, lc_system);
	set_vec(cap, pw, lc_user);
}

void
UserEnvironmentSetter::apply (
	const passwd * pw
) {
	if (pw) {
		LoginDBOwner lc_system(login_getsystemclass(pw));
		LoginDBOwner lc_user(login_getuserclass(pw));
		// These three are supersedable by setenv in login.conf.
		if (set_tools) {
			// POSIX gives us two choices of default line editor; we do not dump ed on people.
			set_str("EDITOR", "editor", pw, lc_system, lc_user, "ex");
			// POSIX gives us one choice of default visual editor.
			set_str("VISUAL", "visual", pw, lc_system, lc_user, "vi");
			set_str("PAGER", "pager", pw, lc_system, lc_user, "more");
		}
		if (set_xdg) {
			envs.set("XDG_RUNTIME_DIR", "/run/user/" + std::string(pw->pw_name) + "/");
			// TrueOS Desktop adds /share to the default search path.
			// But it inverts this order, making "local" the lowest priority.
			// We give "local" data files priority over the operating system data files, as the XDG Desktop Specification does.
			envs.set("XDG_DATA_DIRS", "/usr/local/share:/usr/share:/share");
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
			envs.set("XDG_CONFIG_DIRS", "/usr/local/etc/xdg");
#else
			// The default of /etc/xdg applies to everyone else.
			envs.set("XDG_CONFIG_DIRS", 0);
#endif
			// XDG_CONFIG_HOME defaults to $HOME/.config which is fine.
			envs.set("XDG_CONFIG_HOME", 0);
			// XDG_DATA_HOME defaults to $HOME/.local/share which is fine.
			envs.set("XDG_DATA_HOME", 0);
		}
		if (set_dbus)
			envs.set("DBUS_SESSION_BUS_ADDRESS", "unix:path=/run/user/" + std::string(pw->pw_name) + "/bus");
		// setenv in login.conf can be superseded by all of the rest.
		if (set_other)
			set_vec("setenv", pw, lc_system, lc_user);
		if (set_path) {
			// Always default to STDPATH, even for non-superusers.
			set_pathstr("PATH", "path", pw, lc_system, lc_user, _PATH_STDPATH);
			set_pathstr("MANPATH", "manpath", pw, lc_system, lc_user, 0);
		}
		if (set_user) {
			envs.set("HOME", pw->pw_dir);
			envs.set("USER", pw->pw_name);
			envs.set("LOGNAME", pw->pw_name);
		}
		if (set_shell)
			envs.set("SHELL", *pw->pw_shell ? pw->pw_shell : _PATH_BSHELL);
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
			envs.set("EDITOR", 0);
			envs.set("PAGER", 0);
		}
		if (set_xdg) {
			envs.set("XDG_RUNTIME_DIR", 0);
			envs.set("XDG_DATA_DIRS", 0);
			envs.set("XDG_CONFIG_DIRS", 0);
			envs.set("XDG_CONFIG_HOME", 0);
			envs.set("XDG_DATA_HOME", 0);
		}
		if (set_dbus)
			envs.set("DBUS_SESSION_BUS_ADDRESS", 0);
		if (set_path) {
			// Always use STDPATH, even for non-superusers.
			envs.set("PATH", _PATH_STDPATH);
			envs.set("MANPATH", 0);
		}
		if (set_user) {
			envs.set("HOME", 0);
			envs.set("USER", 0);
			envs.set("LOGNAME", 0);
		}
		if (set_shell)
			envs.set("SHELL", _PATH_BSHELL);
		if (set_term)
			envs.set("TERM", 0);
		if (set_timezone)
			envs.set("TZ", "UTC");
		if (set_locale) {
			envs.set("LANG", "POSIX");
			envs.set("MM_CHARSET", "UTF-8");
		}
	}
}
