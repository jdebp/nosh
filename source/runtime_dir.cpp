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
#include "runtime-dir.h"
#include "fdutils.h"

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
login_user_runtime_dir()
{
	std::string r("/run/user/");
	if (const char * l = getlogin()) {
		r += l;
	} else 
	if (const char * u = std::getenv("USER")) {
		r += u;
	} else
	if (const char * n = std::getenv("LOGNAME")) {
		r += n;
	} else
	{
		if (struct passwd * p = getpwuid(getuid()))
			r += p->pw_name;
		endpwent();
	}
	return r + slash;
}
