/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include "common-manager.h"
#include "fdutils.h"

static const std::string slash("/");

std::string
effective_user_runtime_dir()
{
	std::string r("/run/user/");
#if defined(__LINUX__) || defined(__linux__)
	if (const char * l = cuserid(0)) {
		r += l;
	} else 
#endif
	{
		if (struct passwd * p = getpwuid(geteuid()))
			r += p->pw_passwd;
		endpwent();
	}
	return r + slash;
}

std::string
login_user_runtime_dir()
{
	std::string r("/run/user/");
#if defined(__LINUX__) || defined(__linux__)
	if (const char * l = getlogin()) {
		r += l;
	} else 
#endif
	if (const char * u = std::getenv("USER")) {
		r += u;
	} else
	if (const char * n = std::getenv("LOGNAME")) {
		r += n;
	} else
	{
		if (struct passwd * p = getpwuid(getuid()))
			r += p->pw_passwd;
		endpwent();
	}
	return r + slash;
}
