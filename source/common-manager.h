/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_SYSTEM_MANAGER_H)
#define INCLUDE_SYSTEM_MANAGER_H

#include <vector>
#include <string>
#include <csignal>

struct api_symlink {
	int force;
	const char * name, * target;
};

extern const std::vector<api_symlink> api_symlinks;

struct iovec;

struct api_mount {
	const char * name;
	struct iovec * iov;
	unsigned int ioc;
	unsigned collection;
	int flags;
};

extern const std::vector<api_mount> api_mounts;

extern bool per_user_mode;	// Shared with the service manager client API.

// This signal tells process #1 to enter what systemd terms "emergency mode".
#if defined(SIGRTMIN)
// We go with systemd since BSD init, upstart, and System 5 init do not have a defined convention.
#define	EMERGENCY_SIGNAL	(SIGRTMIN + 2)
#else
// On OpenBSD we do the best of a bad job.
#define	EMERGENCY_SIGNAL	SIGIO
#endif

// This signal tells process #1 to spawn "system-control start sysinit".
#if defined(SIGRTMIN)
// BSD init, upstart, systemd, and System 5 init do not have a defined convention.
#define SYSINIT_SIGNAL	(SIGRTMIN + 10)
#else
// On OpenBSD we do the best of a bad job.
#define	SYSINIT_SIGNAL	SIGTTOU
#endif

// This signal tells process #1 to enter what BSD terms "single user mode" and what systemd terms "rescue mode".
#if defined(SIGRTMIN)
// We go with systemd since upstart and System 5 init do not have a defined convention.
#define RESCUE_SIGNAL	(SIGRTMIN + 1)
#else
// On OpenBSD we do the best of a bad job.
#define	RESCUE_SIGNAL	SIGURG
#endif

// This signal tells process #1 to enter what BSD terms "multi user mode" and what systemd terms "default mode".
#if defined(SIGRTMIN)
// On Linux and BSD, we go with systemd since upstart, BSD init, and System 5 init do not have a defined convention.
#define NORMAL_SIGNAL	(SIGRTMIN + 0)
#else
// On OpenBSD we do the best of a bad job.
#define NORMAL_SIGNAL	SIGTTIN
#endif

// This signal tells process #1 that the Secure Attention Key sequence has been pressed.
#if defined(__LINUX__) || defined(__linux__)
// On Linux, we go with systemd since upstart and System 5 init do not have a defined convention.
#define SAK_SIGNAL	SIGINT
#else
// On the BSDs, it is SIGINFO.
#define SAK_SIGNAL	SIGINFO
#endif

// This signal tells process #1 that the Keyboard Request sequence has been pressed.
#if defined(__LINUX__) || defined(__linux__)
// On Linux, the kernel has a defined convention.
#define KBREQ_SIGNAL	SIGWINCH
#else
// The BSDs have no convention.
#endif

// This signal tells process #1 to activate the reboot target.
#if defined(__LINUX__) || defined(__linux__)
// On Linux, we go with systemd since upstart and System 5 init do not have a defined convention.
#define REBOOT_SIGNAL	(SIGRTMIN + 5)
#else
// On the BSDs, it is SIGINT.
#define REBOOT_SIGNAL	SIGINT
#endif

// This signal tells process #1 to shut down and reboot.
#if defined(SIGRTMIN)
// BSD init, upstart, systemd, and System 5 init do not have a defined convention.
#define FORCE_REBOOT_SIGNAL	(SIGRTMIN + 15)
#else
// On OpenBSD we do the best of a bad job.
#define FORCE_REBOOT_SIGNAL	SIGTSTP
#endif

// This signal tells process #1 to activate the halt target.
#if defined(__LINUX__) || defined(__linux__)
// On Linux, we go with systemd since upstart and System 5 init do not have a defined convention.
#define HALT_SIGNAL	(SIGRTMIN + 3)
#else
// On the BSDs, it is SIGUSR1.
#define HALT_SIGNAL	SIGUSR1
#endif

// This signal tells process #1 to shut down and halt.
#if defined(SIGRTMIN)
// BSD init, upstart, systemd, and System 5 init do not have a defined convention.
#define FORCE_HALT_SIGNAL	(SIGRTMIN + 13)
#else
// And we cannot define this at all on OpenBSD!
#endif

// This signal tells process #1 to activate the powercycle target.
#if defined(__LINUX__) || defined(__linux__)
// On Linux, we go with systemd since upstart and System 5 init do not have a defined convention.
#define POWERCYCLE_SIGNAL	(SIGRTMIN + 7)
#elif defined(SIGRTMIN)
// On the BSDs that have real-time signals, it is SIGWINCH.
#define POWERCYCLE_SIGNAL	(SIGRTMIN + 7)
//#define POWERCYCLE_SIGNAL	SIGWINCH
#else
// On OpenBSD we do the best of a bad job.
#define POWERCYCLE_SIGNAL	SIGWINCH
#endif

// This signal tells process #1 to shut down and power cycle.
#if defined(SIGRTMIN)
// BSD init, upstart, systemd, and System 5 init do not have a defined convention.
#define FORCE_POWERCYCLE_SIGNAL	(SIGRTMIN + 17)
#else
// And we cannot define this at all on OpenBSD!
#endif

// This signal tells process #1 to activate the poweroff target.
#if defined(__LINUX__) || defined(__linux__)
// On Linux, we go with systemd since upstart and System 5 init do not have a defined convention.
#define POWEROFF_SIGNAL	(SIGRTMIN + 4)
#else
// On the BSDs, it is SIGUSR2.
#define POWEROFF_SIGNAL	SIGUSR2
#endif

// This signal tells process #1 to shut down and power down.
#if defined(SIGRTMIN)
// BSD init, upstart, systemd, and System 5 init do not have a defined convention.
#define FORCE_POWEROFF_SIGNAL	(SIGRTMIN + 14)
#else
// On OpenBSD we do the best of a bad job.
#define FORCE_POWEROFF_SIGNAL	SIGTERM
#endif

#endif
