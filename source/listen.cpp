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

static inline
bool 
query_listen_fds_internal(
	const ProcessEnvironment & envs, 
	unsigned & n
) {
	const char * old;
	const char * listen_pid(envs.query("LISTEN_PID"));
	if (!listen_pid) return false;
	old = listen_pid;
	unsigned long lp(std::strtoul(listen_pid, const_cast<char **>(&listen_pid), 0));
	if (*listen_pid || old == listen_pid) return false;
	const int pid(getpid());
	if (0 > pid) return false;
	if (static_cast<unsigned long>(pid) != lp) return false;
	const char * listen_fds(envs.query("LISTEN_FDS"));
	if (!listen_fds) return false;
	old = listen_fds;
	unsigned long lf(std::strtoul(listen_fds, const_cast<char **>(&listen_fds), 0));
	if (*listen_fds || old == listen_fds) return false;
	if (UINT_MAX < lf) return false;
	n = static_cast<unsigned>(lf);
	return true;
}

unsigned 
query_listen_fds(ProcessEnvironment & envs)
{
	unsigned n;
	const bool success(query_listen_fds_internal(envs, n));
	envs.unset("LISTEN_PID");
	envs.unset("LISTEN_FDS");
	// The application may still want to play with LISTEN_FDNAMES itself.
	if (!success) return 0U;
	for (unsigned i(0U); i < n; ++i)
		set_close_on_exec(LISTEN_SOCKET_FILENO + i, true);
	return n;
}

unsigned 
query_listen_fds_or_daemontools(ProcessEnvironment & envs)
{
	unsigned n;
	const bool success(query_listen_fds_internal(envs, n));
	envs.unset("LISTEN_PID");
	envs.unset("LISTEN_FDS");
	envs.unset("LISTEN_FDNAMES");	// We know that the application will be ignoring this.
	if (!success) n = 1UL;
	for (unsigned i(0U); i < n; ++i)
		set_close_on_exec(LISTEN_SOCKET_FILENO + i, true);
	return n;
}

int 
query_listen_fds_passthrough(ProcessEnvironment & envs)
{
	unsigned n;
	const bool success(query_listen_fds_internal(envs, n));
	envs.unset("LISTEN_PID");
	envs.unset("LISTEN_FDS");
	// LISTEN_FDNAMES passes through, of course.
	if (!success) return 0;
	return INT_MAX - 1 < n ? INT_MAX - 1 : static_cast<int>(n);
}
