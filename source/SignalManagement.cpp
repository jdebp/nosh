/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <csignal>
#include <cstdarg>
#include <unistd.h>
#include "SignalManagement.h"

// A way to set SIG_IGN that is reset by execve().
static void sig_ignore (int) {}

ReserveSignalsForKQueue::ReserveSignalsForKQueue(
	int signo, 
	...
) {
#if defined(__LINUX__) || defined(__linux__)
	sigset_t masked_signals;
	sigemptyset(&masked_signals);
	va_list l;
	for ( va_start(l, signo); signo > 0; signo = va_arg(l, int) )
		sigaddset(&masked_signals, signo);
	va_end(l);
	sigprocmask(SIG_BLOCK, &masked_signals, &original);
#else
	static_cast<void>(signo);	// silence compiler warning
#endif
}

ReserveSignalsForKQueue::~ReserveSignalsForKQueue()
{
#if defined(__LINUX__) || defined(__linux__)
	sigprocmask(SIG_SETMASK, &original, 0);
#endif
}

PreventDefaultForFatalSignals::PreventDefaultForFatalSignals(
	int signo, 
	...
) {
	sigemptyset(&signals);
	struct sigaction sa;
	sa.sa_flags=0;
	sa.sa_handler=sig_ignore;
	sigemptyset(&sa.sa_mask);
	va_list l;
	for ( va_start(l, signo); signo > 0; signo = va_arg(l, int) ) {
		sigaddset(&signals, signo);
		sigaction(signo,&sa,NULL);
	}
	va_end(l);
}

PreventDefaultForFatalSignals::~PreventDefaultForFatalSignals()
{
	struct sigaction sa;
	sa.sa_flags=0;
	sa.sa_handler=SIG_DFL;
	sigemptyset(&sa.sa_mask);
#if !defined(__LINUX__) && !defined(__linux__)
	for (int signo(1); signo < NSIG; ++signo)
#else
	for (int signo(1); signo < _NSIG; ++signo)
#endif
	{
		if (sigismember(&signals, signo))
			sigaction(signo,&sa,NULL);
	}
}
