/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <map>
#include <string>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include "UserEnvironmentSetter.h"
#include "ProcessEnvironment.h"
#include "DefaultEnvironment.h"
#include "LoginClassRecordOwner.h"

UserEnvironmentSetter::UserEnvironmentSetter(
	ProcessEnvironment & e
) :
	set_user(false),
	set_shell(false),
	default_locale(false),
	default_path(false),
	default_term(false),
	default_timezone(false),
	default_tools(false),
	set_dbus(false),
	set_xdg(false),
	set_other(false),
	toolkit_pager(false),
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
	const passwd & pw,
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
				if (pw.pw_dir) {
					s += pw.pw_dir;
					if (*val && '/' != *val && *pw.pw_dir)
						s += '/';
				}
			} else
			if ('$' == c) {
				if (pw.pw_name)
					s += pw.pw_name;
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
	const passwd & pw,
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
			if (pw.pw_dir) {
				*cur += pw.pw_dir;
				if (*vec && '/' != *vec && *pw.pw_dir)
					*cur += '/';
			}
		} else
		if ('$' == c) {
			if (pw.pw_name)
				*cur += pw.pw_name;
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
UserEnvironmentSetter::default_str (
	const char * var,
	const char * val
) {
	if (!envs.query(var)) envs.set(var, val);
}

void
UserEnvironmentSetter::default_str (
	const char * var,
	const char * cap,
	const passwd & pw,
	LoginClassRecordOwner & lc_system,
	LoginClassRecordOwner & lc_user,
	const char * def
) {
	if (envs.query(var)) return;
	const char * val(lc_user.getcapstr(cap, def, nullptr));
	if (!val || def == val)
		val = lc_system.getcapstr(cap, def, def);
	set(pw, var, val, false);
}

void
UserEnvironmentSetter::default_pathstr (
	const char * var,
	const char * cap,
	const passwd & pw,
	LoginClassRecordOwner & lc_system,
	LoginClassRecordOwner & lc_user,
	const char * def
) {
	if (envs.query(var)) return;
	// FreeBSD's login_getpath() has botched processing of backslash.
	// And OpenBSD does not have the function at all.
	const char * val(lc_user.getcapstr(cap, def, nullptr));
	if (!val || def == val)
		val = lc_system.getcapstr(cap, def, def);
	set(pw, var, val, true);
}

inline
void
UserEnvironmentSetter::set_vec (
	const char * cap,
	const passwd & pw,
	LoginClassRecordOwner & lc
) {
	// FreeBSD's login_getpath() has a memory leak and botched processing of backslash.
	// And OpenBSD does not have the function at all.
	const char * val(lc.getcapstr(cap, nullptr, nullptr));
	if (val)
		set_vec(pw, val);
}

inline
void
UserEnvironmentSetter::set_vec (
	const char * cap,
	const passwd & pw,
	LoginClassRecordOwner & lc_system,
	LoginClassRecordOwner & lc_user
) {
	set_vec(cap, pw, lc_system);
	set_vec(cap, pw, lc_user);
}

void
UserEnvironmentSetter::prependpath (
	const char * var,
	const std::string & app
) {
	if (const char * val = envs.query(var)) {
		std::string s(app);
		s += ':';
		s += val;
		envs.set(var, s);
	} else {
		envs.set(var, app);
	}
}

void
UserEnvironmentSetter::apply (
	const passwd * pw
) {
	const char * pager(toolkit_pager ? DefaultEnvironment::Toolkit::PAGER : DefaultEnvironment::UserLogin::PAGER);
	const char * manpager(toolkit_pager ? DefaultEnvironment::Toolkit::MANPAGER : DefaultEnvironment::UserLogin::MANPAGER);
	const char * www_browser(DefaultEnvironment::UserLogin::WWWBROWSER);
	const char * xml_viewer(toolkit_pager ? DefaultEnvironment::Toolkit::XMLVIEWER : nullptr);
	if (pw) {
		LoginClassRecordOwner lc_system(LoginClassRecordOwner::GetSystem(*pw));
		LoginClassRecordOwner lc_user(LoginClassRecordOwner::GetUser(*pw));
		// These three are supersedable by setenv in login.conf.
		if (set_xdg) {
			envs.set("XDG_RUNTIME_DIR", "/run/user/" + std::string(pw->pw_name) + "/");
			envs.set("XDG_DATA_DIRS", DefaultEnvironment::UserLogin::XDG_DATA_DIRS);
			envs.set("XDG_CONFIG_DIRS", DefaultEnvironment::UserLogin::XDG_CONFIG_DIRS);
			// XDG_CONFIG_HOME has a fallback of $HOME/.config which is fine.
			envs.set("XDG_CONFIG_HOME", nullptr);
			// XDG_DATA_HOME has a fallback of $HOME/.local/share which is fine.
			envs.set("XDG_DATA_HOME", nullptr);
			// XDG_STATE_HOME has a fallback of $HOME/.local/state which is fine.
			envs.set("XDG_STATE_HOME", nullptr);
			// XDG_CACHE_HOME has a fallback of $HOME/.cache which is fine.
			envs.set("XDG_CACHE_HOME", nullptr);
		}
		if (set_dbus)
			envs.set("DBUS_SESSION_BUS_ADDRESS", "unix:path=/run/user/" + std::string(pw->pw_name) + "/bus");
		// setenv in login.conf can be superseded by all of the rest.
		if (set_other)
			set_vec("setenv", *pw, lc_system, lc_user);
		if (default_locale) {
			default_str("LANG", "lang", *pw, lc_system, lc_user, DefaultEnvironment::UserLogin::LANG);
			default_str("MM_CHARSET", "charset", *pw, lc_system, lc_user, DefaultEnvironment::UserLogin::MM_CHARSET);
		}
		if (default_path) {
			default_pathstr("PATH", "path", *pw, lc_system, lc_user, nullptr);
			default_pathstr("MANPATH", "manpath", *pw, lc_system, lc_user, nullptr);
			default_pathstr("TERMPATH", "termpath", *pw, lc_system, lc_user, nullptr);
			default_pathstr("TERMINFO_DIRS", "terminfopath", *pw, lc_system, lc_user, nullptr);
			// Superusers and non-superusers have the same default.
			default_str("PATH", DefaultEnvironment::UserLogin::PATH);
			default_str("TERMPATH", DefaultEnvironment::UserLogin::TERMPATH);
			default_str("TERMINFO_DIRS", DefaultEnvironment::UserLogin::TERMINFO_DIRS);
		}
		if (default_term)
			default_str("TERM", "term", *pw, lc_system, lc_user, DefaultEnvironment::TERM);
		if (default_timezone)
			default_str("TZ", "timezone", *pw, lc_system, lc_user, DefaultEnvironment::UserLogin::TZ);
		if (default_tools) {
			default_str("EDITOR", "editor", *pw, lc_system, lc_user, DefaultEnvironment::UserLogin::EDITOR);
			default_str("VISUAL", "visual", *pw, lc_system, lc_user, DefaultEnvironment::UserLogin::VISUAL);
			default_str("PAGER", "pager", *pw, lc_system, lc_user, pager);
			default_str("MANPAGER", "manpager", *pw, lc_system, lc_user, manpager);
			default_str("MANXMLVIEWER", "manxmlviewer", *pw, lc_system, lc_user, xml_viewer);
			default_str("WWWBROWSER", "www-browser", *pw, lc_system, lc_user, www_browser);
		}
		if (set_user) {
			envs.set("HOME", pw->pw_dir);
			envs.set("USER", pw->pw_name);
			envs.set("LOGNAME", pw->pw_name);
		}
		if (set_shell) {
			envs.set("SHELL", nullptr);
			default_str("SHELL", "shell", *pw, lc_system, lc_user, *pw->pw_shell ? pw->pw_shell : DefaultEnvironment::UserLogin::SHELL);
		}
		if (set_xdg) {
			prependpath("PATH", std::string(pw->pw_dir ? pw->pw_dir : "") + "/.local/bin");
			prependpath("MANPATH", std::string(pw->pw_dir ? pw->pw_dir : "") + "/.local/share/man");
			prependpath("MANPATH", std::string(pw->pw_dir ? pw->pw_dir : "") + "/.local/man");
			prependpath("TERMPATH", std::string(pw->pw_dir ? pw->pw_dir : "") + "/.config/termcap");
			prependpath("TERMINFO_DIRS", std::string(pw->pw_dir ? pw->pw_dir : "") + "/.config/terminfo");
		}
	} else {
		if (set_xdg) {
			envs.set("XDG_RUNTIME_DIR", nullptr);
			envs.set("XDG_DATA_DIRS", DefaultEnvironment::UserLogin::XDG_DATA_DIRS);
			envs.set("XDG_CONFIG_DIRS", DefaultEnvironment::UserLogin::XDG_CONFIG_DIRS);
			// XDG_CONFIG_HOME has a fallback of $HOME/.config which is fine.
			envs.set("XDG_CONFIG_HOME", nullptr);
			// XDG_DATA_HOME has a fallback of $HOME/.local/share which is fine.
			envs.set("XDG_DATA_HOME", nullptr);
			// XDG_STATE_HOME has a fallback of $HOME/.local/state which is fine.
			envs.set("XDG_STATE_HOME", nullptr);
			// XDG_CACHE_HOME has a fallback of $HOME/.cache which is fine.
			envs.set("XDG_CACHE_HOME", nullptr);
		}
		if (set_dbus)
			envs.set("DBUS_SESSION_BUS_ADDRESS", nullptr);
		if (default_locale) {
			default_str("LANG", DefaultEnvironment::UserLogin::LANG);
			default_str("MM_CHARSET", DefaultEnvironment::UserLogin::MM_CHARSET);
		}
		if (default_path) {
			// Superusers and non-superusers have the same default.
			default_str("PATH", DefaultEnvironment::UserLogin::PATH);
			default_str("TERMPATH", DefaultEnvironment::UserLogin::TERMPATH);
			default_str("TERMINFO_DIRS", DefaultEnvironment::UserLogin::TERMINFO_DIRS);
		}
		if (default_term)
			default_str("TERM", DefaultEnvironment::TERM);
		if (default_timezone)
			default_str("TZ", DefaultEnvironment::UserLogin::TZ);
		if (default_tools) {
			default_str("EDITOR", DefaultEnvironment::UserLogin::EDITOR);
			default_str("VISUAL", DefaultEnvironment::UserLogin::VISUAL);
			default_str("PAGER", pager);
			default_str("MANPAGER", manpager);
			default_str("MANXMLVIEWER", xml_viewer);
			default_str("WWWBROWSER", www_browser);
		}
		if (set_user) {
			envs.set("HOME", nullptr);
			envs.set("USER", nullptr);
			envs.set("LOGNAME", nullptr);
		}
		if (set_shell) {
			default_str("SHELL", DefaultEnvironment::UserLogin::SHELL);
		}
	}
}
