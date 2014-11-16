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
#include <ttyent.h>
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
ttys_wants_enable_preset (
	const std::string & name
) {
	const struct ttyent *entry(getttynam(name.c_str()));
	const bool r(entry && (entry->ty_status & TTY_ON));
	endttyent();
	return r;
}

static inline
std::string
strip_if_prefix (
	const std::string & s,
	const std::string & p
) {
	const std::string::size_type l(p.length());
	if (s.length() < l || 0 != memcmp(s.c_str(), p.c_str(), l)) return s;
	return s.substr(l, std::string::npos);
}

void
convert_ttys_presets ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	const char * prefix("");
	try {
		popt::string_definition prefix_option('p', "prefix", "string", "Prefix each name with this (template) name.", prefix);
		popt::definition * main_table[] = {
			&prefix_option
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

	const std::string p(prefix);
	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		std::string service(p + *i), path, name;
		const int bundle_dir_fd(open_bundle_directory(service.c_str(), path, name));
		if (0 > bundle_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, service.c_str(), std::strerror(error));
			continue;
		}
		const int service_dir_fd(open_service_dir(bundle_dir_fd));
		if (0 > service_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, service.c_str(), std::strerror(error));
			close(bundle_dir_fd);
			continue;
		}
		if (ttys_wants_enable_preset(strip_if_prefix(name, p))) {
			const int rc(unlinkat(service_dir_fd, "down", 0));
			if (0 > rc && ENOENT != errno) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s%s/%s: %s\n", prog, service.c_str(), path.c_str(), name.c_str(), "down", std::strerror(error));
			}
		} else {
			const int fd(open_writecreate_at(service_dir_fd, "down", 0600));
			if (0 > fd) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s%s/%s: %s\n", prog, service.c_str(), path.c_str(), name.c_str(), "down", std::strerror(error));
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
