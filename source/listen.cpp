/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstdlib>
#include <climits>
#include <unistd.h>
#include "listen.h"
#include "fdutils.h"
#include "ProcessEnvironment.h"

unsigned 
query_listen_fds(ProcessEnvironment & envs)
{
	const int pid(getpid());
	if (0 > pid) {
daemontools_semantics:
		envs.unset("LISTEN_PID");
		envs.unset("LISTEN_FDS");
		set_close_on_exec(LISTEN_SOCKET_FILENO, true);
		return 1U;
	}
	const char * old;
	const char * listen_pid(envs.query("LISTEN_PID"));
	if (!listen_pid) goto daemontools_semantics;
	old = listen_pid;
	unsigned long lp(std::strtoul(listen_pid, const_cast<char **>(&listen_pid), 0));
	if (*listen_pid || old == listen_pid) goto daemontools_semantics;
	if (static_cast<unsigned long>(pid) != lp) goto daemontools_semantics;
	const char * listen_fds(envs.query("LISTEN_FDS"));
	if (!listen_fds) goto daemontools_semantics;
	old = listen_fds;
	unsigned long lf(std::strtoul(listen_fds, const_cast<char **>(&listen_fds), 0));
	if (*listen_fds || old == listen_fds) goto daemontools_semantics;
	if (UINT_MAX < lf) goto daemontools_semantics;
	for (unsigned i(0U); i < lf; ++i)
		set_close_on_exec(LISTEN_SOCKET_FILENO + i, true);
	envs.unset("LISTEN_PID");
	envs.unset("LISTEN_FDS");
	return static_cast<unsigned>(lf);
}

int 
query_listen_fds_passthrough(ProcessEnvironment & envs)
{
	int n(0U);
	const int pid(getpid());
	if (0 <= pid) {
		const char * old;
		const char * listen_pid(envs.query("LISTEN_PID"));
		if (!listen_pid) goto fail;
		old = listen_pid;
		unsigned long lp(std::strtoul(listen_pid, const_cast<char **>(&listen_pid), 0));
		if (*listen_pid || old == listen_pid) goto fail;
		if (static_cast<unsigned long>(pid) != lp) goto fail;
		const char * listen_fds(envs.query("LISTEN_FDS"));
		if (!listen_fds) goto fail;
		old = listen_fds;
		unsigned long lf(std::strtoul(listen_fds, const_cast<char **>(&listen_fds), 0));
		if (*listen_fds || old == listen_fds) goto fail;
		if (INT_MAX < lf) goto fail;
		n = static_cast<int>(lf);
	}
fail:
	envs.unset("LISTEN_PID");
	envs.unset("LISTEN_FDS");
	return n;
}
