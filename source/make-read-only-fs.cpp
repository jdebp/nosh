/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <unistd.h>
#include "utils.h"
#include "nmount.h"
#include "common-manager.h"	// Because we make API mounts too.
#include "fdutils.h"
#include "popt.h"
#include "FileDescriptorOwner.h"

/* Main function ************************************************************
// **************************************************************************
*/

static
void
mount_or_remount_readonly (
	const char * prog,
	const char * where
) {
	const FileDescriptorOwner fd(open_dir_at(AT_FDCWD, where));
	if (0 > fd.get()) {
		const int error(errno);
		if (ENOENT != error) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, where, std::strerror(error));
			throw EXIT_FAILURE;
		} else
			return;
	}

	struct iovec remount_iov[] = {
		FSPATH,			{ const_cast<char *>(where), std::strlen(where) + 1 },
		FSTYPE,			MAKE_IOVEC(""),
	};
#if defined(__LINUX__) || defined(__linux__)
	const int remount_flags(MS_REMOUNT|MS_RDONLY);
#else
	const int remount_flags(MNT_UPDATE|MNT_RDONLY);
#endif

	if (0 > nmount(remount_iov, sizeof remount_iov/sizeof *remount_iov, remount_flags)) {
		const int error(errno);
		if (EINVAL != error && EBUSY != error) {
			std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, "nmount", where, std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	struct iovec bind_mount_iov[] = {
		FSPATH,			{ const_cast<char *>(where), std::strlen(where) + 1 },
		FROM,			{ const_cast<char *>(where), std::strlen(where) + 1 },
#if defined(__LINUX__) || defined(__linux__)
		FSTYPE,			MAKE_IOVEC(""),
#else
		FSTYPE,			MAKE_IOVEC("nullfs"),
#endif
	};
#if defined(__LINUX__) || defined(__linux__)
	const int bind_mount_flags(MS_BIND|MS_REC|MS_RDONLY);
#else
	const int bind_mount_flags(MNT_RDONLY);
#endif

	if (0 > nmount(bind_mount_iov, sizeof bind_mount_iov/sizeof *bind_mount_iov, bind_mount_flags)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, "nmount", where, std::strerror(error));
		throw EXIT_FAILURE;
	}
#if defined(__LINUX__) || defined(__linux__)
	const int bind_remount_flags(MS_REMOUNT|MS_BIND|MS_RDONLY);
	if (0 > nmount(remount_iov, sizeof remount_iov/sizeof *remount_iov, bind_remount_flags)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, "nmount", where, std::strerror(error));
		throw EXIT_FAILURE;
	}
#endif
}

void
make_read_only_fs ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool os(false), etc(false), homes(false), sysctl(false), cgroups(false);
	std::list<std::string> include_directories;
	std::list<std::string> except_directories;
	try {
		popt::bool_definition os_option('o', "os", "Make a read-only /usr.", os);
		popt::bool_definition etc_option('e', "etc", "Make a read-only /etc.", etc);
		popt::bool_definition homes_option('d', "homes", "Make a read-only /home.", homes);
		popt::bool_definition sysctl_option('s', "sysctl", "Make a read-only /sys, /proc/sys, et al..", sysctl);
		popt::bool_definition cgroups_option('c', "cgroups", "Make a read-only /sys/fs/cgroup.", cgroups);
		popt::string_list_definition include_option('i', "include", "directory", "Make this directory read-only.", include_directories);
		popt::string_list_definition except_option('x', "except", "directory", "Retain this directory as read-write.", except_directories);
		popt::definition * top_table[] = {
			&os_option,
			&etc_option,
			&homes_option,
			&sysctl_option,
			&cgroups_option,
			&include_option,
			&except_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (etc) {
		mount_or_remount_readonly(prog, "/etc");
#if !defined(__LINUX__) && !defined(__linux__)
		mount_or_remount_readonly(prog, "/usr/local/etc");
#endif
	}
	if (os) {
		mount_or_remount_readonly(prog, "/usr");
		mount_or_remount_readonly(prog, "/boot");
#if defined(__LINUX__) || defined(__linux__)
		mount_or_remount_readonly(prog, "/efi");
#else
		mount_or_remount_readonly(prog, "/lib");
		mount_or_remount_readonly(prog, "/libexec");
		mount_or_remount_readonly(prog, "/bin");
		mount_or_remount_readonly(prog, "/sbin");
#endif
	}
	if (homes) {
		mount_or_remount_readonly(prog, "/root");
#if defined(__LINUX__) || defined(__linux__)
		mount_or_remount_readonly(prog, "/home");
		mount_or_remount_readonly(prog, "/run/user");
#else
		mount_or_remount_readonly(prog, "/usr/home");
#endif
	}
	if (sysctl) {
#if defined(__LINUX__) || defined(__linux__)
		mount_or_remount_readonly(prog, "/proc/sys");
		mount_or_remount_readonly(prog, "/proc/acpi");
		mount_or_remount_readonly(prog, "/proc/apm");
		mount_or_remount_readonly(prog, "/proc/asound");
		mount_or_remount_readonly(prog, "/proc/bus");
		mount_or_remount_readonly(prog, "/proc/fs");
		mount_or_remount_readonly(prog, "/proc/irq");
		mount_or_remount_readonly(prog, "/sys");
		mount_or_remount_readonly(prog, "/sys/fs/pstore");
		mount_or_remount_readonly(prog, "/sys/kernel/debug");
		mount_or_remount_readonly(prog, "/sys/kernel/tracing");
#elif defined(__FreeBSD__)
		mount_or_remount_readonly(prog, "/compat/linux/proc/sys");
		mount_or_remount_readonly(prog, "/compat/linux/sys");
#endif
	}
	if (cgroups) {
#if defined(__LINUX__) || defined(__linux__)
		mount_or_remount_readonly(prog, "/sys/fs/cgroup");
#endif
	}
	for (std::list<std::string>::const_iterator e(include_directories.end()), i(include_directories.begin()); e != i; ++i) {
		mount_or_remount_readonly(prog, e->c_str());
	}
	for (std::list<std::string>::const_iterator e(except_directories.end()), i(except_directories.begin()); e != i; ++i) {
		mount_or_remount_readonly(prog, e->c_str());
	}
}
