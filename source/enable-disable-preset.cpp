/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <map>
#include <set>
#include <iostream>
#include <cstddef>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <new>
#include <memory>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "service-manager-client.h"
#include "service-manager.h"
#include "common-manager.h"
#include "popt.h"


/* Utilities ****************************************************************
// **************************************************************************
*/

static inline
int
open_bundle_directory (
	const char * arg,
	std::string & path
) {
	const std::string a(arg);
	if (std::strchr(arg, '/')) {
		path = a;
		return open_dir_at(AT_FDCWD, (path + "/").c_str());
	}

	if (!local_session_mode) {
		for ( const char * const * q(bundle_prefixes); q < bundle_prefixes + sizeof bundle_prefixes/sizeof *bundle_prefixes; ++q) {
			const std::string p(*q);
			const std::string suffix(p + a);
			for ( const char * const * proot(roots); proot < roots + sizeof roots/sizeof *roots; ++proot) {
				const std::string r(*proot);
				path = r + suffix;
				const int bundle_dir_fd(open_dir_at(AT_FDCWD, (path + "/").c_str()));
				if (0 > bundle_dir_fd) continue;
				return bundle_dir_fd;
			}
		}
	}

	return -1;
}

/* System control subcommands ***********************************************
// **************************************************************************
*/

static inline
void
enable_disable ( 
	const char * prog,
	bool make,
	const std::string & path,
	const int bundle_dir_fd,
	const std::string & source,
	const std::string & target
) {
	const std::string source_dir_name(path + "/" + source);
	const std::string base(basename_of(path.c_str()));
	const int source_dir_fd(open_dir_at(bundle_dir_fd, (source + "/").c_str()));
	if (source_dir_fd < 0) {
		const int error(errno);
		if (ENOENT != error)
			std::fprintf(stderr, "%s: WARNING: %s: %s\n", prog, source_dir_name.c_str(), std::strerror(error));
		return;
	}
	DIR * source_dir(fdopendir(source_dir_fd));
	if (!source_dir) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, source_dir_name.c_str(), std::strerror(error));
		return;
	}
	for (;;) {
		errno = 0;
		const dirent * entry(readdir(source_dir));
		if (!entry) {
			const int error(errno);
			if (error)
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, source_dir_name.c_str(), std::strerror(error));
			break;
		}
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_DIR != entry->d_type && DT_LNK != entry->d_type) continue;
#endif
#if defined(_DIRENT_HAVE_D_NAMLEN)
		if (1 > entry->d_namlen) continue;
#endif
		if ('.' == entry->d_name[0]) continue;

		const int target_dir_fd(open_dir_at(source_dir_fd, entry->d_name));
		if (0 > target_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, source_dir_name.c_str(), entry->d_name, std::strerror(error));
			continue;
		}
		if (make)
			mkdirat(target_dir_fd, target.c_str(), 0755);
		if (0 > unlinkat(target_dir_fd, (target + "/" + base).c_str(), 0)) {
			const int error(errno);
			if (ENOENT != error)
				std::fprintf(stderr, "%s: ERROR: %s/%s/%s/%s: %s\n", prog, source_dir_name.c_str(), entry->d_name, target.c_str(), base.c_str(), std::strerror(error));
		}
		if (make) {
			if (0 > symlinkat(path.c_str(), target_dir_fd, (target + "/" + base).c_str())) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s/%s/%s/%s: %s\n", prog, source_dir_name.c_str(), entry->d_name, target.c_str(), base.c_str(), std::strerror(error));
			}
		}
		close(target_dir_fd);
	}
	closedir(source_dir);
}

static inline
bool
determine_preset (
	const char * prog,
	const char * arg
) {
	std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, arg, "Don't know how to determine preset yet.");
	throw EXIT_FAILURE;
}

void
enable ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
		popt::definition * main_table[] = {
			&user_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "service(s)...");

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

	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		std::string path;
		const int bundle_dir_fd(open_bundle_directory(*i, path));
		if (0 > bundle_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, *i, std::strerror(error));
			continue;
		}
		enable_disable(prog, true, path, bundle_dir_fd, "wanted-by", "wants");
		enable_disable(prog, true, path, bundle_dir_fd, "stopped-by", "conflicts");
		enable_disable(prog, true, path, bundle_dir_fd, "stopped-by", "after");
		close(bundle_dir_fd);
	}

	throw EXIT_SUCCESS;
}

void
disable ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
		popt::definition * main_table[] = {
			&user_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "service(s)...");

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

	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		std::string path;
		const int bundle_dir_fd(open_bundle_directory(*i, path));
		if (0 > bundle_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, *i, std::strerror(error));
			continue;
		}
		enable_disable(prog, false, path, bundle_dir_fd, "wanted-by", "wants");
		enable_disable(prog, false, path, bundle_dir_fd, "stopped-by", "conflicts");
		enable_disable(prog, false, path, bundle_dir_fd, "stopped-by", "after");
		close(bundle_dir_fd);
	}

	throw EXIT_SUCCESS;
}

void
preset ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
		popt::definition * main_table[] = {
			&user_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "service(s)...");

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

	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		std::string path;
		const int bundle_dir_fd(open_bundle_directory(*i, path));
		if (0 > bundle_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, *i, std::strerror(error));
			continue;
		}
		const bool make(determine_preset(prog, *i));
		enable_disable(prog, make, path, bundle_dir_fd, "wanted-by", "wants");
		enable_disable(prog, make, path, bundle_dir_fd, "stopped-by", "conflicts");
		enable_disable(prog, make, path, bundle_dir_fd, "stopped-by", "after");
		close(bundle_dir_fd);
	}

	throw EXIT_SUCCESS;
}
