/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#if !defined(__LINUX__) && !defined(__linux__)
#include <sys/event.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#else
#define _BSD_SOURCE 1
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <linux/kd.h>
#include <fcntl.h>
#include <mntent.h>
#include "FileStar.h"
#endif
#include <sys/mount.h>
#include <dirent.h>
#include <unistd.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <ctime>
#include <fstream>
#include "utils.h"
#include "fdutils.h"
#include "nmount.h"
#include "listen.h"
#include "popt.h"
#include "jail.h"
#include "common-manager.h"
#include "service-manager.h"

static int service_manager_pid(-1);
static int cyclog_pid(-1);
static int system_control_pid(-1);

static inline
std::string
concat (
	const std::vector<const char *> & args
) {
	std::string r;
	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		if (!r.empty()) r += ' ';
		r += *i;
	}
	return r;
}

/* State machine ************************************************************
// **************************************************************************
*/

static sig_atomic_t sysinit_signalled (false);
static sig_atomic_t init_signalled (true);
static sig_atomic_t normal_signalled (false);
static sig_atomic_t child_signalled (false);
static sig_atomic_t rescue_signalled (false);
static sig_atomic_t emergency_signalled (false);
static sig_atomic_t halt_signalled (false);
static sig_atomic_t poweroff_signalled (false);
static sig_atomic_t reboot_signalled (false);
static sig_atomic_t power_signalled (false);
static sig_atomic_t kbrequest_signalled (false);
static sig_atomic_t sak_signalled (false);
static sig_atomic_t fasthalt_signalled (false);
static sig_atomic_t fastpoweroff_signalled (false);
static sig_atomic_t fastreboot_signalled (false);
static sig_atomic_t unknown_signalled (false);
#define has_service_manager (-1 != service_manager_pid)
#define has_cyclog (-1 != cyclog_pid)
#define has_system_control (-1 != system_control_pid)
#define stop_signalled (fasthalt_signalled || fastpoweroff_signalled || fastreboot_signalled)

static inline
void
record_signal_system (
	int signo
) {
	switch (signo) {
		case SIGCHLD:		child_signalled = true; break;
#if !defined(__LINUX__) && !defined(__linux__)
		case SIGINT:		reboot_signalled = true; break;
		case SIGTERM:		rescue_signalled = true; break;
		case SIGUSR1:		halt_signalled = true; break;
		case SIGUSR2:		poweroff_signalled = true; break;
#else
		case SIGINT:		sak_signalled = true; break;
#endif
#if defined(SIGPWR)
		case SIGPWR:		power_signalled = true; break;
#endif
		case SIGWINCH:		kbrequest_signalled = true; break;
		default:
			if (SIGRTMIN <= signo) switch (signo - SIGRTMIN) {
				case 0:		normal_signalled = true; break;
				case 1:		rescue_signalled = true; break;
				case 2:		emergency_signalled = true; break;
				case 3:		halt_signalled = true; break;
				case 4:		poweroff_signalled = true; break;
				case 5:		reboot_signalled = true; break;
				case 10:	sysinit_signalled = true; break;
				case 13:	fasthalt_signalled = true; break;
				case 14:	fastpoweroff_signalled = true; break;
				case 15:	fastreboot_signalled = true; break;
				default:	unknown_signalled = true; break;
			} else
				unknown_signalled = true; 
			break;
	}
}

static inline
void
record_signal_user (
	int signo
) {
	switch (signo) {
		case SIGCHLD:		child_signalled = true; break;
		case SIGINT:		halt_signalled = true; break;
		case SIGTERM:		halt_signalled = true; break;
		case SIGHUP:		halt_signalled = true; break;
		case SIGPIPE:		halt_signalled = true; break;
		default:
			if (SIGRTMIN <= signo) switch (signo - SIGRTMIN) {
				case 0:		normal_signalled = true; break;
				case 1:		normal_signalled = true; break;
				case 2:		normal_signalled = true; break;
				case 3:		halt_signalled = true; break;
				case 4:		halt_signalled = true; break;
				case 5:		halt_signalled = true; break;
				case 10:	sysinit_signalled = true; break;
				case 13:	fasthalt_signalled = true; break;
				case 14:	fasthalt_signalled = true; break;
				case 15:	fasthalt_signalled = true; break;
				default:	unknown_signalled = true; break;
			} else
				unknown_signalled = true; 
			break;
	}
}

static void (*record_signal) ( int signo ) = 0;

// A way to set SIG_IGN that is reset by execve().
static void sig_ignore ( int ) {}

static inline
void
ignore_all_signals()
{
	// We use a sig_ignore function rather than SIG_IGN so that all signals are automatically reset to their default actions on execve().
	// GNU libc doesn't like us setting SIGRTMIN+0 and SIGRTMIN+1, but we don't care enough about error returns to notice.
	struct sigaction sa;
	sa.sa_flags=0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler=sig_ignore;
#if !defined(__LINUX__) && !defined(__linux__)
	for (int signo(1); signo < NSIG; ++signo)
		sigaction(signo,&sa,NULL);
	for (int signo(0); signo < 16; ++signo)
		sigaction(SIGRTMIN + signo,&sa,NULL);
#else
	for (int signo(1); signo < _NSIG; ++signo)
		sigaction(signo,&sa,NULL);
#endif

	// TODO: SIGPIPE, SIGTTIN, SIGTTOU, and SIGTSTP should be an inheritable SIG_IGN.
}

#if defined(__LINUX__) || defined(__linux__)
static
void
sig_handle (
	int  signo
) {
	record_signal(signo);
}
#endif

static inline
void
attach_signals_to_state_machine()
{
#if !defined(__LINUX__) && !defined(__linux__)
	// kevent() handles the signals.
#else
	struct sigaction sa;
	sa.sa_flags=0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler=sig_handle;
	sigaction(SIGCHLD,&sa,NULL);
	sigaction(SIGRTMIN + 0,&sa,NULL);
	sigaction(SIGRTMIN + 1,&sa,NULL);
	sigaction(SIGRTMIN + 2,&sa,NULL);
	sigaction(SIGRTMIN + 3,&sa,NULL);
	sigaction(SIGRTMIN + 4,&sa,NULL);
	sigaction(SIGRTMIN + 5,&sa,NULL);
#if defined(SIGPWR)
	sigaction(SIGPWR,&sa,NULL);
#endif
	sigaction(SIGWINCH,&sa,NULL);
	sigaction(SIGINT,&sa,NULL);
	sigaction(SIGRTMIN + 10,&sa,NULL);
	sigaction(SIGRTMIN + 11,&sa,NULL);
	sigaction(SIGRTMIN + 13,&sa,NULL);
	sigaction(SIGRTMIN + 14,&sa,NULL);
	sigaction(SIGRTMIN + 15,&sa,NULL);
#endif
}

static inline
void
default_all_signals()
{
	// GNU libc doesn't like us setting SIGRTMIN+0 and SIGRTMIN+1, but we don't care enough about error returns to notice.
	struct sigaction sa;
	sa.sa_flags=0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler=SIG_DFL;
#if !defined(__LINUX__) && !defined(__linux__)
	for (int signo(1); signo < NSIG; ++signo)
#else
	for (int signo(1); signo < _NSIG; ++signo)
#endif
		sigaction(signo,&sa,NULL);
}

/* File and directory names *************************************************
// **************************************************************************
*/

static
const char * 
env_files[] = {
	"/etc/locale.conf",
	"/etc/default/locale",
	"/etc/sysconfig/i18n",
	"/etc/sysconfig/language",
	"/etc/sysconf/i18n"
};

static
const char * 
manager_directories[] = {
	"/run/system-manager",
	"/run/system-manager/log",
	"/run/service-bundles",
	"/run/service-bundles/early-supervise",
	"/run/service-manager",
	"/run/user"
};

static
const struct api_symlink manager_symlinks[] = 
{
	// Compatibilitu with early supervise bundles from version 1.16 and earlier.
	{	"/run/system-manager/early-supervise",	"../service-bundles/early-supervise"		},
};

/* Utilities for the main program *******************************************
// **************************************************************************
*/

static inline
void
open_logging_pipe (
	const char * prog,
	int pipe_fds[2]
) {
	if (0 > pipe_close_on_exec (pipe_fds)) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "pipe", std::strerror(error));
	} else {
		// We must ensure that the pipe file descriptors are not in the standard+systemd file descriptors range, otherwise dup2() in child processes goes wrong later.
		for (unsigned i(0); i < 2; ++i) {
			while (pipe_fds[i] != -1 && LISTEN_SOCKET_FILENO >= pipe_fds[i]) {
				pipe_fds[i] = dup(pipe_fds[i]);
				set_close_on_exec(pipe_fds[i], true);
			}
		}
		dup2(pipe_fds[1], STDOUT_FILENO);
		dup2(pipe_fds[1], STDERR_FILENO);
	}
}

static inline
void
setup_process_state(
	const bool is_system,
	const char * prog
) {
	if (is_system) {
		setsid();
#if !defined(__LINUX__) && !defined(__linux__)
		setlogin("root");
#endif
		chdir("/");
		umask(0);

		// We cannot omit /sbin and /bin from the path because we cannot reliably detect that they duplicate /usr/bin and /usr/sbin at this point.
		// On some systems, /usr/sbin and /usr/bin are the symbolic links, and don't exist until we have mounted /usr .
		// On other systems, /sbin and /bin are the symbolic links, but /usr isn't a mount point and everything is on the root volume.
		setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
		setenv("LANG", "C", 1);

		// parse /etc/locale.d as if by envdir.
		const int scan_dir_fd(open_dir_at(AT_FDCWD, "/etc/locale.d"));
		if (0 > scan_dir_fd) {
			const int error(errno);
			if (ENOENT != error)
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "/etc/locale.d", std::strerror(error));
		} else
			process_env_dir(prog, "/etc/locale.d", scan_dir_fd, true /*ignore errors*/, false /*first lines only*/, false /*no chomping*/);

		for (std::size_t fi(0); fi < sizeof env_files/sizeof *env_files; ++fi) {
			const char * filename(env_files[fi]);
			FILE * f(std::fopen(filename, "r"));
			if (!f) {
				const int error(errno);
				if (ENOENT != error)
					std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, filename, std::strerror(error));
				continue;
			}
			try {
				std::vector<std::string> env_strings(read_file(f));
				std::fclose(f);
				f = 0;
				for (std::vector<std::string>::const_iterator i(env_strings.begin()); i != env_strings.end(); ++i) {
					const std::string & s(*i);
					const std::string::size_type p(s.find('='));
					const std::string var(s.substr(0, p));
					const std::string val(p == std::string::npos ? std::string() : s.substr(p + 1, std::string::npos));
					setenv(var.c_str(), val.c_str(), 1);
				}
				break;
			} catch (const char * r) {
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, filename, r);
			}
			if (f)
				std::fclose(f);
		}
	} else {
		subreaper(true);
	}
}

static inline
bool
hwclock_runs_in_UTC()
{
#if defined(__LINUX__) || defined(__linux__)
	std::ifstream i("/etc/adjtime");
	if (i.fail()) return true;
	char buf[100];
	i.getline(buf, sizeof buf, '\n');
	i.getline(buf, sizeof buf, '\n');
	i.getline(buf, sizeof buf, '\n');
	if (i.fail()) return true;
	return 0 != std::strcmp("LOCAL", buf);
#else
	int oid[CTL_MAXNAME];
	std::size_t len = sizeof oid/sizeof *oid;
	int local(0);			// It is important that this is an int.
	std::size_t siz = sizeof local;

	sysctlnametomib("machdep.wall_cmos_clock", oid, &len);
	sysctl(oid, len, &local, &siz, 0, 0);
	if (local) return true;

	return 0 > access("/etc/wall_cmos_clock", F_OK);
#endif
}

static inline
void
initialize_system_clock_timezone(
	const char * prog
) {
	struct timezone tz = { 0, 0 };
	const struct timeval * ztv(0);		// This works around a compiler warning.
	const bool utc(hwclock_runs_in_UTC());
	const std::time_t now(std::time(0));
	const struct tm *l(localtime(&now));
	const int seconds_west(-l->tm_gmtoff);	// It is important that this is an int.

#if defined(__LINUX__) || defined(__linux__)
	(void)prog;	// Silences a compiler warning about an unused parameter in this branch of the conditional compilation.
	if (utc)
		settimeofday(ztv, &tz);	// Prevent the next call from adjusting the system clock.
	// Set the RTC/FAT local time offset, and (if not UTC) adjust the system clock from local-time-as-if-UTC to UTC.
	tz.tz_minuteswest = seconds_west / 60;
	settimeofday(ztv, &tz);		
#else
	if (!utc) {
		std::size_t siz;

		int disable_rtc_set(0), old_disable_rtc_set;
		int wall_cmos_clock(!utc), old_wall_cmos_clock;
		int old_seconds_west;

		siz = sizeof old_disable_rtc_set;
		sysctlbyname("machdep.disable_rtc_set", &old_disable_rtc_set, &siz, &disable_rtc_set, sizeof disable_rtc_set);

		siz = sizeof old_wall_cmos_clock;
		sysctlbyname("machdep.wall_cmos_clock", &old_wall_cmos_clock, &siz, &wall_cmos_clock, sizeof wall_cmos_clock);

		siz = sizeof old_seconds_west;
		sysctlbyname("machdep.adjkerntz", &old_seconds_west, &siz, &seconds_west, sizeof seconds_west);

		if (!old_wall_cmos_clock) old_seconds_west = 0;

		if (disable_rtc_set != old_disable_rtc_set)
			sysctlbyname("machdep.disable_rtc_set", 0, 0, &old_disable_rtc_set, sizeof old_disable_rtc_set);

		// Adjust the system clock from local-time-as-if-UTC to UTC, and zero out the tz_minuteswest if it is non-zero.
		struct timeval tv = { 0, 0 };
		gettimeofday(&tv, 0);
		tv.tv_sec += seconds_west - old_seconds_west;
		settimeofday(&tv, &tz);

		if (seconds_west != old_seconds_west)
			std::fprintf(stderr, "%s: WARNING: Timezone wrong.  Please put machdep.adjkerntz=%i and machdep.wall_cmos_clock=1 in loader.conf.\n", prog, seconds_west);
	} else
		// Zero out the tz_minuteswest if it is non-zero.
		settimeofday(ztv, &tz);
#endif
}

static inline
int
update_flag ( 
	bool update 
) {
	return !update ? 0 :
#if defined(__LINUX__) || defined(__linux__)
		MS_REMOUNT
#else
		MNT_UPDATE
#endif
	;
}

static inline
bool
is_already_mounted (
	const std::string & fspath
) {
	struct stat b;
	if (0 <= stat(fspath.c_str(), &b)) {
		// This is traditional, and what FreeBSD/PC-BSD does.
		// On-disc volumes on Linux mostly do this, too.
		if (2 == b.st_ino)
			return true;
#if defined(__LINUX__) || defined(__linux__)
		// Some virtual volumes on Linux do this, instead.
		if (1 == b.st_ino)
			return true;
#endif
	}
#if defined(__LINUX__) || defined(__linux__)
	// We're going to have to check this the long way around.
	FileStar f(setmntent("/proc/self/mounts", "r"));
	if (f.operator FILE *()) {
		while (struct mntent * m = getmntent(f)) {
			if (fspath == m->mnt_dir)
				return true;
		}
	}
#endif
	return false;
}

static inline
void
setup_kernel_api_volumes_and_devices(
	const char * prog
) {
	for (std::vector<api_mount>::const_iterator i(api_mounts.begin()); api_mounts.end() != i; ++i) {
		const std::string fspath(fspath_from_mount(i->iov, i->ioc));
		bool update(false);
		if (!fspath.empty()) {
			if (0 > mkdir(fspath.c_str(), 0700)) {
				const int error(errno);
				if (EEXIST != error)
					std::fprintf(stderr, "%s: ERROR: %s: %s: %s\n", prog, "mkdir", fspath.c_str(), std::strerror(error));
			}
			update = is_already_mounted(fspath);
			if (update)
				std::fprintf(stderr, "%s: INFO: %s: %s\n", prog, fspath.c_str(), "A volume is already mounted here.");
		}
		if (0 > nmount(i->iov, i->ioc, i->flags | update_flag(update))) {
			const int error(errno);
			if (EBUSY != error)
				std::fprintf(stderr, "%s: ERROR: %s: %s: %s\n", prog, "nmount", i->name, std::strerror(error));
		}
	}
	for (std::vector<api_symlink>::const_iterator i(api_symlinks.begin()); api_symlinks.end() != i; ++i) {
		if (0 > symlink(i->target, i->name)) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s: %s\n", prog, "symlink", i->name, std::strerror(error));
		}
	}
}

static inline
void
make_needed_run_directories(
	const char * prog
) {
	for (std::size_t fi(0); fi < sizeof manager_directories/sizeof *manager_directories; ++fi) {
		const char * dirname(manager_directories[fi]);
		if (0 > mkdir(dirname, 0755)) {
			const int error(errno);
			if (EEXIST != error)
				std::fprintf(stderr, "%s: ERROR: %s: %s: %s\n", prog, "mkdir", dirname, std::strerror(error));
		}
	}
	for (std::size_t fi(0); fi < sizeof manager_symlinks/sizeof *manager_symlinks; ++fi) {
		const api_symlink * const i(manager_symlinks + fi);
		if (0 > symlink(i->target, i->name)) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s: %s\n", prog, "symlink", i->name, std::strerror(error));
		}
	}
}

static inline
int
open_null_as_stdin(
	const char * prog
) {
	const int dev_null_fd(open_read_at(AT_FDCWD, "/dev/null"));
	if (0 > dev_null_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "/dev/null", std::strerror(error));
	} else {
		set_non_blocking(dev_null_fd, false);
		dup2(dev_null_fd, STDIN_FILENO);
	}
	return dev_null_fd;
}

static inline
int
open_console(
	const char * prog
) {
	const int dev_console_fd(open_readwriteexisting_at(AT_FDCWD, "/dev/console"));
	if (0 > dev_console_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "/dev/console", std::strerror(error));
	} else
		set_non_blocking(dev_console_fd, false);
	return dev_console_fd;
}

static inline
int
open_tty(
	const char * prog
) {
	const int dev_tty_fd(open_readwriteexisting_at(AT_FDCWD, "/dev/tty"));
	if (0 > dev_tty_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "/dev/tty", std::strerror(error));
	} else
		set_non_blocking(dev_tty_fd, false);
	return dev_tty_fd;
}

static inline
void
request_system_events(
	const int dev_console_fd
) {
#if defined(__LINUX__) || defined(__linux__)
	if (0 <= dev_console_fd)
		ioctl(dev_console_fd, KDSIGACCEPT, SIGWINCH);
	reboot(RB_DISABLE_CAD);
#else
	static_cast<void>(dev_console_fd);	// Silences a compiler warning.
#endif
}

static inline
void
end_system()
{
#if defined(__LINUX__) || defined(__linux__)
	sync();		// The BSD reboot system call already implies a sync() unless RB_NOSYNC is used.
#endif
	if (fastpoweroff_signalled) {
#if !defined(__LINUX__) && !defined(__linux__)
		reboot(RB_POWEROFF);
#else
		reboot(RB_POWER_OFF);
#endif
	}
	if (fasthalt_signalled) {
#if !defined(__LINUX__) && !defined(__linux__)
		reboot(RB_HALT);
#else
		reboot(RB_HALT_SYSTEM);
#endif
	}
	if (fastreboot_signalled) {
		reboot(RB_AUTOBOOT);
	}
	reboot(RB_AUTOBOOT);
}

static
const char *
system_manager_roots[] = {
	"/var/log/system-manager",
	"/var/system-manager/log",
	"/var/tmp/system-manager/log",
	"/run/system-manager/log"
};

static inline
void
change_to_system_manager_log_root (
	const bool is_system
) {
	if (is_system) {
		for (const char ** r(system_manager_roots); r < system_manager_roots + sizeof system_manager_roots/sizeof *system_manager_roots; ++r)
			if (0 <= chdir(*r))
				return;
	} else
		chdir((login_user_runtime_dir() + "system-manager/log").c_str());
}

static inline
const char *
construct_system_manager_log_name (
	const bool is_system,
	std::string & name_buf
) {
#if 0
	if (is_system) return ".";
	name_buf = login_user_runtime_dir() + "log.";
	const int id(query_manager_pid(is_system));
	char idbuf[64];
	snprintf(idbuf, sizeof idbuf, "%d", id);
	name_buf += idbuf;
	return name_buf.c_str();
#else
	return ".";
#endif
}

/* Main program *************************************************************
// **************************************************************************
*/

static
void
common_manager ( 
	const bool is_system,
	const char * & next_prog,
	std::vector<const char *> & args
) {
	record_signal = (is_system ? record_signal_system : record_signal_user);

	const char * prog(basename_of(args[0]));
	args.erase(args.begin());

	// We cannot open /dev/console until we've mounted /dev; but we need somewhere to record errors mounting things.
	// Linux runs process #1 with standard input, output, and error already attached to a TTY; but BSD does not.
	// On the gripping hand, we don't want our output cluttering a TTY; so we use a pipe.
	// We must be careful about not writing too much to this pipe without a running cyclog process.
	int pipe_fds[2] = { -1, -1 };
	open_logging_pipe(prog, pipe_fds);

	setup_process_state(is_system, prog);
	ignore_all_signals();
	if (is_system) {
		initialize_system_clock_timezone(prog);
		setup_kernel_api_volumes_and_devices(prog);
		make_needed_run_directories(prog);
	}

	const int dev_null_fd(open_null_as_stdin(prog));
	const int dev_console_fd((is_system ? open_console : open_tty)(prog));
	if (is_system)
		request_system_events(dev_console_fd);

	const int service_manager_socket_fd(listen_service_manager_socket(is_system, prog));

#if defined(DEBUG)
	if (is_system) {
		const int shell(fork());
		if (-1 == shell) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
		} else if (0 == shell) {
			setsid();
			args.clear();
			args.insert(args.end(), "/bin/sh");
			args.insert(args.end(), 0);
			next_prog = arg0_of(args);
			dup2(dev_console_fd, STDIN_FILENO);
			dup2(dev_console_fd, STDOUT_FILENO);
			dup2(dev_console_fd, STDERR_FILENO);
			close(LISTEN_SOCKET_FILENO);
			close(pipe_fds[0]);
			close(pipe_fds[1]);
			close(dev_null_fd);
			close(dev_console_fd);
			close(service_manager_socket_fd);
			return;
		}
	}
#endif

#if !defined(__LINUX__) && !defined(__linux__)
	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		return;
	}

	std::vector<struct kevent> p(24);
	unsigned n(0);
	EV_SET(&p[n++], SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[n++], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[n++], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	if (is_system) {
	EV_SET(&p[n++], SIGUSR1, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[n++], SIGUSR2, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
#if defined(SIGPWR)
	EV_SET(&p[n++], SIGPWR, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
#endif
	EV_SET(&p[n++], SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	} else {
	EV_SET(&p[n++], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[n++], SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	}
	EV_SET(&p[n++], SIGRTMIN +  0, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	if (is_system) {
	EV_SET(&p[n++], SIGRTMIN +  1, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[n++], SIGRTMIN +  2, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	}
	EV_SET(&p[n++], SIGRTMIN +  3, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[n++], SIGRTMIN +  4, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[n++], SIGRTMIN +  5, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[n++], SIGRTMIN + 10, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[n++], SIGRTMIN + 11, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[n++], SIGRTMIN + 13, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[n++], SIGRTMIN + 14, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(&p[n++], SIGRTMIN + 15, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	if (0 > kevent(queue, p.data(), n, 0, 0, 0)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
	}
#else
	sigset_t original_signals, masked_signals;
	sigfillset(&masked_signals);
	sigprocmask(SIG_SETMASK, &masked_signals, &original_signals);
	sigset_t masked_signals_during_poll(masked_signals);
	sigdelset(&masked_signals_during_poll, SIGCHLD);
	sigdelset(&masked_signals_during_poll, SIGINT);
	if (is_system) {
#if defined(SIGPWR)
	sigdelset(&masked_signals_during_poll, SIGPWR);
#endif
	} else {
	sigdelset(&masked_signals_during_poll, SIGHUP);
	sigdelset(&masked_signals_during_poll, SIGPIPE);
	}
	sigdelset(&masked_signals_during_poll, SIGWINCH);
	sigdelset(&masked_signals_during_poll, SIGRTMIN + 0);
	if (is_system) {
	sigdelset(&masked_signals_during_poll, SIGRTMIN + 1);
	sigdelset(&masked_signals_during_poll, SIGRTMIN + 2);
	}
	sigdelset(&masked_signals_during_poll, SIGRTMIN + 3);
	sigdelset(&masked_signals_during_poll, SIGRTMIN + 4);
	sigdelset(&masked_signals_during_poll, SIGRTMIN + 5);
	if (is_system) {
	sigdelset(&masked_signals_during_poll, SIGRTMIN + 10);
	sigdelset(&masked_signals_during_poll, SIGRTMIN + 11);
	}
	sigdelset(&masked_signals_during_poll, SIGRTMIN + 13);
	sigdelset(&masked_signals_during_poll, SIGRTMIN + 14);
	sigdelset(&masked_signals_during_poll, SIGRTMIN + 15);
#endif

	attach_signals_to_state_machine();

	{
		char pid[64];
		snprintf(pid, sizeof pid, "%u", getpid());
		setenv("MANAGER_PID", pid, 1);
	}

	for (;;) {
		if (child_signalled) {
			for (;;) {
				int status;
				const pid_t c(waitpid(-1, &status, WNOHANG));
				if (c <= 0) break;
				if (c == service_manager_pid) {
					std::fprintf(stderr, "%s: WARNING: %s (pid %i) ended status %i\n", prog, "service-manager", c, status);
					service_manager_pid = -1;
				} else
				if (c == cyclog_pid) {
					std::fprintf(stderr, "%s: WARNING: %s (pid %i) ended status %i\n", prog, "cyclog", c, status);
					cyclog_pid = -1;
					// If cyclog abended, throttle respawns.
					if (WIFSIGNALED(status) || (WIFEXITED(status) && 0 != WEXITSTATUS(status))) {
						timespec t;
						t.tv_sec = 0;
						t.tv_nsec = 500000000; // 0.5 second
						// If someone sends us a signal to do something, this will be interrupted.
						nanosleep(&t, 0);
					}
				} else
				if (c == system_control_pid) {
					std::fprintf(stderr, "%s: INFO: %s (pid %i) ended status %i\n", prog, "system-control", c, status);
					system_control_pid = -1;
				}
			}
			child_signalled = false;
		}
		// Run system-control if a job is pending and system-control isn't already running.
		if (!has_system_control) {
			const char * subcommand(0), * option(0);
			bool verbose(true);
			if (sysinit_signalled) {
				subcommand = "start";
				option = "sysinit";
				sysinit_signalled = false;
			} else
			if (normal_signalled) {
				subcommand = "start";
				option = "normal";
				normal_signalled = false;
			} else
			if (rescue_signalled) {
				subcommand = "start";
				option = "rescue";
				rescue_signalled = false;
			} else
			if (emergency_signalled) {
				subcommand = "activate";
				option = "emergency";
				emergency_signalled = false;
			} else
			if (halt_signalled) {
				subcommand = "start";
				option = "halt";
				halt_signalled = false;
			} else
			if (poweroff_signalled) {
				subcommand = "start";
				option = "poweroff";
				poweroff_signalled = false;
			} else
			if (reboot_signalled) {
				subcommand = "start";
				option = "reboot";
				reboot_signalled = false;
			} else
			if (power_signalled) {
				subcommand = "activate";
				option = "powerfail";
				power_signalled = false;
			} else
			if (kbrequest_signalled) {
				subcommand = "activate";
				option = "kbrequest";
				kbrequest_signalled = false;
			} else
			if (sak_signalled) {
				subcommand = "activate";
				option = "secure-attention-key";
				sak_signalled = false;
			}
			if (subcommand) {
				system_control_pid = fork();
				if (-1 == system_control_pid) {
					const int error(errno);
					std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
				} else if (0 == system_control_pid) {
#if defined(__LINUX__) || defined(__linux__)
					sigprocmask(SIG_SETMASK, &original_signals, &masked_signals);
#endif
					default_all_signals();
					alarm(180);
					// Replace the original arguments with this.
					args.clear();
					args.insert(args.end(), "system-control");
					args.insert(args.end(), subcommand);
					if (verbose)
						args.insert(args.end(), "--verbose");
					if (!is_system)
						args.insert(args.end(), "--user");
					if (option)
						args.insert(args.end(), option);
					args.insert(args.end(), 0);
					next_prog = arg0_of(args);
					return;
				} else
					std::fprintf(stderr, "%s: INFO: %s (pid %i) started (%s %s)\n", prog, "system-control", system_control_pid, subcommand, option ? option : "");
			}
		}
		if (!has_system_control) {
			if (init_signalled) {
				init_signalled = false;
				system_control_pid = fork();
				if (-1 == system_control_pid) {
					const int error(errno);
					std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
				} else if (0 == system_control_pid) {
#if defined(__LINUX__) || defined(__linux__)
					sigprocmask(SIG_SETMASK, &original_signals, &masked_signals);
#endif
					default_all_signals();
					alarm(420);
					// Retain the original arguments and insert the following in front of them.
					if (!is_system)
						args.insert(args.begin(), "--user");
					args.insert(args.begin(), "init");
					args.insert(args.begin(), "system-control");
					next_prog = arg0_of(args);
					return;
				} else
					std::fprintf(stderr, "%s: INFO: %s (pid %i) started (%s %s)\n", prog, "system-control", system_control_pid, "init", concat(args).c_str());
			}
		}
		// Exit if stop has been signalled and both the service manager and logger have exited.
		if (stop_signalled && !has_cyclog && !has_service_manager) break;
		// Kill the service manager if stop has been signalled.
		if (has_service_manager && stop_signalled && !has_system_control) {
			std::fprintf(stderr, "%s: DEBUG: %s\n", prog, "terminating service manager");
			kill(service_manager_pid, SIGTERM);
		}
		// Restart the logger unless both stop has been signalled and the service manager has exited.
		// If the service manager has not exited and stop has been signalled, we still need the logger to restart and keep draining the pipe.
		if (!has_cyclog && (!stop_signalled || has_service_manager)) {
			cyclog_pid = fork();
			if (-1 == cyclog_pid) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
			} else if (0 == cyclog_pid) {
				change_to_system_manager_log_root(is_system);
				std::string name_buf;
				const char * log_name(construct_system_manager_log_name(is_system, name_buf));
#if defined(__LINUX__) || defined(__linux__)
				sigprocmask(SIG_SETMASK, &original_signals, &masked_signals);
#endif
				if (is_system)
					setsid();
				default_all_signals();
				args.clear();
				args.insert(args.end(), "cyclog");
				args.insert(args.end(), "--max-file-size");
				args.insert(args.end(), "32768");
				args.insert(args.end(), "--max-total-size");
				args.insert(args.end(), "1048576");
				args.insert(args.end(), log_name);
				args.insert(args.end(), 0);
				next_prog = arg0_of(args);
				if (-1 != pipe_fds[0]) dup2(pipe_fds[0], STDIN_FILENO);
				dup2(dev_console_fd, STDOUT_FILENO);
				dup2(dev_console_fd, STDERR_FILENO);
				close(LISTEN_SOCKET_FILENO);
				close(pipe_fds[0]);
				close(pipe_fds[1]);
				close(dev_null_fd);
				close(dev_console_fd);
				close(service_manager_socket_fd);
				return;
			} else
				std::fprintf(stderr, "%s: INFO: %s (pid %i) started\n", prog, "cyclog", cyclog_pid);
		}
		// If the service manager has exited and stop has been signalled, close the logging pipe so that the logger finally exits.
		if (!has_service_manager && stop_signalled && -1 != pipe_fds[0]) {
			std::fprintf(stderr, "%s: DEBUG: %s\n", prog, "closing logger");
			dup2(dev_console_fd, STDIN_FILENO);
			dup2(dev_console_fd, STDOUT_FILENO);
			dup2(dev_console_fd, STDERR_FILENO);
			close(LISTEN_SOCKET_FILENO);
			close(pipe_fds[0]);
			close(pipe_fds[1]);
			pipe_fds[0] = pipe_fds[1] = -1;
		}
		// Restart the service manager unless stop has been signalled.
		if (!has_service_manager && !stop_signalled) {
			service_manager_pid = fork();
			if (-1 == service_manager_pid) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
			} else if (0 == service_manager_pid) {
#if defined(__LINUX__) || defined(__linux__)
				sigprocmask(SIG_SETMASK, &original_signals, &masked_signals);

				// Linux's default file handle limit of 1024 is far too low for normal usage patterns.
				const rlimit file_limit = { 16384U, 16384U };
				setrlimit(RLIMIT_NOFILE, &file_limit);
#endif
				if (is_system)
					setsid();
				default_all_signals();
				args.clear();
				args.insert(args.end(), "service-manager");
				args.insert(args.end(), 0);
				next_prog = arg0_of(args);
				dup2(dev_null_fd, STDIN_FILENO);
				if (-1 != pipe_fds[1]) dup2(pipe_fds[1], STDOUT_FILENO);
				if (-1 != pipe_fds[1]) dup2(pipe_fds[1], STDERR_FILENO);
				dup2(service_manager_socket_fd, LISTEN_SOCKET_FILENO);
				close(pipe_fds[0]);
				close(pipe_fds[1]);
				close(dev_null_fd);
				close(dev_console_fd);
				close(service_manager_socket_fd);
				return;
			} else
				std::fprintf(stderr, "%s: INFO: %s (pid %i) started\n", prog, "service-manager", service_manager_pid);
		}
		if (unknown_signalled) {
			std::fprintf(stderr, "%s: WARNING: %s\n", prog, "Unknown signal ignored.");
			unknown_signalled = false;
		}
#if !defined(__LINUX__) && !defined(__linux__)
		const int rc(kevent(queue, p.data(), 0, p.data(), p.size(), 0));
#else
		const int rc(sigsuspend(&masked_signals_during_poll));
#endif
		if (0 > rc) {
			if (EINTR == errno) continue;
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
			return;
		}
#if !defined(__LINUX__) && !defined(__linux__)
		for (size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			if (EVFILT_SIGNAL == p[i].filter)
				record_signal(p[i].ident);
		}
#endif
	}

	if (is_system) {
		sync();
		if (!am_in_jail()) 
			end_system();
	}
	throw EXIT_SUCCESS;
}

void
session_manager ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const bool is_system(1 == getpid());
	common_manager(is_system, next_prog, args);
}

void
system_manager ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const bool is_system(1 == getpid());
	common_manager(is_system, next_prog, args);
}
