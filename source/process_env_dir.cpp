/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "utils.h"
#include "fdutils.h"
#include "ProcessEnvironment.h"
#include "FileDescriptorOwner.h"
#include "DirStar.h"

bool
process_env_dir (
	const char * prog,
	ProcessEnvironment & envs,
	const char * dir,
	int scan_dir_fd,
	bool ignore_errors,
	bool full,
	bool chomp
) {
	const DirStar scan_dir(fdopendir(scan_dir_fd));
	if (!scan_dir) {
exit_scan:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, dir, std::strerror(error));
		if (!scan_dir) close(scan_dir_fd);
		return false;
	}
	for (;;) {
		errno = 0;
		const dirent * entry(readdir(scan_dir));
		if (!entry) {
			if (errno) goto exit_scan;
			break;
		}
#if defined(_DIRENT_HAVE_D_NAMLEN)
		if (1 > entry->d_namlen) continue;
#endif
		if ('.' == entry->d_name[0]) continue;
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_REG != entry->d_type && DT_LNK != entry->d_type) {
			if (DT_DIR != entry->d_type)
				std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, dir, entry->d_name, "Not a regular file.");
			continue;
		}
#endif

		const FileDescriptorOwner var_file_fd(open_read_at(scan_dir_fd, entry->d_name));
		if (0 > var_file_fd.get()) {
bad_file:
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, dir, entry->d_name, std::strerror(error));
			if (!ignore_errors)
				return false;
			continue;
		}
		struct stat s;
		if (0 > fstat(var_file_fd.get(), &s)) goto bad_file;
		if (!S_ISREG(s.st_mode)) {
			if (!S_ISDIR(s.st_mode))
				std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, dir, entry->d_name, "Not a regular file.");
			continue;
		}
		if (0 == s.st_size) {
			if (!envs.unset(entry->d_name)) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, entry->d_name, std::strerror(error));
				if (!ignore_errors)
					return false;
			}
		} else {
			const std::string val(read_env_file(prog, dir, entry->d_name, var_file_fd.get(), full, chomp));
			if (!envs.set(entry->d_name, val)) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dir, entry->d_name, std::strerror(error));
				if (!ignore_errors)
					return false;
			}
		}
	}
	return true;
}
