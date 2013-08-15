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
#include "popt.h"
#include "fdutils.h"

static inline
void
process_env_dir (
	const char * prog,
	const char * dir,
	int scan_dir_fd
) {
	DIR * scan_dir(fdopendir(scan_dir_fd));
	if (!scan_dir) {
exit_scan:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, dir, std::strerror(error));
		throw EXIT_FAILURE;
	}
	for (;;) {
		errno = 0;
		const dirent * entry(readdir(scan_dir));
		if (!entry) {
			if (errno) goto exit_scan;
			break;
		}
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_REG != entry->d_type && DT_LNK != entry->d_type) {
			if (DT_DIR != entry->d_type)
				std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, dir, entry->d_name, "Not a regular file.");
			continue;
		}
#endif
#if defined(_DIRENT_HAVE_D_NAMLEN)
		if (1 > entry->d_namlen) continue;
#endif
		if ('.' == entry->d_name[0]) continue;

		const int var_file_fd(open_read_at(scan_dir_fd, entry->d_name));
		if (0 > var_file_fd) {
bad_file:
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, dir, entry->d_name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		struct stat s;
		if (0 > fstat(var_file_fd, &s)) goto bad_file;
		if (!S_ISREG(s.st_mode)) {
			close(var_file_fd);
			if (!S_ISDIR(s.st_mode))
				std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, dir, entry->d_name, "Not a regular file.");
			continue;
		}
		if (0 == s.st_size) {
			if (0 > unsetenv(entry->d_name)) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, entry->d_name, std::strerror(error));
				close(var_file_fd);
				throw EXIT_FAILURE;
			}
		} else {
			const std::string val(read_env_file(prog, dir, entry->d_name, var_file_fd));
			if (0 > setenv(entry->d_name, val.c_str(), 1)) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dir, entry->d_name, std::strerror(error));
				throw EXIT_FAILURE;
			}
		}
	}
	closedir(scan_dir);
}

/* Main function ************************************************************
// **************************************************************************
*/

void
envdir ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "dir prog");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing directory name.");
		throw EXIT_FAILURE;
	}
	const char * dir(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);
	const int scan_dir_fd(open_dir_at(AT_FDCWD, dir));
	if (0 > scan_dir_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, dir, std::strerror(error));
		throw EXIT_FAILURE;
	}
	process_env_dir(prog, dir, scan_dir_fd);
}
