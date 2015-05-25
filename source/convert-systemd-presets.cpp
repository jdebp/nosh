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
#include <cctype>
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
#include "FileStar.h"

/* System control subcommands ***********************************************
// **************************************************************************
*/

static inline
bool
initial_space (
	const std::string & s
) {
	return s.length() > 0 && std::isspace(s[0]);
}

static inline
bool
wildmat (
	std::string::const_iterator p,
	const std::string::const_iterator & pe,
	std::string::const_iterator n,
	const std::string::const_iterator & ne
) {
	for (;;) {
		if (p == pe && n == ne) return true;
		if (p == pe || n == ne) return false;
		switch (*p) {
			case '*':
				do { ++p; } while (p != pe && '*' == *p);
				for (std::string::const_iterator b(ne); b != n; --b)
					if (wildmat(p, pe, b, ne)) 
						return true;
				break;
			case '\\':
				++p;
				if (p == pe) return false;
				// Fall through to:
			default:
				if (*p != *n) return false;
				// Fall through to:
			case '?':
				++p;
				++n;
				break;
		}
	}
}

static inline
bool
wildmat (
	const std::string & pattern,
	const std::string & name
) {
	return wildmat(pattern.begin(), pattern.end(), name.begin(), name.end());
}

static inline
bool
matches (
	const std::string & pattern,
	const std::string & name,
	const std::string & suffix
) {
	std::string base;
	if (ends_in(pattern, suffix, base))
		return wildmat(base, name);
	else
		return wildmat(pattern, name);
}

static
bool
scan (
	const std::string & name,
	const std::string & suffix,
	FILE * file,
	bool & wants_enable
) {
	for (std::string line; read_line(file, line); ) {
		line = ltrim(line);
		if (line.length() < 1) continue;
		if ('#' == line[0] || ';' == line[0]) continue;
		std::string remainder;
		if (begins_with(line, "enable", remainder) && initial_space(remainder)) {
			remainder = rtrim(ltrim(remainder));
			if (matches(remainder, name, suffix)) {
				wants_enable = true;
				return true;
			}
		} else
		if (begins_with(line, "disable", remainder) && initial_space(remainder)) {
			remainder = rtrim(ltrim(remainder));
			if (matches(remainder, name, suffix)) {
				wants_enable = false;
				return true;
			}
		}
	}
	return false;
}

static
const char *
preset_directories[] = {
	"/run/system-manager/presets/",
	"/run/systemd/system-preset/",
	"/etc/system-manager/presets/",
	"/etc/systemd/system-preset/",
	"/usr/systemd/system-preset/",
	"/usr/lib/systemd/system-preset/",
	"/usr/local/lib/systemd/system-preset/",
};

static inline
bool
systemd_wants_enable_preset (
	const std::string & name,
	const std::string & suffix
) {
	bool wants_enable(true);
	std::string earliest;
	for (size_t i(0); i < sizeof preset_directories/sizeof *preset_directories; ++i) {
		const std::string preset_dir_name(preset_directories[i]);
		const int preset_dir_fd(open_dir_at(AT_FDCWD, preset_dir_name.c_str()));
		if (preset_dir_fd < 0) continue;
		DIR * preset_dir(fdopendir(preset_dir_fd));
		if (!preset_dir) continue;
		for (;;) {
			errno = 0;
			const dirent * entry(readdir(preset_dir));
			if (!entry) break;
#if defined(_DIRENT_HAVE_D_TYPE)
			if (DT_REG != entry->d_type && DT_LNK != entry->d_type) continue;
#endif
#if defined(_DIRENT_HAVE_D_NAMLEN)
			if (1 > entry->d_namlen) continue;
#endif
			if ('.' == entry->d_name[0]) continue;
			const std::string d_name(entry->d_name);
			if (!earliest.empty() && earliest <= d_name) continue;
			const std::string p(preset_dir_name + d_name);
			const int f(open_read_at(preset_dir_fd, entry->d_name));
			if (0 > f) continue;
			FileStar preset_file(fdopen(f, "rt"));
			if (!preset_file) continue;
			if (scan(name, suffix, preset_file, wants_enable))
				earliest = d_name;
		}
		closedir(preset_dir);
	}
	return wants_enable;
}

void
convert_systemd_presets ( 
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
		std::string path, name, suffix;
		const int bundle_dir_fd(open_bundle_directory(*i, path, name, suffix));
		if (0 > bundle_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, *i, std::strerror(error));
			continue;
		}
		const int service_dir_fd(open_service_dir(bundle_dir_fd));
		if (0 > service_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, *i, std::strerror(error));
			close(bundle_dir_fd);
			continue;
		}
		if (systemd_wants_enable_preset(name, suffix)) {
			const int rc(unlinkat(service_dir_fd, "down", 0));
			if (0 > rc && ENOENT != errno) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s%s/%s: %s\n", prog, *i, path.c_str(), name.c_str(), "down", std::strerror(error));
			}
		} else {
			const int fd(open_writecreate_at(service_dir_fd, "down", 0600));
			if (0 > fd) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s%s/%s: %s\n", prog, *i, path.c_str(), name.c_str(), "down", std::strerror(error));
				close(service_dir_fd);
				close(bundle_dir_fd);
				continue;
			}
			close(fd);
		}
		close(service_dir_fd);
		close(bundle_dir_fd);
	}

	throw EXIT_SUCCESS;
}
