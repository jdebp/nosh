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
checkyesno (
	const std::string & s
) {
	if ("0" == s) return false;
	if ("1" == s) return true;
	const std::string l(tolower(s));
	if ("no" == l) return false;
	if ("yes" == l) return true;
	if ("false" == l) return false;
	if ("true" == l) return true;
	if ("off" == l) return false;
	if ("on" == l) return true;
	return false;
}

static
const char *
rcconf_files[] = {
	"/etc/rc.conf.local",
	"/etc/rc.conf",
};

static inline
bool
rcconf_wants_enable_preset (
	const char * prog,
	const std::string & name
) {
	const std::string wanted(name + "_enable");
	for (size_t i(0); i < sizeof rcconf_files/sizeof *rcconf_files; ++i) {
		const std::string rcconf_file(rcconf_files[i]);
		FILE * f(std::fopen(rcconf_file.c_str(), "r"));
		if (!f) continue;
		std::vector<std::string> env_strings(read_file(prog, rcconf_file.c_str(), f));
		for (std::vector<std::string>::const_iterator j(env_strings.begin()); j != env_strings.end(); ++j) {
			const std::string & s(*j);
			const std::string::size_type p(s.find('='));
			const std::string var(s.substr(0, p));
			if (var != wanted) continue;
			const std::string val(p == std::string::npos ? std::string() : s.substr(p + 1, std::string::npos));
			std::fprintf(stderr, "%s: INFO: %s: checking %s\n", prog, var.c_str(), val.c_str());
			return checkyesno(val);
		}
	}
	return false;
}

void
convert_rcconf_presets ( 
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
		std::string path, name;
		const int bundle_dir_fd(open_bundle_directory(*i, path, name));
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
		if (rcconf_wants_enable_preset(prog, name)) {
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
