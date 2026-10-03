/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <vector>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "kqueue_common.h"
#if defined(__LINUX__) || defined(__linux__)
#include <sys/resource.h>
#include <linux/kd.h>
#include <fcntl.h>
#include <mntent.h>
#include <sys/vt.h>
#else
#include <sys/sysctl.h>
#endif
#include <sys/ioctl.h>
#include <sys/uio.h>
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
#include "ProcessEnvironment.h"
#include "DefaultEnvironment.h"
#include "nmount.h"
#include "listen.h"
#include "popt.h"
#include "jail.h"
#include "runtime-dir.h"
#include "log_dir.h"
#include "common-manager.h"
#include "service-manager-client.h"
#include "FileStar.h"
#include "FileDescriptorOwner.h"
#include "SignalManagement.h"
#include "control_groups.h"

/* State machine ************************************************************
// **************************************************************************
*/

namespace {

pid_t service_manager_pid(-1);
pid_t cyclog_pid(-1);
pid_t regular_system_control_pid(-1);
pid_t emergency_system_control_pid(-1);
pid_t kbreq_system_control_pid(-1);
inline bool has_service_manager() { return static_cast<pid_t>(-1) != service_manager_pid; }
inline bool has_cyclog() { return static_cast<pid_t>(-1) != cyclog_pid; }
inline bool has_regular_system_control() { return static_cast<pid_t>(-1) != regular_system_control_pid; }
inline bool has_emergency_system_control() { return static_cast<pid_t>(-1) != emergency_system_control_pid; }
inline bool has_kbreq_system_control() { return static_cast<pid_t>(-1) != kbreq_system_control_pid; }
inline bool has_any_system_control() { return has_regular_system_control() || has_emergency_system_control() || has_kbreq_system_control(); }

sig_atomic_t sysinit_signalled (false);
sig_atomic_t init_signalled (true);
sig_atomic_t restart_logger1_signalled (false);
sig_atomic_t restart_logger2_signalled (false);
sig_atomic_t restart_logger3_signalled (false);
sig_atomic_t normal_signalled (false);
sig_atomic_t child_signalled (false);
sig_atomic_t rescue_signalled (false);
sig_atomic_t emergency_signalled (false);
sig_atomic_t halt_signalled (false);
sig_atomic_t poweroff_signalled (false);
sig_atomic_t powercycle_signalled (false);
sig_atomic_t reboot_signalled (false);
sig_atomic_t power_signalled (false);
sig_atomic_t kbrequest_signalled (false);
sig_atomic_t sak_signalled (false);
sig_atomic_t fasthalt_signalled (false);
sig_atomic_t fastpoweroff_signalled (false);
sig_atomic_t fastpowercycle_signalled (false);
sig_atomic_t fastreboot_signalled (false);
sig_atomic_t unknown_signalled (false);
inline bool stop_signalled() { return fasthalt_signalled || fastpoweroff_signalled || fastpowercycle_signalled || fastreboot_signalled; }
inline bool restart_logger_signalled() { return restart_logger1_signalled || restart_logger2_signalled || restart_logger3_signalled; }

inline
void
record_signal_system (
	int signo
) {
	switch (signo) {
		case SIGCHLD:		child_signalled = true; break;
#if defined(KBREQ_SIGNAL)
		case KBREQ_SIGNAL:	kbrequest_signalled = true; break;
#endif
#if defined(SAK_SIGNAL)
		case SAK_SIGNAL:	sak_signalled = true; break;
#endif
#if defined(SIGPWR)
		case SIGPWR:		power_signalled = true; break;
#endif
#if !defined(__LINUX__) && !defined(__linux__)
		case POWEROFF_SIGNAL:	poweroff_signalled = true; break;
#	if defined(POWERCYCLE_SIGNAL)
		case POWERCYCLE_SIGNAL:	powercycle_signalled = true; break;
#	endif
		case HALT_SIGNAL:	halt_signalled = true; break;
		case REBOOT_SIGNAL:	reboot_signalled = true; break;
#endif
#if !defined(SIGRTMIN)
		case EMERGENCY_SIGNAL:	emergency_signalled = true; break;
		case RESCUE_SIGNAL:	rescue_signalled = true; break;
		case NORMAL_SIGNAL:	normal_signalled = true; break;
		case SYSINIT_SIGNAL:	sysinit_signalled = true; break;
		case FORCE_REBOOT_SIGNAL:	fastreboot_signalled = true; break;
		case FORCE_POWEROFF_SIGNAL:	fastpoweroff_signalled = true; break;
#	if defined(FORCE_POWERCYCLE_SIGNAL)
		case FORCE_POWERCYCLE_SIGNAL:	fastpowercycle_signalled = true; break;
#	endif
#endif
		default:
#if defined(SIGRTMIN)
			if (SIGRTMIN <= signo) switch (signo - SIGRTMIN) {
				case 0:		normal_signalled = true; break;
				case 1:		rescue_signalled = true; break;
				case 2:		emergency_signalled = true; break;
				case 3:		halt_signalled = true; break;
				case 4:		poweroff_signalled = true; break;
				case 5:		reboot_signalled = true; break;
				// 6 is kexec
				case 7:		powercycle_signalled = true; break;
				case 10:	sysinit_signalled = true; break;
				case 13:	fasthalt_signalled = true; break;
				case 14:	fastpoweroff_signalled = true; break;
				case 15:	fastreboot_signalled = true; break;
				// 16 is kexec
				case 17:	fastpowercycle_signalled = true; break;
				case 26:	restart_logger1_signalled = true; break;
				case 27:	restart_logger2_signalled = true; break;
				case 28:	restart_logger3_signalled = true; break;
				default:	unknown_signalled = true; break;
			} else
#endif
				unknown_signalled = true;
			break;
	}
}

inline
void
record_signal_user (
	int signo
) {
	switch (signo) {
		case SIGCHLD:		child_signalled = true; break;
#if defined(SIGRTMIN)
		case SIGINT:		halt_signalled = true; break;
#endif
		case SIGTERM:		halt_signalled = true; break;
		case SIGHUP:		halt_signalled = true; break;
		case SIGPIPE:		halt_signalled = true; break;
#if !defined(SIGRTMIN)
		case NORMAL_SIGNAL:	normal_signalled = true; break;
		case SYSINIT_SIGNAL:	sysinit_signalled = true; break;
		case HALT_SIGNAL:	halt_signalled = true; break;
		case POWEROFF_SIGNAL:	halt_signalled = true; break;
		case REBOOT_SIGNAL:	halt_signalled = true; break;
		case FORCE_REBOOT_SIGNAL:	fasthalt_signalled = true; break;
#endif
		default:
#if defined(SIGRTMIN)
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
				case 26:	restart_logger1_signalled = true; break;
				case 27:	restart_logger2_signalled = true; break;
				case 28:	restart_logger3_signalled = true; break;
				default:	unknown_signalled = true; break;
			} else
#endif
				unknown_signalled = true;
			break;
	}
}

void (*record_signal) ( int signo ) = nullptr;

inline
void
read_command (
	bool is_system,
	int fd
) {
	char command;
	const int rc(read(fd, &command, sizeof command));
	if (static_cast<int>(sizeof command) <= rc) {
		switch (command) {
			case 'R':	(is_system ? fastreboot_signalled : fasthalt_signalled) = true; break;
			case 'r':	(is_system ? reboot_signalled : halt_signalled) = true; break;
			case 'H':	fasthalt_signalled = true; break;
			case 'h':	halt_signalled = true; break;
			case 'C':	(is_system ? fastpowercycle_signalled : fasthalt_signalled) = true; break;
			case 'c':	(is_system ? powercycle_signalled : halt_signalled) = true; break;
			case 'P':	(is_system ? fastpoweroff_signalled : fasthalt_signalled) = true; break;
			case 'p':	(is_system ? poweroff_signalled : halt_signalled) = true; break;
			case 'S':	sysinit_signalled = true; break;
			case 's':	(is_system ? rescue_signalled : unknown_signalled) = true; break;
			case 'b':	(is_system ? emergency_signalled : unknown_signalled) = true; break;
			case 'n':	normal_signalled = true; break;
			case 'L':	restart_logger3_signalled = true; break;
			case 'l':	restart_logger1_signalled = true; break;
		}
	}
}

inline
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
		sigaction(signo,&sa,nullptr);
}

}

/* File and directory names *************************************************
// **************************************************************************
*/

namespace {

/// envdir-style locale directories, in reverse priority order; accumulative
const char *
env_dirs[] = {
	"/etc/defaults/locale.d",
	"/etc/locale.d"
	"/usr/local/etc/locale.d",
};

/// read-conf-style locale files, in fallback order; non-accumulative
/// \note This does not include /etc/environment, found on old versions of Debian and on AIX.
const char *
env_files[] = {
	"/usr/local/etc/locale.conf",
	"/etc/locale.conf",
	"/etc/defaults/locale.conf",
	"/etc/default/locale",
	"/etc/sysconfig/i18n",
	"/etc/sysconfig/language",
	"/etc/sysconf/i18n"
};

const char *
system_manager_directories[] = {
	"/run/system-manager",
	"/run/system-manager/log",
	"/run/service-bundles",
	"/run/service-bundles/early-supervise",
	"/run/service-manager",
	"/run/user"
};

const struct api_symlink system_manager_symlinks_data[] =
{
	// Compatibility with early supervise bundles from version 1.16 and earlier.
	{	0,	"/run/system-manager/early-supervise",	"../service-bundles/early-supervise"		},
};

const std::vector<api_symlink> system_manager_symlinks(system_manager_symlinks_data, system_manager_symlinks_data + sizeof system_manager_symlinks_data/sizeof *system_manager_symlinks_data);

}

/* Utilities for the main program *******************************************
// **************************************************************************
*/

namespace {

/// \brief Open the primary logging pipe and attach it to our standard output and standard error.
inline
void
open_logging_pipe (
	const char * prog,
	FileDescriptorOwner & read_log_pipe,
	FileDescriptorOwner & write_log_pipe
) {
	int pipe_fds[2] = { -1, -1 };
	if (0 > pipe_close_on_exec (pipe_fds)) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "pipe", std::strerror(error));
	}
	read_log_pipe.reset(pipe_fds[0]);
	write_log_pipe.reset(pipe_fds[1]);
}

#if defined(__FreeBSD__) || defined(__DragonFly__)
int
setnoctty (
) {
	const FileDescriptorOwner fd(open_readwriteexisting_at(AT_FDCWD, "/dev/tty"));
	if (0 <= fd.get()) return -1;
	if (!isatty(fd.get())) {
		errno = ENOTTY;
		return -1;
	}
	return ioctl(fd.get(), TIOCNOTTY, 0);
}
#endif

inline
void
setup_process_state(
	const bool is_system,
	const char * prog,
	ProcessEnvironment & envs
) {
	if (is_system) {
		setsid();
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || defined(__NetBSD__)
		setlogin("root");
#endif
#if defined(__FreeBSD__) || defined(__DragonFly__)
		setnoctty();
#endif
		chdir("/");
		umask(0022);

		// ***********************************************************************************
		// initial environment for the trusted system base

		envs.set("LANG", DefaultEnvironment::SystemManager::LANG);
		envs.set("PATH", DefaultEnvironment::SystemManager::PATH);

		for (std::size_t di(0); di < sizeof env_dirs/sizeof *env_dirs; ++di) {
			const char * dirname(env_dirs[di]);
			const int scan_dir_fd(open_dir_at(AT_FDCWD, dirname));
			if (0 > scan_dir_fd) {
				const int error(errno);
				if (ENOENT != error)
					std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, dirname, std::strerror(error));
				continue;
			}
			process_env_dir(prog, envs, dirname, scan_dir_fd, true /*ignore errors*/, false /*first lines only*/, false /*no chomping*/);
		}

		for (std::size_t fi(0); fi < sizeof env_files/sizeof *env_files; ++fi) {
			const char * filename(env_files[fi]);
			FileStar f(std::fopen(filename, "r"));
			if (!f) {
				const int error(errno);
				if (ENOENT != error)
					std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, filename, std::strerror(error));
				continue;
			}
			try {
				std::vector<std::string> env_strings(read_file(prog, envs, filename, f));
				f = nullptr;
				for (std::vector<std::string>::const_iterator i(env_strings.begin()); i != env_strings.end(); ++i) {
					const std::string & s(*i);
					const std::string::size_type p(s.find('='));
					const std::string var(s.substr(0, p));
					const std::string val(p == std::string::npos ? std::string() : s.substr(p + 1, std::string::npos));
					envs.set(var, val);
				}
				break;
			} catch (const char * r) {
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, filename, r);
			}
		}
	} else {
		subreaper(true);
	}
}

#if defined(__LINUX__) || defined(__linux__)

inline
bool
hwclock_runs_in_UTC()
{
	std::ifstream i("/etc/adjtime");
	if (i.fail()) return true;
	char buf[100];
	i.getline(buf, sizeof buf, '\n');
	i.getline(buf, sizeof buf, '\n');
	i.getline(buf, sizeof buf, '\n');
	if (i.fail()) return true;
	return 0 != std::strcmp("LOCAL", buf);
}

inline
void
initialize_system_clock_timezone()
{
	struct timezone tz = { 0, 0 };
	const struct timeval * ztv(nullptr);		// This works around a compiler warning.
	const bool utc(hwclock_runs_in_UTC());
	const std::time_t now(std::time(nullptr));
	const struct tm *l(localtime(&now));
	const int seconds_west(-l->tm_gmtoff);	// It is important that this is an int.

	if (utc)
		settimeofday(ztv, &tz);	// Prevent the next call from adjusting the system clock.
	// Set the RTC/FAT local time offset, and (if not UTC) adjust the system clock from local-time-as-if-UTC to UTC.
	tz.tz_minuteswest = seconds_west / 60;
	settimeofday(ztv, &tz);
}

#elif defined(__FreeBSD__) || defined(__DragonFly__)

inline
bool
hwclock_runs_in_UTC()
{
	int oid[CTL_MAXNAME];
	std::size_t len = sizeof oid/sizeof *oid;
	int local(0);			// It is important that this is an int.
	std::size_t siz = sizeof local;

	sysctlnametomib("machdep.wall_cmos_clock", oid, &len);
	sysctl(oid, len, &local, &siz, nullptr, 0);
	if (local) return true;

	return 0 > access("/etc/wall_cmos_clock", F_OK);
}

inline
void
initialize_system_clock_timezone(
	const char * prog
) {
	struct timezone tz = {};
	const struct timeval * ztv(nullptr);	// This works around a compiler warning.
	const bool utc(hwclock_runs_in_UTC());
	const std::time_t now(std::time(nullptr));
	const struct tm *l(localtime(&now));
	const int seconds_west(-l->tm_gmtoff);	// It is important that this is an int.

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
			sysctlbyname("machdep.disable_rtc_set", nullptr, 0, &old_disable_rtc_set, sizeof old_disable_rtc_set);

		// Adjust the system clock from local-time-as-if-UTC to UTC, and zero out the tz_minuteswest if it is non-zero.
		struct timeval tv = {};
		gettimeofday(&tv, nullptr);
		tv.tv_sec += seconds_west - old_seconds_west;
		settimeofday(&tv, &tz);

		if (seconds_west != old_seconds_west)
			std::fprintf(stderr, "%s: WARNING: Timezone wrong.  Please put machdep.adjkerntz=%i and machdep.wall_cmos_clock=1 in loader.conf.\n", prog, seconds_west);
	} else
		// Zero out the tz_minuteswest if it is non-zero.
		settimeofday(ztv, &tz);
}

#elif defined(__NetBSD__)

#else

#error "Don't know what needs to be done about the system clock."

#endif

inline
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

inline
bool
is_already_mounted (
	const std::string & fspath
) {
	struct stat b;
	if (0 <= stat(fspath.c_str(), &b)) {
		// This is traditional, and what FreeBSD and derivatives do.
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

inline
std::list<std::string>
split_whitespace_columns (
	const std::string & s
) {
	std::list<std::string> r;
	std::string q;
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		if (!std::isspace(*p)) {
			q += *p;
		} else {
			if (!q.empty()) {
				r.push_back(q);
				q.clear();
			}
		}
	}
	if (!q.empty()) r.push_back(q);
	return r;
}

inline
unsigned
query_control_group_level(
) {
	std::ifstream i("/proc/filesystems");
	unsigned l(0U);
	if (!i.fail()) while (2U > l) {
		std::string line;
		std::getline(i, line, '\n');
		if (i.eof()) break;
		const std::list<std::string> cols(split_whitespace_columns(line));
		if (cols.empty()) continue;
		if (1U > l && "cgroup" == cols.back()) l = 1U;
		if (2U > l && "cgroup2" == cols.back()) l = 2U;
	}
	return l;
}

inline
void
setup_kernel_api_volumes(
	const char * prog,
	unsigned collection
) {
	for (std::vector<api_mount>::const_iterator i(api_mounts.begin()); api_mounts.end() != i; ++i) {
		if (collection != i->collection) continue;
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
}

inline
void
make_all(
	const char * prog,
	const std::vector<api_symlink> & symlinks
) {
	for (std::vector<api_symlink>::const_iterator i(symlinks.begin()); symlinks.end() != i; ++i) {
		for (int force = !!i->force ; ; --force) {
			if (0 <= symlink(i->target, i->name)) break;
			const int error(errno);
			if (!force || EEXIST != error) {
				std::fprintf(stderr, "%s: ERROR: %s: %s: %s\n", prog, "symlink", i->name, std::strerror(error));
				break;
			}
			unlink(i->name);
		}
	}
}

inline
void
setup_kernel_api_volumes_and_devices(
	const char * prog
) {
	setup_kernel_api_volumes(prog, 0U); // Base collection, wanted everywhere

	// This must be queried after /proc has been mounted.
	const unsigned cgl(query_control_group_level());
	std::fprintf(stderr, "%s: INFO: Control group level is %u\n", prog, cgl);

	switch (cgl) {
		case 1U:
			setup_kernel_api_volumes(prog, 1U); // Old cgroups v1 collection
			break;
		case 2U:
			setup_kernel_api_volumes(prog, 2U); // cgroups v2 collection
			break;
	}
	make_all(prog, api_symlinks);
}

inline
void
make_needed_run_directories(
	const bool is_system,
	const char * prog
) {
	if (is_system) {
		for (std::size_t fi(0); fi < sizeof system_manager_directories/sizeof *system_manager_directories; ++fi) {
			const char * dirname(system_manager_directories[fi]);
			if (0 > mkdir(dirname, 0755)) {
				const int error(errno);
				if (EEXIST != error)
					std::fprintf(stderr, "%s: ERROR: %s: %s: %s\n", prog, "mkdir", dirname, std::strerror(error));
			}
		}
		make_all(prog, system_manager_symlinks);
	} else {
		const
		std::string
		user_manager_directories[] = {
			effective_user_runtime_dir() + "per-user-manager",
			effective_user_runtime_dir() + "per-user-manager/log",
			effective_user_runtime_dir() + "service-bundles",
			effective_user_runtime_dir() + "service-bundles/early-supervise",
			effective_user_runtime_dir() + "service-manager",
		};
		for (std::size_t fi(0); fi < sizeof user_manager_directories/sizeof *user_manager_directories; ++fi) {
			const char * dirname(user_manager_directories[fi].c_str());
			if (0 > mkdir(dirname, 0755)) {
				const int error(errno);
				if (EEXIST != error)
					std::fprintf(stderr, "%s: ERROR: %s: %s: %s\n", prog, "mkdir", dirname, std::strerror(error));
			}
		}
	}
}

inline
int
open_null(
	const char * prog
) {
	const int dev_null_fd(open_read_at(AT_FDCWD, "/dev/null"));
	if (0 > dev_null_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "/dev/null", std::strerror(error));
	} else
		set_non_blocking(dev_null_fd, false);
	return dev_null_fd;
}

inline
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

inline
void
fdcopy(
	const char * prog,
	const FileDescriptorOwner & fd,
	FileDescriptorOwner & td
) {
	const int d(::dup(fd.get()));
	if (0 > d) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: fdcopy(%i): %s\n", prog, fd.get(), std::strerror(error));
	} else
	{
		set_non_blocking(d, false);
		td.reset(d);
	}
}

inline
void
fdcopy(
	const char * prog,
	const FileDescriptorOwner & existing_file,
	int fd
) {
	const int d(::dup2(existing_file.get(), fd));
	if (0 > d) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: fdcopy(%i, %i): %s\n", prog, existing_file.get(), fd, std::strerror(error));
	}
}

inline
void
fdcopy(
	const char * prog,
	FileDescriptorOwner filler_stdio[LISTEN_SOCKET_FILENO + 1],
	const FileDescriptorOwner & existing_file,
	int fd
) {
	const int d(::dup2(existing_file.get(), fd));
	if (0 > d) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: fdcopy(%i, %i): %s\n", prog, existing_file.get(), fd, std::strerror(error));
	} else
	if (d <= LISTEN_SOCKET_FILENO)
		filler_stdio[d].release();
}

inline
void
last_resort_io_defaults(
	const bool is_system,
	const char * prog,
	const FileDescriptorOwner & dev_null,
	FileDescriptorOwner saved_stdio[STDERR_FILENO + 1]
) {
	// Populate saved standard input as /dev/null if it was initially closed as we inherited it.
	if (0 > saved_stdio[STDIN_FILENO].get())
		fdcopy(prog, dev_null, saved_stdio[STDIN_FILENO]);
	if (is_system) {
		// Always open the console.
		FileDescriptorOwner dev_console(open_readwriteexisting_at(AT_FDCWD, "/dev/console"));
		if (0 > dev_console.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "/dev/console", std::strerror(error));
			fdcopy(prog, saved_stdio[STDIN_FILENO], dev_console);
		} else {
			set_non_blocking(dev_console.get(), false);
		}
		// Populate saved standard output as /dev/console if it was initially closed.
		// The console is the logger of last resort.
		if (0 > saved_stdio[STDOUT_FILENO].get())
			fdcopy(prog, dev_console, saved_stdio[STDOUT_FILENO]);
	} else {
		// Populate saved standard output as standard input if it was initially closed.
		// The logger of last resort is whatever standard input is.
		if (0 > saved_stdio[STDOUT_FILENO].get())
			fdcopy(prog, saved_stdio[STDIN_FILENO], saved_stdio[STDOUT_FILENO]);
	}
	// Populate saved standard error as standard output if it was initially closed.
	if (0 > saved_stdio[STDERR_FILENO].get())
		fdcopy(prog, saved_stdio[STDOUT_FILENO], saved_stdio[STDERR_FILENO]);
}

inline
void
start_system(
	const char * prog
) {
#if defined(__LINUX__) || defined(__linux__)
	reboot(RB_DISABLE_CAD);
	FileDescriptorOwner current_kvt(open_readwriteexisting_at(AT_FDCWD, "/dev/tty0"));
	if (0 > current_kvt.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "/dev/tty0", std::strerror(error));
	} else {
#if defined(KBREQ_SIGNAL)
		ioctl(current_kvt.get(), KDSIGACCEPT, KBREQ_SIGNAL);
#endif
	}
#else
	static_cast<void>(prog);
#endif
}

#if defined(__NetBSD__)
int reboot(int flags) { return ::reboot(flags, nullptr); }
#endif

inline
void
end_system()
{
#if defined(__LINUX__) || defined(__linux__)
	sync();		// The BSD reboot system call already implies a sync() unless RB_NOSYNC is used.
#endif
	if (fastpoweroff_signalled) {
#if defined(__LINUX__) || defined(__linux__)
		reboot(RB_POWER_OFF);
#elif defined(__OpenBSD__) || defined(__NetBSD__)
		reboot(RB_POWERDOWN);
#else
		reboot(RB_POWEROFF);
#endif
	}
	if (fasthalt_signalled) {
#if defined(__LINUX__) || defined(__linux__)
		reboot(RB_HALT_SYSTEM);
#else
		reboot(RB_HALT);
#endif
	}
	if (fastreboot_signalled) {
		reboot(RB_AUTOBOOT);
	}
	if (fastpowercycle_signalled) {
#if defined(RB_POWERCYCLE)
		reboot(RB_POWERCYCLE);
#else
		reboot(RB_AUTOBOOT);
#endif
	}
	reboot(RB_AUTOBOOT);
}

struct iovec
cgroup_controllers[4] = {
	MAKE_IOVEC("+cpu"),
	MAKE_IOVEC("+memory"),
	MAKE_IOVEC("+io"),
	MAKE_IOVEC("+pids"),
};

const char *
cgroup_paths[] = {
	"",
	"/service-manager.slice"
	// We don't need system-control.slice or *-manager-log.slice because they don't distribute onwards to further groups.
};

inline
void
initialize_root_control_groups (
	const char * prog,
	const ProcessEnvironment & envs
) {
	FileStar self_cgroup(open_my_control_group_info("/proc/self/cgroup"));
	if (!self_cgroup) {
		const int error(errno);
		if (ENOENT != error)	// This is what we'll see on a BSD.
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "/proc/self/cgroup", std::strerror(error));
		return;
	}
	std::string prefix("/sys/fs/cgroup"), current;
	if (!read_my_control_group(self_cgroup, "", current)) {
		if (!read_my_control_group(self_cgroup, "name=systemd", current))
			return;
		prefix += "/systemd";
	}
	const std::string cgroup_root(prefix + current);
	const FileDescriptorOwner cgroup_root_fd(open_dir_at(AT_FDCWD, cgroup_root.c_str()));
	if (0 > cgroup_root_fd.get()) {
		message_fatal_errno(prog, envs, cgroup_root.c_str());
		return;
	}
	const std::string me_slice(cgroup_root + "/me.slice");
	if (0 > mkdirat(AT_FDCWD, me_slice.c_str(), 0755)) {
		if (EEXIST == errno) goto group_exists;
		message_fatal_errno(prog, envs, me_slice.c_str());
	} else {
group_exists:
		const std::string knobname(me_slice + "/cgroup.procs");
		const FileDescriptorOwner cgroup_procs_fd(open_appendexisting_at(AT_FDCWD, knobname.c_str()));
		if (0 > cgroup_procs_fd.get()
		||  0 > write(cgroup_procs_fd.get(), "0\n", 2)) {
			message_fatal_errno(prog, envs, knobname.c_str());
		}
	}
	for (const char ** p(cgroup_paths); p < cgroup_paths + sizeof cgroup_paths/sizeof *cgroup_paths; ++p) {
		const char * group(*p);

		if (0 > mkdirat(cgroup_root_fd.get(), (cgroup_root + group).c_str(), 0755)) {
			const int error(errno);
			if (EEXIST != error)
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, (cgroup_root + group).c_str(), std::strerror(error));
		}
		const std::string knobname((cgroup_root + group) + "/cgroup.subtree_control");
		const FileDescriptorOwner cgroup_knob_fd(open_writetruncexisting_at(cgroup_root_fd.get(), knobname.c_str()));
		if (0 > cgroup_knob_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, knobname.c_str(), std::strerror(error));
			continue;
		}
		for (const struct iovec * v(cgroup_controllers); v < cgroup_controllers + sizeof cgroup_controllers/sizeof *cgroup_controllers; ++v)
			if (0 > writev(cgroup_knob_fd.get(), v, 1)) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %.*s: %s\n", prog, knobname.c_str(), static_cast<int>(v->iov_len), static_cast<const char *>(v->iov_base), std::strerror(error));
			}
	}
}

const char *
system_manager_logdirs[] = {	// This must have at least 3 entries.
	"/var/log/system-manager",	// skipped when not trying all log directories
	"/var/system-manager/log",	// skipped when not trying all log directories
	"/run/system-manager/log"
};

inline
void
change_to_system_manager_log_root (
	const bool is_system,
	const bool try_all_log_directories
) {
	if (is_system) {
		for (const char ** r(system_manager_logdirs + (try_all_log_directories ? 0U : 2U));
		     r < system_manager_logdirs + sizeof system_manager_logdirs/sizeof *system_manager_logdirs;
		     ++r
		)
			if (0 <= chdir(*r))
				return;
	} else
	{
		if (try_all_log_directories) {
			if (0 <= chdir(effective_user_log_dir().c_str()))
				return;
		}
		if (0 <= chdir((effective_user_runtime_dir() + "per-user-manager/log").c_str()))
			return;
	}
}

inline
void
reap_spawned_children (
	const char * prog
) {
	if (child_signalled) {
		child_signalled = false;
		for (;;) {
			int status, code;
			pid_t c;
			if (0 >= wait_nonblocking_for_anychild_exit(c, status, code)) break;
			if (c == service_manager_pid) {
				std::fprintf(stderr, "%s: WARNING: %s (pid %i) ended status %i code %i\n", prog, "service-manager", c, status, code);
				service_manager_pid = -1;
			} else
			if (c == cyclog_pid) {
				std::fprintf(stderr, "%s: WARNING: %s (pid %i) ended status %i code %i\n", prog, "cyclog", c, status, code);
				cyclog_pid = -1;
				// If cyclog abended, throttle respawns.
				if (WAIT_STATUS_SIGNALLED == status || WAIT_STATUS_SIGNALLED_CORE == status || (WAIT_STATUS_EXITED == status && 0 != code)) {
					timespec t;
					t.tv_sec = 0;
					t.tv_nsec = 500000000; // 0.5 second
					// If someone sends us a signal to do something, this will be interrupted.
					nanosleep(&t, nullptr);
				}
			} else
			if (c == regular_system_control_pid) {
				std::fprintf(stderr, "%s: INFO: %s (pid %i) ended status %i code %i\n", prog, "system-control", c, status, code);
				regular_system_control_pid = -1;
			} else
			if (c == emergency_system_control_pid) {
				std::fprintf(stderr, "%s: INFO: %s (pid %i) ended status %i code %i\n", prog, "system-control", c, status, code);
				emergency_system_control_pid = -1;
			} else
			if (c == kbreq_system_control_pid) {
				std::fprintf(stderr, "%s: INFO: %s (pid %i) ended status %i code %i\n", prog, "system-control", c, status, code);
				kbreq_system_control_pid = -1;
			}
		}
	}
}

inline
bool
fork_system_control_as_needed (
	const bool is_system,
	const char * prog,
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const bool verbose(true);
	if (!has_emergency_system_control()) {
		const char * subcommand(nullptr), * option(nullptr);
		if (emergency_signalled) {
			subcommand = "activate";
			option = "emergency";
			emergency_signalled = false;
		} else
		{
		}
		if (subcommand) {
			emergency_system_control_pid = fork();
			if (-1 == emergency_system_control_pid) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
			} else if (0 == emergency_system_control_pid) {
				default_all_signals();
				alarm(60);
				// Replace the original arguments with this.
				args.clear();
				args.push_back("move-to-control-group");
				args.push_back("../system-control.slice");
				args.push_back("system-control");
				args.push_back(subcommand);
				if (verbose)
					args.push_back("--verbose");
				if (!is_system)
					args.push_back("--user");
				if (option)
					args.push_back(option);
				next_prog = arg0_of(args);
				return true;
			} else
				std::fprintf(stderr, "%s: INFO: %s (pid %i) started (%s%s %s)\n", prog, "system-control", emergency_system_control_pid, subcommand, is_system ? "" : " --user", option ? option : "");
		}
	}
	if (!has_kbreq_system_control()) {
		const char * subcommand(nullptr), * option(nullptr);
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
		} else
		{
		}
		if (subcommand) {
			kbreq_system_control_pid = fork();
			if (-1 == kbreq_system_control_pid) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
			} else if (0 == kbreq_system_control_pid) {
				default_all_signals();
				alarm(60);
				// Replace the original arguments with this.
				args.clear();
				args.push_back("move-to-control-group");
				args.push_back("../system-control.slice");
				args.push_back("system-control");
				args.push_back(subcommand);
				if (verbose)
					args.push_back("--verbose");
				if (!is_system)
					args.push_back("--user");
				if (option)
					args.push_back(option);
				next_prog = arg0_of(args);
				return true;
			} else
				std::fprintf(stderr, "%s: INFO: %s (pid %i) started (%s%s %s)\n", prog, "system-control", kbreq_system_control_pid, subcommand, is_system ? "" : " --user", option ? option : "");
		}
	}
	if (!has_regular_system_control()) {
		const char * subcommand(nullptr), * option(nullptr);
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
		if (powercycle_signalled) {
			subcommand = "start";
			option = "powercycle";
			poweroff_signalled = false;
		} else
		if (reboot_signalled) {
			subcommand = "start";
			option = "reboot";
			reboot_signalled = false;
		} else
		{
		}
		if (subcommand) {
			regular_system_control_pid = fork();
			if (-1 == regular_system_control_pid) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
			} else if (0 == regular_system_control_pid) {
				default_all_signals();
				alarm(480);
				// Replace the original arguments with this.
				args.clear();
				args.push_back("move-to-control-group");
				args.push_back("../system-control.slice");
				args.push_back("system-control");
				args.push_back(subcommand);
				if (verbose)
					args.push_back("--verbose");
				if (!is_system)
					args.push_back("--user");
				if (option)
					args.push_back(option);
				next_prog = arg0_of(args);
				return true;
			} else
				std::fprintf(stderr, "%s: INFO: %s (pid %i) started (%s%s %s)\n", prog, "system-control", regular_system_control_pid, subcommand, is_system ? "" : " --user", option ? option : "");
		}
	}
	if (!has_regular_system_control()) {
		if (init_signalled) {
			init_signalled = false;
			regular_system_control_pid = fork();
			if (-1 == regular_system_control_pid) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
			} else if (0 == regular_system_control_pid) {
				default_all_signals();
				alarm(420);
				// Retain the original arguments and insert the following in front of them.
				if (!is_system)
					args.insert(args.begin(), "--user");
				args.insert(args.begin(), "init");
				args.insert(args.begin(), "system-control");
				args.insert(args.begin(), "../system-control.slice");
				args.insert(args.begin(), "move-to-control-group");
				next_prog = arg0_of(args);
				return true;
			} else
				std::fprintf(stderr, "%s: INFO: %s (pid %i) started (%s%s %s)\n", prog, "system-control", regular_system_control_pid, "init", is_system ? "" : " --user", concat(args).c_str());
		}
	}
	return false;
}

inline
bool
fork_cyclog_as_needed (
	const bool is_system,
	const char * prog,
	const char * & next_prog,
	std::vector<const char *> & args,
	const FileDescriptorOwner saved_stdio[STDERR_FILENO + 1],
	const FileDescriptorOwner & read_log_pipe,
	const bool try_all_log_directories
) {
	if (!has_cyclog() && (!stop_signalled() || has_service_manager())) {
		cyclog_pid = fork();
		if (-1 == cyclog_pid) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
		} else if (0 == cyclog_pid) {
			change_to_system_manager_log_root(is_system, try_all_log_directories);
			if (is_system)
				setsid();
			default_all_signals();
			args.clear();
			args.push_back("move-to-control-group");
			if (is_system)
				args.push_back("../system-manager-log.slice");
			else
				args.push_back("../per-user-manager-log.slice");
			args.push_back("cyclog");
			args.push_back("--max-file-size");
			args.push_back("262144");
			args.push_back("--max-total-size");
			args.push_back("1048576");
			args.push_back(".");
			next_prog = arg0_of(args);
			if (-1 != read_log_pipe.get())
				fdcopy(prog, read_log_pipe, STDIN_FILENO);
			fdcopy(prog, saved_stdio[STDOUT_FILENO], STDOUT_FILENO);
			fdcopy(prog, saved_stdio[STDERR_FILENO], STDERR_FILENO);
			close(LISTEN_SOCKET_FILENO);
			return true;
		} else
			std::fprintf(stderr, "%s: INFO: %s (pid %i) started\n", prog, "cyclog", cyclog_pid);
	}
	return false;
}

inline
bool
fork_service_manager_as_needed (
	const bool is_system,
	const char * prog,
	const char * & next_prog,
	std::vector<const char *> & args,
	const FileDescriptorOwner & dev_null_fd,
	const FileDescriptorOwner & service_manager_socket_fd,
	const FileDescriptorOwner & write_log_pipe
) {
	if (!has_service_manager() && !stop_signalled()) {
		service_manager_pid = fork();
		if (-1 == service_manager_pid) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
		} else if (0 == service_manager_pid) {
			if (is_system)
				setsid();
			default_all_signals();
			args.clear();
			args.push_back("move-to-control-group");
			args.push_back("../service-manager.slice/me.slice");
			args.push_back("service-manager");
			next_prog = arg0_of(args);
			fdcopy(prog, dev_null_fd, STDIN_FILENO);
			if (-1 != write_log_pipe.get()) {
				fdcopy(prog, write_log_pipe, STDOUT_FILENO);
				fdcopy(prog, write_log_pipe, STDERR_FILENO);
			}
			fdcopy(prog, service_manager_socket_fd, LISTEN_SOCKET_FILENO);
			return true;
		} else
			std::fprintf(stderr, "%s: INFO: %s (pid %i) started\n", prog, "service-manager", service_manager_pid);
	}
	return false;
}

}

/* Main program *************************************************************
// **************************************************************************
*/

namespace {

void
common_manager (
	const bool is_system,
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	record_signal = (is_system ? record_signal_system : record_signal_user);

	const char * prog(basename_of(args[0]));
	args.erase(args.begin());

#if defined(__FreeBSD__) || defined(__DragonFly__)
	// FreeBSD initializes process #1 with a controlling terminal!
	// The only way to get rid of it is to close all open file descriptors to it.
	if (is_system) {
		for (std::size_t i(0U); i < LISTEN_SOCKET_FILENO; ++i)
			if (isatty(i))
				close(i);
	}
#endif

	// We must ensure that no new file descriptors are allocated in the standard+systemd file descriptors range, otherwise dup2() and automatic-close() in child processes go wrong later.
	FileDescriptorOwner filler_stdio[LISTEN_SOCKET_FILENO + 1] = { FileDescriptorOwner(-1), FileDescriptorOwner(-1), FileDescriptorOwner(-1), FileDescriptorOwner(-1) };
	for (
		FileDescriptorOwner root(open_dir_at(AT_FDCWD, "/"));
		0 <= root.get() && root.get() <= LISTEN_SOCKET_FILENO;
		root.reset(::dup(root.release()))
	) {
		filler_stdio[root.get()].reset(root.get());
	}

	// The system manager begins with standard I/O connected to a (console) TTY.
	// A per-user manager begins with standard I/O connected to logger services and suchlike.
	// We want to save these, if they are open, for use as log destinations of last resort during shutdown.
	FileDescriptorOwner saved_stdio[STDERR_FILENO + 1] = { FileDescriptorOwner(-1), FileDescriptorOwner(-1), FileDescriptorOwner(-1) };
	for (std::size_t i(0U); i < sizeof saved_stdio/sizeof *saved_stdio; ++i) {
		if (0 > filler_stdio[i].get())
			saved_stdio[i].reset(::dup(i));
	}

	// In the normal course of events, standard output and error will be connected to some form of logger process, via a pipe.
	// We don't want our output cluttering a TTY, and device files such as /dev/null and /dev/console do not exist yet.
	FileDescriptorOwner read_log_pipe(-1), write_log_pipe(-1);
	open_logging_pipe(prog, read_log_pipe, write_log_pipe);
	fdcopy(prog, filler_stdio, write_log_pipe, STDOUT_FILENO);
	fdcopy(prog, filler_stdio, write_log_pipe, STDERR_FILENO);

	// Now we perform the process initialization that does thing like mounting /dev.
	// Errors mounting things go down the pipe, from which nothing is reading as yet.
	// We must be careful about not writing too much to this pipe without a running cyclog process.
	setup_process_state(is_system, prog, envs);
	PreventDefaultForFatalSignals ignored_signals(
		SIGTERM,
		SIGINT,
		SIGQUIT,
		SIGHUP,
		SIGUSR1,
		SIGUSR2,
		SIGPIPE,
		SIGABRT,
		SIGALRM,
		SIGIO,
#if defined(SIGPWR)
		SIGPWR,
#endif
#if defined(SIGRTMIN)
		SIGRTMIN + 0,
		SIGRTMIN + 1,
		SIGRTMIN + 2,
		SIGRTMIN + 3,
		SIGRTMIN + 4,
		SIGRTMIN + 5,
		SIGRTMIN + 10,
		SIGRTMIN + 13,
		SIGRTMIN + 14,
		SIGRTMIN + 15,
		SIGRTMIN + 26,
		SIGRTMIN + 27,
		SIGRTMIN + 28,
#endif
		0
	);
	ReserveSignalsForKQueue kqueue_reservation(
		SIGCHLD,
#if defined(KBREQ_SIGNAL)
		KBREQ_SIGNAL,
#endif
		SYSINIT_SIGNAL,
		NORMAL_SIGNAL,
		EMERGENCY_SIGNAL,
		RESCUE_SIGNAL,
		HALT_SIGNAL,
		POWEROFF_SIGNAL,
#if defined(POWERCYCLE_SIGNAL)
		POWERCYCLE_SIGNAL,
#endif
		REBOOT_SIGNAL,
#if defined(FORCE_HALT_SIGNAL)
		FORCE_HALT_SIGNAL,
#endif
#if defined(FORCE_POWEROFF_SIGNAL)
		FORCE_POWEROFF_SIGNAL,
#endif
#if defined(FORCE_POWERCYCLE_SIGNAL)
		FORCE_POWERCYCLE_SIGNAL,
#endif
		FORCE_REBOOT_SIGNAL,
		SIGTERM,
		SIGHUP,
		SIGPIPE,
#if defined(SAK_SIGNAL)
		SAK_SIGNAL,
#endif
#if defined(SIGPWR)
		SIGPWR,
#endif
#if defined(RETARGET_LOGGING_SIGNAL1)
		RETARGET_LOGGING_SIGNAL1,
#endif
#if defined(RETARGET_LOGGING_SIGNAL2)
		RETARGET_LOGGING_SIGNAL2,
#endif
#if defined(RETARGET_LOGGING_SIGNAL3)
		RETARGET_LOGGING_SIGNAL3,
#endif
		0
	);
	if (is_system) {
#if defined(__LINUX__) || defined(__linux__)
		initialize_system_clock_timezone();
#elif defined(__FreeBSD__) || defined(__DragonFly__)
		initialize_system_clock_timezone(prog);
#elif defined(__NetBSD__)
#else
#error "Don't know what needs to be done about the system clock."
#endif
		setup_kernel_api_volumes_and_devices(prog);
	}
	make_needed_run_directories(is_system, prog);
	if (is_system) {
		if (!am_in_jail(envs))
			start_system(prog);
	}
	initialize_root_control_groups(prog, envs);

	// Now we can use /dev/console, /dev/null, and the rest.
	const FileDescriptorOwner dev_null_fd(open_null(prog));
	fdcopy(prog, filler_stdio, dev_null_fd, STDIN_FILENO);
	last_resort_io_defaults(is_system, prog, dev_null_fd, saved_stdio);

	const unsigned listen_fds(query_listen_fds(envs));
	if (listen_fds)
		envs.unset("LISTEN_FDNAMES");

	const FileDescriptorOwner service_manager_socket_fd(listen_service_manager_socket(is_system, prog));

#if defined(DEBUG)	// This is not an emergency mode.  Do not abuse as such.
	if (is_system) {
		const pid_t shell(fork());
		if (-1 == shell) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
		} else if (0 == shell) {
			setsid();
			args.clear();
			args.push_back("sh");
			next_prog = arg0_of(args);
			fdcopy(saved_stdin, STDIN_FILENO);
			fdcopy(saved_stdout, STDOUT_FILENO);
			fdcopy(saved_stderr, STDERR_FILENO);
			close(LISTEN_SOCKET_FILENO);
			return;
		}
	}
#endif

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		message_fatal_errno(prog, envs, "kqueue");
		return;
	}

	std::vector<struct kevent> ip;
	for (unsigned i(0U); i < listen_fds; ++i)
		append_event(ip, LISTEN_SOCKET_FILENO + i, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	if (is_system) {
#if defined(KBREQ_SIGNAL)
		append_event(ip, KBREQ_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
#endif
		append_event(ip, RESCUE_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		append_event(ip, EMERGENCY_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
#if defined(SAK_SIGNAL)
		append_event(ip, SAK_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
#endif
#if defined(SIGPWR)
		append_event(ip, SIGPWR, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
#endif
	} else {
		if (SIGINT != REBOOT_SIGNAL) {
			append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		}
		append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	}
	append_event(ip, SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, NORMAL_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SYSINIT_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, HALT_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, POWEROFF_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
#if defined(POWERCYCLE_SIGNAL)
	append_event(ip, POWERCYCLE_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
#endif
	append_event(ip, REBOOT_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, FORCE_REBOOT_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
#if defined(FORCE_HALT_SIGNAL)
	append_event(ip, FORCE_HALT_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
#endif
#if defined(FORCE_POWEROFF_SIGNAL)
	append_event(ip, FORCE_POWEROFF_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
#endif
#if defined(FORCE_POWERCYCLE_SIGNAL)
	append_event(ip, FORCE_POWERCYCLE_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
#endif
#if defined(RETARGET_LOGGING_SIGNAL1)
	append_event(ip, RETARGET_LOGGING_SIGNAL1, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
#endif
#if defined(RETARGET_LOGGING_SIGNAL2)
	append_event(ip, RETARGET_LOGGING_SIGNAL2, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
#endif
#if defined(RETARGET_LOGGING_SIGNAL3)
	append_event(ip, RETARGET_LOGGING_SIGNAL3, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
#endif

	// Remove any remaining standard I/O filler.
	for (std::size_t i(0U); i < sizeof filler_stdio/sizeof *filler_stdio; ++i)
		filler_stdio[i].reset(-1);

	bool try_all_log_directories(false);
	for (;;) {
		reap_spawned_children(prog);

		// Run system-control if a job is pending and system-control isn't already running.
		if (fork_system_control_as_needed(is_system, prog, next_prog, args)) return;

		// Exit if stop has been signalled and both the service manager and logger have exited.
		if (stop_signalled() && !has_service_manager() && !has_cyclog()) break;

		// Kill the service manager if stop has been signalled.
		if (stop_signalled() && has_service_manager() && !has_any_system_control()) {
			std::fprintf(stderr, "%s: DEBUG: %s\n", prog, "terminating service manager");
			kill(service_manager_pid, SIGTERM);
		}

		// Kill the logger if logger restart has been signalled.
		if (has_cyclog() && restart_logger_signalled()) {
			std::fprintf(stderr, "%s: DEBUG: %s (pid %i)\n", prog, "terminating cyclog", cyclog_pid);
			kill(cyclog_pid, SIGTERM);
			try_all_log_directories = restart_logger1_signalled;
			restart_logger1_signalled = restart_logger2_signalled = restart_logger3_signalled = false;
		}

		// Restart the logger unless both stop has been signalled and the service manager has exited.
		// If the service manager has not exited and stop has been signalled, we still need the logger to restart and keep draining the pipe.
		if (fork_cyclog_as_needed(is_system, prog, next_prog, args, saved_stdio, read_log_pipe, try_all_log_directories)) return;

		// If stop has been signalled and the service manager has exited, close the logging pipe so that the logger finally exits.
		if (stop_signalled() && !has_service_manager() && -1 != read_log_pipe.get()) {
			std::fprintf(stderr, "%s: DEBUG: %s\n", prog, "closing logger");
			for (std::size_t i(0U); i < sizeof saved_stdio/sizeof *saved_stdio; ++i)
				fdcopy(prog, saved_stdio[i], i);
			read_log_pipe.reset(-1);
			write_log_pipe.reset(-1);
		}

		// Restart the service manager unless stop has been signalled.
		if (fork_service_manager_as_needed(is_system, prog, next_prog, args, dev_null_fd, service_manager_socket_fd, write_log_pipe)) return;

		if (unknown_signalled) {
			std::fprintf(stderr, "%s: WARNING: %s\n", prog, "Unknown signal ignored.");
			unknown_signalled = false;
		}

		struct kevent p[1024];
		const int rc(kevent(queue.get(), ip.data(), ip.size(), p, sizeof p/sizeof *p, nullptr));
		ip.clear();
		if (0 > rc) {
			if (EINTR == errno) continue;
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
			return;
		}
		for (size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			switch (p[i].filter) {
				case EVFILT_SIGNAL:	record_signal(p[i].ident); break;
				case EVFILT_READ:
				{
					append_event(ip, p[i].ident, p[i].filter, EV_EOF | EV_CLEAR, 0, 0, nullptr);
					read_command(is_system, p[i].ident);
					break;
				}
			}
		}
	}

	if (is_system) {
		sync();
		if (!am_in_jail(envs))
			end_system();
	}
	throw EXIT_SUCCESS;
}

}

void
per_user_manager (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const bool is_system(1 == getpid());
	common_manager(is_system, next_prog, args, envs);
}

void
system_manager (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const bool is_system(1 == getpid());
	common_manager(is_system, next_prog, args, envs);
}
