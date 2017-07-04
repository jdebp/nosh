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
#include "home-dir.h"
#include "ProcessEnvironment.h"

std::string
effective_user_home_dir (const ProcessEnvironment & envs)
{
	if (const char * h = envs.query("HOME")) {
		return h;
	} else
	{
		if (struct passwd * p = getpwuid(geteuid()))
			if (p->pw_dir) {
				const std::string d(p->pw_dir);
				endpwent();
				return d;
			}
		endpwent();
	}
	return "/dev/null/";	// Yes, the final slash is intentional.
}
