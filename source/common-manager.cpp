/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "kqueue_common.h"
#if defined(__LINUX__) || defined(__linux__)
#define _BSD_SOURCE 1
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
#include "nmount.h"
#include "listen.h"
#include "popt.h"
#include "jail.h"
#include "runtime-dir.h"
#include "common-manager.h"
#include "service-manager-client.h"
#include "FileStar.h"
#include "FileDescriptorOwner.h"
#include "SignalManagement.h"
#include "control_groups.h"

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

static pid_t service_manager_pid(-1);
static pid_t cyclog_pid(-1);
static pid_t regular_system_control_pid(-1);
static pid_t emergency_system_control_pid(-1);
static pid_t kbreq_system_control_pid(-1);
static inline bool has_service_manager() { return static_cast<pid_t>(-1) != service_manager_pid; }
static inline bool has_cyclog() { return static_cast<pid_t>(-1) != cyclog_pid; }
static inline bool has_regular_system_control() { return static_cast<pid_t>(-1) != regular_system_control_pid; }
static inline bool has_emergency_system_control() { return static_cast<pid_t>(-1) != emergency_system_control_pid; }
static inline bool has_kbreq_system_control() { return static_cast<pid_t>(-1) != kbreq_system_control_pid; }
static inline bool has_any_system_control() { return has_regular_system_control() || has_emergency_system_control() || has_kbreq_system_control(); }

static sig_atomic_t sysinit_signalled (false);
static sig_atomic_t init_signalled (true);
static sig_atomic_t normal_signalled (false);
static sig_atomic_t child_signalled (false);
static sig_atomic_t rescue_signalled (false);
static sig_atomic_t emergency_signalled (false);
static sig_atomic_t halt_signalled (false);
static sig_atomic_t poweroff_signalled (false);
static sig_atomic_t powercycle_signalled (false);
static sig_atomic_t reboot_signalled (false);
static sig_atomic_t power_signalled (false);
static sig_atomic_t kbrequest_signalled (false);
static sig_atomic_t sak_signalled (false);
static sig_atomic_t fasthalt_signalled (false);
static sig_atomic_t fastpoweroff_signalled (false);
static sig_atomic_t fastpowercycle_signalled (false);
static sig_atomic_t fastreboot_signalled (false);
static sig_atomic_t unknown_signalled (false);
static inline bool stop_signalled() { return fasthalt_signalled || fastpoweroff_signalled || fastpowercycle_signalled || fastreboot_signalled; }

static inline
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
				default:	unknown_signalled = true; break;
			} else
#endif
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
				default:	unknown_signalled = true; break;
			} else
#endif
				unknown_signalled = true; 
			break;
	}
}

static void (*record_signal) ( int signo ) = 0;

static inline
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
		}
	}
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
system_manager_directories[] = {
	"/run/system-manager",
	"/run/system-manager/log",
	"/run/service-bundles",
	"/run/service-bundles/early-supervise",
	"/run/service-manager",
	"/run/user"
};

static
const struct api_symlink system_manager_symlinks_data[] = 
{
	// Compatibility with early supervise bundles from version 1.16 and earlier.
	{	0,	"/run/system-manager/early-supervise",	"../service-bundles/early-supervise"		},
};

static const std::vector<api_symlink> system_manager_symlinks(system_manager_symlinks_data, system_manager_symlinks_data + sizeof system_manager_symlinks_data/sizeof *system_manager_symlinks_data);

/* Utilities for the main program *******************************************
// **************************************************************************
*/

/// \brief Open the primary logging pipe and attach it to our standard output and standard error.
static inline
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
static
int
setnoctty ( 
) {
	const FileDescriptorOwner fd(open_readwriteexisting_at(AT_FDCWD, "/dev/tty"));
	if (0 <= fd.get()) return -1;
	if (!isatty(fd.get())) return errno = ENOTTY, -1;
	return ioctl(fd.get(), TIOCNOTTY, 0);
}
#endif

static inline
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

		// We cannot omit /sbin and /bin from the path because we cannot reliably detect that they duplicate /usr/bin and /usr/sbin at this point.
		// On some systems, /usr/sbin and /usr/bin are the symbolic links, and don't exist until we have mounted /usr .
		// On other systems, /sbin and /bin are the symbolic links, but /usr isn't a mount point and everything is on the root volume.
		envs.set("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");
#if defined(__LINUX__) || defined(__linux__)
		// https://sourceware.org/glibc/wiki/Proposals/C.UTF-8
		envs.set("LANG", "C.UTF-8");
#else
		envs.set("LANG", "C");
#endif

		// parse /etc/locale.d as if by envdir.
		const int scan_dir_fd(open_dir_at(AT_FDCWD, "/etc/locale.d"));
		if (0 > scan_dir_fd) {
			const int error(errno);
			if (ENOENT != error)
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "/etc/locale.d", std::strerror(error));
		} else
			process_env_dir(prog, envs, "/etc/locale.d", scan_dir_fd, true /*ignore errors*/, false /*first lines only*/, false /*no chomping*/);

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
				std::vector<std::string> env_strings(read_file(f));
				f = 0;
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

static inline
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

static inline
void
initialize_system_clock_timezone() 
{
	struct timezone tz = { 0, 0 };
	const struct timeval * ztv(0);		// This works around a compiler warning.
	const bool utc(hwclock_runs_in_UTC());
	const std::time_t now(std::time(0));
	const struct tm *l(localtime(&now));
	const int seconds_west(-l->tm_gmtoff);	// It is important that this is an int.

	if (utc)
		settimeofday(ztv, &tz);	// Prevent the next call from adjusting the system clock.
	// Set the RTC/FAT local time offset, and (if not UTC) adjust the system clock from local-time-as-if-UTC to UTC.
	tz.tz_minuteswest = seconds_west / 60;
	settimeofday(ztv, &tz);		
}

#elif defined(__FreeBSD__) || defined(__DragonFly__)

static inline
bool
hwclock_runs_in_UTC()
{
	int oid[CTL_MAXNAME];
	std::size_t len = sizeof oid/sizeof *oid;
	int local(0);			// It is important that this is an int.
	std::size_t siz = sizeof local;

	sysctlnametomib("machdep.wall_cmos_clock", oid, &len);
	sysctl(oid, len, &local, &siz, 0, 0);
	if (local) return true;

	return 0 > access("/etc/wall_cmos_clock", F_OK);
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
}

#elif defined(__NetBSD__)

#error "Don't know what needs to be done about the system clock."

#endif

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
		// This is traditional, and what FreeBSD/TrueOS does.
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

static inline
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

static inline
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

static inline
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

static inline
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

static inline
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

static inline
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

static inline
int
dup(
	const char * prog,
	const FileDescriptorOwner & fd
) {
	const int d(dup(fd.get()));
	if (0 > d) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: dup(%i): %s\n", prog, fd.get(), std::strerror(error));
	} else
		set_non_blocking(d, false);
	return d;
}

static inline
void
dup2(
	const char * prog,
	FileDescriptorOwner filler_stdio[LISTEN_SOCKET_FILENO + 1], 
	const FileDescriptorOwner & new_file,
	int fd
) {
	const int d(dup2(new_file.get(), fd));
	if (0 > d) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: dup2(%i, %i): %s\n", prog, new_file.get(), fd, std::strerror(error));
	} else
		filler_stdio[fd].release();
}

static inline
void
last_resort_io_defaults(
	const bool is_system,
	const char * prog,
	const FileDescriptorOwner & dev_null,
	FileDescriptorOwner saved_stdio[STDERR_FILENO + 1]
) {
	// Populate saved standard input as /dev/null if it was initially closed as we inherited it.
	if (0 > saved_stdio[STDIN_FILENO].get())
		saved_stdio[STDIN_FILENO].reset(dup(prog, dev_null));
	if (is_system) {
		// Always open the console in order to turn on console events.
		FileDescriptorOwner dev_console(open_readwriteexisting_at(AT_FDCWD, "/dev/console"));
		if (0 > dev_console.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "/dev/console", std::strerror(error));
			dev_console.reset(dup(prog, saved_stdio[STDIN_FILENO]));
		} else {
			set_non_blocking(dev_console.get(), false);
#if defined(KBREQ_SIGNAL)
#	if defined(__LINUX__) || defined(__linux__)
			ioctl(dev_console.get(), KDSIGACCEPT, KBREQ_SIGNAL);
#	endif
#endif
		}
		// Populate saved standard output as /dev/console if it was initially closed.
		// The console is the logger of last resort.
		if (0 > saved_stdio[STDOUT_FILENO].get())
			saved_stdio[STDOUT_FILENO].reset(dup(prog, dev_console));
	} else {
		// Populate saved standard output as standard input if it was initially closed.
		// The logger of last resort is whatever standard input is.
		if (0 > saved_stdio[STDOUT_FILENO].get())
			saved_stdio[STDOUT_FILENO].reset(dup(prog, saved_stdio[STDIN_FILENO]));
	}
	// Populate saved standard error as standard output if it was initially closed.
	if (0 > saved_stdio[STDERR_FILENO].get())
		saved_stdio[STDERR_FILENO].reset(dup(prog, saved_stdio[STDOUT_FILENO]));
}

static inline
void
start_system()
{
#if defined(__LINUX__) || defined(__linux__)
	reboot(RB_DISABLE_CAD);
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
#if defined(__LINUX__) || defined(__linux__)
		reboot(RB_POWER_OFF);
#elif defined(__OpenBSD__)
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

static
struct iovec
cgroup_controllers[4] = { 
	MAKE_IOVEC("+cpu"),
	MAKE_IOVEC("+memory"),
	MAKE_IOVEC("+io"),
	MAKE_IOVEC("+pids"),
};

static
const char *
cgroup_paths[] = {
	"",
	"/service-manager.slice"
	// We don't need system-control.slice or *-manager-log.slice because they don't distribute onwards to further groups.
};

static inline
void
initialize_root_control_groups (
	const char * prog
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
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, cgroup_root.c_str(), std::strerror(error));
		return;
	}
	const std::string me_slice(cgroup_root + "/me.slice");
	if (0 > mkdirat(AT_FDCWD, me_slice.c_str(), 0755)) {
		const int error(errno);
		if (EEXIST == error) goto group_exists;
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, me_slice.c_str(), std::strerror(error));
	} else {
group_exists:
		const std::string knobname(me_slice + "/cgroup.procs");
		const FileDescriptorOwner cgroup_procs_fd(open_appendexisting_at(AT_FDCWD, knobname.c_str()));
		if (0 > cgroup_procs_fd.get()
		||  0 > write(cgroup_procs_fd.get(), "0\n", 2)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, knobname.c_str(), std::strerror(error));
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

static
const char *
system_manager_logdirs[] = {
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
		for (const char ** r(system_manager_logdirs); r < system_manager_logdirs + sizeof system_manager_logdirs/sizeof *system_manager_logdirs; ++r)
			if (0 <= chdir(*r))
				return;
	} else
		chdir((effective_user_runtime_dir() + "per-user-manager/log").c_str());
}

static inline
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
					nanosleep(&t, 0);
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

static inline
bool
fork_system_control_as_needed (
	const bool is_system,
	const char * prog,
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const bool verbose(true);
	if (!has_emergency_system_control()) {
		const char * subcommand(0), * option(0);
		if (emergency_signalled) {
			subcommand = "activate";
			option = "emergency";
			emergency_signalled = false;
		} else
			;
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
				args.insert(args.end(), "move-to-control-group");
				args.insert(args.end(), "../system-control.slice");
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
				return true;
			} else
				std::fprintf(stderr, "%s: INFO: %s (pid %i) started (%s%s %s)\n", prog, "system-control", emergency_system_control_pid, subcommand, is_system ? "" : " --user", option ? option : "");
		}
	}
	if (!has_kbreq_system_control()) {
		const char * subcommand(0), * option(0);
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
			;
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
				args.insert(args.end(), "move-to-control-group");
				args.insert(args.end(), "../system-control.slice");
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
				return true;
			} else
				std::fprintf(stderr, "%s: INFO: %s (pid %i) started (%s%s %s)\n", prog, "system-control", kbreq_system_control_pid, subcommand, is_system ? "" : " --user", option ? option : "");
		}
	}
	if (!has_regular_system_control()) {
		const char * subcommand(0), * option(0);
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
			;
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
				args.insert(args.end(), "move-to-control-group");
				args.insert(args.end(), "../system-control.slice");
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

static inline
bool
fork_cyclog_as_needed (
	const bool is_system,
	const char * prog,
	const char * & next_prog,
	std::vector<const char *> & args,
	const FileDescriptorOwner saved_stdio[STDERR_FILENO + 1],
	const FileDescriptorOwner & read_log_pipe
) {
	if (!has_cyclog() && (!stop_signalled() || has_service_manager())) {
		cyclog_pid = fork();
		if (-1 == cyclog_pid) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
		} else if (0 == cyclog_pid) {
			change_to_system_manager_log_root(is_system);
			if (is_system)
				setsid();
			default_all_signals();
			args.clear();
			args.insert(args.end(), "move-to-control-group");
			if (is_system)
				args.insert(args.end(), "../system-manager-log.slice");
			else
				args.insert(args.end(), "../per-user-manager-log.slice");
			args.insert(args.end(), "cyclog");
			args.insert(args.end(), "--max-file-size");
			args.insert(args.end(), "262144");
			args.insert(args.end(), "--max-total-size");
			args.insert(args.end(), "1048576");
			args.insert(args.end(), ".");
			args.insert(args.end(), 0);
			next_prog = arg0_of(args);
			if (-1 != read_log_pipe.get())
				dup2(read_log_pipe.get(), STDIN_FILENO);
			dup2(saved_stdio[STDOUT_FILENO].get(), STDOUT_FILENO);
			dup2(saved_stdio[STDERR_FILENO].get(), STDERR_FILENO);
			close(LISTEN_SOCKET_FILENO);
			return true;
		} else
			std::fprintf(stderr, "%s: INFO: %s (pid %i) started\n", prog, "cyclog", cyclog_pid);
	}
	return false;
}

static inline
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
			args.insert(args.end(), "move-to-control-group");
			args.insert(args.end(), "../service-manager.slice/me.slice");
			args.insert(args.end(), "service-manager");
			args.insert(args.end(), 0);
			next_prog = arg0_of(args);
			dup2(dev_null_fd.get(), STDIN_FILENO);
			if (-1 != write_log_pipe.get()) {
				dup2(write_log_pipe.get(), STDOUT_FILENO);
				dup2(write_log_pipe.get(), STDERR_FILENO);
			}
			dup2(service_manager_socket_fd.get(), LISTEN_SOCKET_FILENO);
			return true;
		} else
			std::fprintf(stderr, "%s: INFO: %s (pid %i) started\n", prog, "service-manager", service_manager_pid);
	}
	return false;
}

/* Main program *************************************************************
// **************************************************************************
*/

static
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
	FileDescriptorOwner filler_stdio[LISTEN_SOCKET_FILENO + 1] = { -1, -1, -1, -1 };
	for (
		FileDescriptorOwner root(open_dir_at(AT_FDCWD, "/")); 
		0 <= root.get() && root.get() <= LISTEN_SOCKET_FILENO; 
		root.reset(dup(root.release()))
	) {
		filler_stdio[root.get()].reset(root.get());
	}

	// The system manager begins with standard I/O connected to a (console) TTY.
	// A per-user manager begins with standard I/O connected to logger services and suchlike.
	// We want to save these, if they are open, for use as log destinations of last resort during shutdown.
	FileDescriptorOwner saved_stdio[STDERR_FILENO + 1] = { -1, -1, -1 };
	for (std::size_t i(0U); i < sizeof saved_stdio/sizeof *saved_stdio; ++i) {
		if (0 > filler_stdio[i].get())
			saved_stdio[i].reset(dup(i));
	}

	// In the normal course of events, standard output and error will be connected to some form of logger process, via a pipe.
	// We don't want our output cluttering a TTY, and device files such as /dev/null and /dev/console do not exist yet.
	FileDescriptorOwner read_log_pipe(-1), write_log_pipe(-1);
	open_logging_pipe(prog, read_log_pipe, write_log_pipe);
	dup2(prog, filler_stdio, write_log_pipe, STDOUT_FILENO);
	dup2(prog, filler_stdio, write_log_pipe, STDERR_FILENO);

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
		0
	);
	if (is_system) {
#if defined(__LINUX__) || defined(__linux__)
		initialize_system_clock_timezone();
#elif defined(__FreeBSD__) || defined(__DragonFly__)
		initialize_system_clock_timezone(prog);
#elif defined(__NetBSD__)
#error "Don't know what needs to be done about the system clock."
#endif
		setup_kernel_api_volumes_and_devices(prog);
	}
	make_needed_run_directories(is_system, prog);
	if (is_system) {
		if (!am_in_jail(envs)) 
			start_system();
	}
	initialize_root_control_groups(prog);

	// Now we can use /dev/console, /dev/null, and the rest.
	const FileDescriptorOwner dev_null_fd(open_null(prog));
	dup2(prog, filler_stdio, dev_null_fd, STDIN_FILENO);
	last_resort_io_defaults(is_system, prog, dev_null_fd, saved_stdio);

	const unsigned listen_fds(query_listen_fds(envs));
	if (listen_fds)
		envs.unset("LISTEN_FDNAMES");

	const FileDescriptorOwner service_manager_socket_fd(listen_service_manager_socket(is_system, prog));

#if defined(DEBUG)	// This is not an emergency mode.  Do not abuse as such.
	if (is_system) {
		const int shell(fork());
		if (-1 == shell) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "fork", std::strerror(error));
		} else if (0 == shell) {
			setsid();
			args.clear();
			args.insert(args.end(), "sh");
			args.insert(args.end(), 0);
			next_prog = arg0_of(args);
			dup2(saved_stdin.get(), STDIN_FILENO);
			dup2(saved_stdout.get(), STDOUT_FILENO);
			dup2(saved_stderr.get(), STDERR_FILENO);
			close(LISTEN_SOCKET_FILENO);
			return;
		}
	}
#endif

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		return;
	}

	std::vector<struct kevent> p(26 + listen_fds);
	unsigned n(0);
	for (unsigned i(0U); i < listen_fds; ++i)
		EV_SET(&p[n++], LISTEN_SOCKET_FILENO + i, EVFILT_READ, EV_ADD, 0, 0, 0);
	if (is_system) {
#if defined(KBREQ_SIGNAL)
		set_event(&p[n++], KBREQ_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
#endif
		set_event(&p[n++], RESCUE_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[n++], EMERGENCY_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
#if defined(SAK_SIGNAL)
		set_event(&p[n++], SAK_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
#endif
#if defined(SIGPWR)
		set_event(&p[n++], SIGPWR, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
#endif
	} else {
		if (SIGINT != REBOOT_SIGNAL) {
			set_event(&p[n++], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		}
		set_event(&p[n++], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[n++], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[n++], SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	}
	set_event(&p[n++], SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	set_event(&p[n++], NORMAL_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	set_event(&p[n++], SYSINIT_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	set_event(&p[n++], HALT_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	set_event(&p[n++], POWEROFF_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
#if defined(POWERCYCLE_SIGNAL)
	set_event(&p[n++], POWERCYCLE_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
#endif
	set_event(&p[n++], REBOOT_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	set_event(&p[n++], FORCE_REBOOT_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
#if defined(FORCE_HALT_SIGNAL)
	set_event(&p[n++], FORCE_HALT_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
#endif
#if defined(FORCE_POWEROFF_SIGNAL)
	set_event(&p[n++], FORCE_POWEROFF_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
#endif
#if defined(FORCE_POWERCYCLE_SIGNAL)
	set_event(&p[n++], FORCE_POWERCYCLE_SIGNAL, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
#endif
	if (0 > kevent(queue.get(), p.data(), n, 0, 0, 0)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
	}

	// Remove any remaining standard I/O filler.
	for (std::size_t i(0U); i < sizeof filler_stdio/sizeof *filler_stdio; ++i)
		filler_stdio[i].reset(-1);

	for (;;) {
		reap_spawned_children(prog);

		// Run system-control if a job is pending and system-control isn't already running.
		if (fork_system_control_as_needed(is_system, prog, next_prog, args)) return;

		// Exit if stop has been signalled and both the service manager and logger have exited.
		if (stop_signalled() && !has_cyclog() && !has_service_manager()) break;

		// Kill the service manager if stop has been signalled.
		if (has_service_manager() && stop_signalled() && !has_any_system_control()) {
			std::fprintf(stderr, "%s: DEBUG: %s\n", prog, "terminating service manager");
			kill(service_manager_pid, SIGTERM);
		}

		// Restart the logger unless both stop has been signalled and the service manager has exited.
		// If the service manager has not exited and stop has been signalled, we still need the logger to restart and keep draining the pipe.
		if (fork_cyclog_as_needed(is_system, prog, next_prog, args, saved_stdio, read_log_pipe)) return;

		// If the service manager has exited and stop has been signalled, close the logging pipe so that the logger finally exits.
		if (!has_service_manager() && stop_signalled() && -1 != read_log_pipe.get()) {
			std::fprintf(stderr, "%s: DEBUG: %s\n", prog, "closing logger");
			for (std::size_t i(0U); i < sizeof saved_stdio/sizeof *saved_stdio; ++i)
				dup2(saved_stdio[i].get(), i);
			read_log_pipe.reset(-1);
			write_log_pipe.reset(-1);
		}

		// Restart the service manager unless stop has been signalled.
		if (fork_service_manager_as_needed(is_system, prog, next_prog, args, dev_null_fd, service_manager_socket_fd, write_log_pipe)) return;

		if (unknown_signalled) {
			std::fprintf(stderr, "%s: WARNING: %s\n", prog, "Unknown signal ignored.");
			unknown_signalled = false;
		}

		const int rc(kevent(queue.get(), p.data(), 0, p.data(), p.size(), 0));
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
					struct kevent e;
					EV_SET(&e, p[i].ident, p[i].filter, EV_EOF | EV_CLEAR, 0, 0, 0);
					kevent(queue.get(), &e, 1, 0, 0, 0);
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
