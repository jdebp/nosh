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
#include "popt.h"
#include "FileStar.h"
#include "FileDescriptorOwner.h"
#include "DirStar.h"

/* Helper routines **********************************************************
// **************************************************************************
*/

static inline
int
open_env_dir (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * service,
	std::string & path,
	std::string & name
) {
	std::string suffix;
	const int bundle_dir_fd(open_bundle_directory(envs, "", service, path, name, suffix));
	if (0 > bundle_dir_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, service, std::strerror(error));
		throw EXIT_FAILURE;
	}
	const int service_dir_fd(open_service_dir(bundle_dir_fd));
	if (0 > service_dir_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, service, "service/", std::strerror(error));
		close(bundle_dir_fd);
		throw EXIT_FAILURE;
	}
	close(bundle_dir_fd);
	const int env_dir_fd(open_dir_at(service_dir_fd, "env/"));
	if (0 > env_dir_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s/%s%s: %s\n", prog, service, "service/", "env/", std::strerror(error));
		close(service_dir_fd);
		throw EXIT_FAILURE;
	}
	close(service_dir_fd);
	return env_dir_fd;
}

static inline
void
set (
	bool full,
	FILE * f,
	const char * val
) {
	bool last_lf(false);
	for (int c(*val++); '\0' != c; c = *val++) {
		if (!full && '\n' == c) c = '\0';
		std::fputc(c, f);
		last_lf = '\n' == c;
	}
	if (!last_lf) std::fputc('\n', f);
}

static inline
void
print ( 
	bool full,
	FILE * f 
) {
	bool last_lf(false);
	for (int c(std::fgetc(f)); EOF != c; c = std::fgetc(f)) {
		if (!full && '\n' == c) break;
		if (!c) c = '\n';
		std::cout.put(static_cast<char>(c));
		last_lf = '\n' == c;
	}
	if (!last_lf) std::cout.put('\n');
}

/* System control subcommands ***********************************************
// **************************************************************************
*/

void
print_service_env [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool full(false);
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::bool_definition full_option('f', "full", "Read the full contents of each file.", full);
		popt::definition * main_table[] = {
			&full_option,
			&user_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "{service} [name]");

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

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing service name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * const service(args.front());
	args.erase(args.begin());

	std::string path, name;
	FileDescriptorOwner env_dir_fd(open_env_dir(prog, envs, service, path, name));

	if (args.empty()) {
		// No second argument; print all variables and their values (c.f. "printenv").
		const DirStar env_dir(env_dir_fd);
		if (!env_dir) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s%s/%s%s: %s\n", prog, path.c_str(), name.c_str(), "service/", "env/", std::strerror(error));
			throw EXIT_FAILURE;
		}
		for (;;) {
			errno = 0;
			const dirent * entry(readdir(env_dir));
			if (!entry) {
				const int error(errno);
				if (error) {
					std::fprintf(stderr, "%s: ERROR: %s%s/%s%s: %s\n", prog, path.c_str(), name.c_str(), "service/", "env/", std::strerror(error));
					throw EXIT_FAILURE;
				}
				break;
			}
#if defined(_DIRENT_HAVE_D_TYPE)
			if (DT_REG != entry->d_type && DT_LNK != entry->d_type) continue;
#endif
			if ('.' == entry->d_name[0]) continue;
			const int var_file_fd(open_read_at(env_dir.fd(), entry->d_name));
			if (0 > var_file_fd) {
bad_file:
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s%s/%s%s%s: %s\n", prog, path.c_str(), name.c_str(), "service/", "env/", entry->d_name, std::strerror(error));
				if (0 <= var_file_fd) close(var_file_fd);
				continue;
			}
			struct stat s;
			if (0 > fstat(var_file_fd, &s)) goto bad_file;
			if (!S_ISREG(s.st_mode)) {
				close(var_file_fd);
				if (!S_ISDIR(s.st_mode))
					std::fprintf(stderr, "%s: ERROR: %s%s/%s%s%s: %s\n", prog, path.c_str(), name.c_str(), "service/", "env/", entry->d_name, "Not a regular file.");
				continue;
			}
			if (0 == s.st_size) {
				std::cout << entry->d_name << '\n';
				close(var_file_fd);
				continue;
			}
			FileStar f(fdopen(var_file_fd, "r"));
			if (!f) continue;
			std::cout << entry->d_name << '=';
			print(full, f);
		}
	} else {
		// Explicit second argument; print just that variable's value (c.f. "printenv VAR").
		const char * const raw_var(args.front());
		args.erase(args.begin());
		if (!args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
			throw static_cast<int>(EXIT_USAGE);
		}

		const std::string var('/' == *raw_var ? std::string(".") + raw_var : std::string(raw_var));
		const int var_fd(open_read_at(env_dir_fd.get(), var.c_str()));
		if (0 > var_fd) throw EXIT_FAILURE;

		FileStar f(fdopen(var_fd, "r"));
		if (!f) throw EXIT_FAILURE;
		print(full, f);
	}

	throw EXIT_SUCCESS;
}

void
set_service_env [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool full(false);
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::bool_definition full_option('f', "full", "Read the full contents of each file.", full);
		popt::definition * main_table[] = {
			&full_option,
			&user_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "{service} {name} [value]");

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

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing service name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * const service(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing variable name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * const raw_var(args.front());
	args.erase(args.begin());

	std::string path, name;
	FileDescriptorOwner env_dir_fd(open_env_dir(prog, envs, service, path, name));

	const std::string var('/' == *raw_var ? std::string(".") + raw_var : std::string(raw_var));
	FileDescriptorOwner var_fd(open_writetrunc_at(env_dir_fd.get(), var.c_str(), 0644));
	if (0 > var_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s%s/%s%s%s: %s\n", prog, path.c_str(), name.c_str(), "service/", "env/", var.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}

	if (args.empty()) {
		// No third argument; configure the envdir to explicitly unset the variable.
		// Which means doing nothing at all, since we've just truncated the file to zero size.
	} else {
		// Explicit third argument; configure the envdir to set that variable to the given value.
		const char * const val(args.front());
		args.erase(args.begin());
		if (!args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
			throw static_cast<int>(EXIT_USAGE);
		}

		FileStar f(fdopen(var_fd.get(), "w"));
		if (!f) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s%s/%s%s%s: %s\n", prog, path.c_str(), name.c_str(), "service/", "env/", var.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
		var_fd.release();
		set(full, f, val);
	}

	throw EXIT_SUCCESS;
}
