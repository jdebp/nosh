/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include "runtime-dir.h"
#include "fdutils.h"
#include "ProcessEnvironment.h"

static const std::string slash("/");

std::string
effective_user_runtime_dir()
{
	std::string r("/run/user/");
	// Do not use cuserid() here.
	// BSD libc defines L_cuserid as 17.
	// But GNU libc is even worse and defines it as a mere 9.
	if (struct passwd * p = getpwuid(geteuid()))
		r += p->pw_name;
	endpwent();
	return r + slash;
}

std::string
login_user_runtime_dir(const ProcessEnvironment & envs)
{
	std::string r("/run/user/");
	if (const char * l = getlogin()) {
		r += l;
	} else 
	if (const char * u = envs.query("USER")) {
		r += u;
	} else
	if (const char * n = envs.query("LOGNAME")) {
		r += n;
	} else
	{
		if (struct passwd * p = getpwuid(getuid()))
			r += p->pw_name;
		endpwent();
	}
	return r + slash;
}

void
get_user_dirs_for(
	const std::string & user,
	std::string & UID,
	std::string & GID,
	std::string & runtime_dir_by_name,	///< not slash terminated
	std::string & runtime_dir_by_UID,	///< not slash terminated
	std::string & home_dir			///< not slash terminated
) {
	const std::string r("/run/user/");
	if (struct passwd * p = getpwnam(user.c_str())) {
		runtime_dir_by_name = r + p->pw_name;
		std::ostringstream uid, gid;
		uid << p->pw_uid;
		UID = uid.str();
		gid << p->pw_gid;
		GID = gid.str();
		runtime_dir_by_UID = r + ":" + uid.str();
		home_dir = p->pw_dir && *p->pw_dir ? p->pw_dir : "/nonexistent";
	} else {
		runtime_dir_by_name = r;
		runtime_dir_by_UID = r + ":";
		UID = "";
		GID = "";
		home_dir = "/nonexistent";
	}
	endpwent();
}
