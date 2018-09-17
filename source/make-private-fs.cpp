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

void
make_private_fs ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool temp(false), devices(false), homes(false);
	std::list<std::string> hide_directories;
	try {
		popt::bool_definition temp_option('t', "temp", "Make a private /tmp also mounted as /var/tmp.", temp);
		popt::bool_definition devices_option('d', "devices", "Make a private /dev with no non-API devices.", devices);
		popt::bool_definition homes_option('h', "homes", "Make a private /home with no homes.", homes);
		popt::string_list_definition hide_option('h', "hide", "directory", "Make this directory private.", hide_directories);
		popt::definition * top_table[] = {
			&temp_option,
			&devices_option,
			&homes_option,
			&hide_option
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

#if defined(__LINUX__) || defined(__linux__)
	const int bind_mount_flags(MS_BIND|MS_REC);
#else
	const int bind_mount_flags(0);
#endif

	if (temp) {
		static const char * tempdirs[] = {
			"/tmp",
			"/var/tmp",
		};
		for (const char ** i(tempdirs); (tempdirs + sizeof tempdirs/sizeof *tempdirs) != i; ++i) {
			char name[4096];
			std::strcpy(name, *i);
			std::strcat(name, "/private-fs.XXXXXX");

			// The top-level directory grants no group or world access, so that no unprivileged users from the outside can go poking around in it.
			const mode_t oldmode1(umask(0077));
			if (0 == mkdtemp(name)) {
				const int error(errno);
				umask(oldmode1);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
				throw EXIT_FAILURE;
			}
			umask(oldmode1);

			// An extra directory level is created underneath, which has trwxrwxrwx permissions just like a real /tmp .
			const mode_t oldmode2(umask(0000));
			std::strcat(name, "/tmp");
			if (0 > mkdir(name, 0777 | S_ISVTX)) {
				const int error(errno);
				umask(oldmode2);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
				throw EXIT_FAILURE;
			}
			umask(oldmode2);

			struct iovec iov[] = {
				FSPATH,			{ const_cast<char *>(*i), std::strlen(*i) + 1 },
				FROM,			{ name, std::strlen(name) + 1 },
#if defined(__LINUX__) || defined(__linux__)
				FSTYPE,			MAKE_IOVEC(""),
#else
				FSTYPE,			MAKE_IOVEC("nullfs"),
#endif
			};

			if (0 > nmount(iov, sizeof iov/sizeof *iov, bind_mount_flags)) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
				throw EXIT_FAILURE;
			}
		}
	}
	if (devices) {
#if defined(__LINUX__) || defined(__linux__)
		static const struct iovec dev[] = {
			FSTYPE,			MAKE_IOVEC("tmpfs"),
			FSPATH,			MAKE_IOVEC("/dev"),
			MAKE_IOVEC("mode"),	MAKE_IOVEC("0755"),
			MAKE_IOVEC("size"),	MAKE_IOVEC("10M"),
		};
		static const struct iovec pts[] = {
			FSTYPE,			MAKE_IOVEC("devpts"),
			FSPATH,			MAKE_IOVEC("/dev/pts"),
			MAKE_IOVEC("newinstance"),	{0,0},
			MAKE_IOVEC("ptmxmode"),	MAKE_IOVEC("0666"),
			MAKE_IOVEC("mode"),	MAKE_IOVEC("0620"),
			// Yes, we're hardwiring the GID of the "tty" group to what it conventionally is in Linux distributions.
			// It's this, or have the whole nsswitch mechanism loaded into process #1 just to read /etc/groups.
			MAKE_IOVEC("gid"),	MAKE_IOVEC("5"),
		};
#else
		static const struct iovec dev[] = {
			FSTYPE,			MAKE_IOVEC("devfs"),
			FSPATH,			MAKE_IOVEC("/dev"),
		};
		static const struct iovec fds[] = {
			FSTYPE,			MAKE_IOVEC("fdescfs"),
			FSPATH,			MAKE_IOVEC("/dev/fd"),
		};
#endif

#define MAKE_DATA(x) # x, const_cast<struct iovec *>(x), sizeof x/sizeof *x

		static const struct api_mount mounts[] = 
		{
#if defined(__LINUX__) || defined(__linux__)
			{	MAKE_DATA(dev),		0U,	MS_NOSUID|MS_STRICTATIME|MS_NOEXEC		},
			{	MAKE_DATA(pts),		0U,	MS_NOSUID|MS_STRICTATIME|MS_NOEXEC		},
#else
			{	MAKE_DATA(dev),		0U,	MNT_NOSUID|MNT_NOEXEC				},
			{	MAKE_DATA(fds),		0U,	MNT_NOSUID|MNT_NOEXEC				},
#endif
		};

		const static struct api_symlink symlinks[] = 
		{
#if defined(__LINUX__) || defined(__linux__)
			{	1,	"/dev/ptmx",	"pts/ptmx"		},
			{	0,	"/dev/fd",	"/proc/self/fd"		},
//			{	0,	"/dev/core",	"/proc/kcore"		},
			{	0,	"/dev/stdin",	"/proc/self/fd/0"	},
			{	0,	"/dev/stdout",	"/proc/self/fd/1"	},
			{	0,	"/dev/stderr",	"/proc/self/fd/2"	},
			{	0,	"/dev/shm",	"/run/shm"		},
#else
			{	0,	"/dev/urandom",	"random"		},
			{	0,	"/dev/shm",	"/run/shm"		},
#endif
		};

		const char * device_files[] = {
			"null",
			"zero",
#if defined(__LINUX__) || defined(__linux__)
			"full",
			"urandom",
#endif
			"random",
			"tty",
		};

		const FileDescriptorOwner old_dev_fd(open_dir_at(AT_FDCWD, "/dev"));
		if (0 > old_dev_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "/dev", std::strerror(error));
			throw EXIT_FAILURE;
		}
		for (const api_mount * i(mounts); (mounts + sizeof mounts/sizeof *mounts) != i; ++i) {
			const std::string fspath(fspath_from_mount(i->iov, i->ioc));
			if (!fspath.empty()) {
				if (0 > mkdir(fspath.c_str(), 0700)) {
					const int error(errno);
					if (EEXIST != error)
						std::fprintf(stderr, "%s: ERROR: %s: %s: %s\n", prog, "mkdir", fspath.c_str(), std::strerror(error));
				}
			}
			if (0 > nmount(i->iov, i->ioc, i->flags)) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, "nmount", i->name, std::strerror(error));
				throw EXIT_FAILURE;
			}
		}
		for (const api_symlink * i(symlinks); (symlinks + sizeof symlinks/sizeof *symlinks) != i; ++i) {
			for (int force = !!i->force ; ; --force) {
				if (0 <= symlink(i->target, i->name)) break;
				const int error(errno);
				if (!force || EEXIST != error) {
					std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, "symlink", i->name, std::strerror(error));
					throw EXIT_FAILURE;
				}
				unlink(i->name);
			}
		}
		const FileDescriptorOwner new_dev_fd(open_dir_at(AT_FDCWD, "/dev"));
		if (0 > new_dev_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "/dev", std::strerror(error));
			throw EXIT_FAILURE;
		}
		for (const char ** i(device_files); (device_files + sizeof device_files/sizeof *device_files) != i; ++i) {
			struct stat s;
			if (0 > fstatat(old_dev_fd.get(), *i, &s, 0)) continue;
			if (!S_ISBLK(s.st_mode) && !S_ISCHR(s.st_mode)) continue;
			if (0 > mknodat(new_dev_fd.get(), *i, s.st_mode, s.st_rdev)) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s: %s\n", prog, "mknod", *i, std::strerror(error));
				throw EXIT_FAILURE;
			}
		}
	}
	if (homes || !hide_directories.empty()) {
		static struct homedir {
			bool fatal;
			const char * name;
		} const homedirs[] = {
			{ true, "/root" },
#if defined(__LINUX__) || defined(__linux__)
			{ true, "/home" },
#else
			{ true, "/usr/home" },
#endif
			{ false, "/run/user" },
		};

		char name[4096];
		std::strcpy(name, "/tmp/private-fs.XXXXXX");
		// The top-level directory grants no group or world access, so that no unprivileged users from the outside can go poking around in it.
		const mode_t oldmode1(umask(0077));
		if (0 == mkdtemp(name)) {
			const int error(errno);
			umask(oldmode1);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		umask(oldmode1);

		// An extra directory level is created underneath, which has -rwx------- permissions just like a real secure home directory.
		const mode_t oldmode2(umask(0000));
		std::strcat(name, "/tmp");
		if (0 > mkdir(name, 0700)) {
			const int error(errno);
			umask(oldmode2);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		umask(oldmode2);

		for (const struct homedir * i(homedirs); (homedirs + sizeof homedirs/sizeof *homedirs) != i; ++i) {
			struct iovec iov[] = {
				FSPATH,			{ const_cast<char *>(i->name), std::strlen(i->name) + 1 },
				FROM,			{ name, std::strlen(name) + 1 },
#if defined(__LINUX__) || defined(__linux__)
				FSTYPE,			MAKE_IOVEC(""),
#else
				FSTYPE,			MAKE_IOVEC("nullfs"),
#endif
			};

			if (0 > nmount(iov, sizeof iov/sizeof *iov, bind_mount_flags)) {
				const int error(errno);
				if (i->fatal) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, i->name, std::strerror(error));
					throw EXIT_FAILURE;
				} else
					std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, i->name, std::strerror(error));
			}
		}
		for (std::list<std::string>::const_iterator e(hide_directories.end()), i(hide_directories.begin()); e != i; ++i) {
			struct iovec iov[] = {
				FSPATH,			{ const_cast<char *>(i->c_str()), i->length() + 1 },
				FROM,			{ name, std::strlen(name) + 1 },
#if defined(__LINUX__) || defined(__linux__)
				FSTYPE,			MAKE_IOVEC(""),
#else
				FSTYPE,			MAKE_IOVEC("nullfs"),
#endif
			};

			if (0 > nmount(iov, sizeof iov/sizeof *iov, bind_mount_flags)) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, i->c_str(), std::strerror(error));
				throw EXIT_FAILURE;
			}
		}
	}
}
