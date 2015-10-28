/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstdlib>
#include <climits>
#include <unistd.h>
#include "listen.h"
#include "fdutils.h"

unsigned 
query_listen_fds()
{
	const int pid(getpid());
	if (0 > pid) {
daemontools_semantics:
		unsetenv("LISTEN_PID");
		unsetenv("LISTEN_FDS");
		set_close_on_exec(LISTEN_SOCKET_FILENO, true);
		return 1U;
	}
	const char * old;
	const char * listen_pid(std::getenv("LISTEN_PID"));
	if (!listen_pid) goto daemontools_semantics;
	old = listen_pid;
	unsigned long lp(std::strtoul(listen_pid, const_cast<char **>(&listen_pid), 0));
	if (*listen_pid || old == listen_pid) goto daemontools_semantics;
	if (static_cast<unsigned long>(pid) != lp) goto daemontools_semantics;
	const char * listen_fds(std::getenv("LISTEN_FDS"));
	if (!listen_fds) goto daemontools_semantics;
	old = listen_fds;
	unsigned long lf(std::strtoul(listen_fds, const_cast<char **>(&listen_fds), 0));
	if (*listen_fds || old == listen_fds) goto daemontools_semantics;
	if (UINT_MAX < lf) goto daemontools_semantics;
	for (unsigned i(0U); i < lf; ++i)
		set_close_on_exec(LISTEN_SOCKET_FILENO + i, true);
	unsetenv("LISTEN_PID");
	unsetenv("LISTEN_FDS");
	return lf;
}
