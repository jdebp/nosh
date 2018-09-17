/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_USER_ENVIRONMENT_SETTER_H)
#define INCLUDE_USER_ENVIRONMENT_SETTER_H

#include <pwd.h>
#include <string>

struct ProcessEnvironment;

/// \brief An actor for setting per-user environment variables.
struct UserEnvironmentSetter {
	UserEnvironmentSetter(ProcessEnvironment &);
	void apply(const passwd *);
	bool set_path;
	bool set_user;
	bool set_shell;
	bool set_tools;
	bool set_term;
	bool set_timezone;
	bool set_locale;
	bool set_dbus;
	bool set_xdg;
	bool set_other;
protected:
	struct LoginDBOwner;
	void set_if (const std::string & var, bool cond, const std::string & val) ;
	void set (const passwd * pw, const char * var, const char * val, bool is_path) ;
	void set_vec (const passwd * pw, const char * vec) ;
	void set_str (const char * var, const char * cap, const passwd * pw, LoginDBOwner & lc_system, LoginDBOwner & lc_user, const char * def) ;
	void set_pathstr (const char * var, const char * cap, const passwd * pw, LoginDBOwner & lc_system, LoginDBOwner & lc_user, const char * def) ;
	void set_vec (const char * cap, const passwd * pw, LoginDBOwner & lc) ;
	void set_vec (const char * cap, const passwd * pw, LoginDBOwner & lc_system, LoginDBOwner & lc_user) ;
	ProcessEnvironment & envs;
};

#endif
