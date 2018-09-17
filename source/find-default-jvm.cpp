/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "ProcessEnvironment.h"
#include "FileStar.h"
#include "DirStar.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

static const char * path[] = { "/usr/local", "/usr" };

void
find_default_jvm ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
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

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing next program.");
		throw static_cast<int>(EXIT_USAGE);
	}
	if (envs.query("JAVA_HOME")) return;

	const FileStar f(std::fopen("/usr/local/etc/javavms", "r"));
	if (f) {
		const std::vector<std::string> java_strings(read_file(f));
		if (std::ferror(f)) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "/usr/local/etc/javavms", std::strerror(error));
			throw EXIT_FAILURE;
		}
		if (!java_strings.empty()) {
			const std::string root(dirname_of(dirname_of(java_strings.front())));
			if (!envs.set("JAVA_HOME", root.c_str())) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "JAVA_HOME", std::strerror(error));
				throw EXIT_FAILURE;
			}
			return;
		}
	}

	FileDescriptorOwner d(open_dir_at(AT_FDCWD, "/usr/lib/jvm"));
	if (0 <= d.get()) {
		if (0 <= faccessat(d.get(), "default-java/", F_OK, AT_EACCESS)) {
			if (!envs.set("JAVA_HOME", "/usr/lib/jvm/default-java")) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "JAVA_HOME", std::strerror(error));
				throw EXIT_FAILURE;
			}
			return;
		}
		if (0 <= faccessat(d.get(), "default-runtime/", F_OK, AT_EACCESS)) {
			if (!envs.set("JAVA_HOME", "/usr/lib/jvm/default-runtime")) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "JAVA_HOME", std::strerror(error));
				throw EXIT_FAILURE;
			}
			return;
		}
	}

	for (const char * const * i(path); i < path + sizeof path/sizeof *path; ++i) {
		if (0 > faccessat(AT_FDCWD, (std::string(*i) + "/bin/java").c_str(), F_OK, AT_EACCESS)) continue;
		if (!envs.set("JAVA_HOME", *i)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "JAVA_HOME", std::strerror(error));
			throw EXIT_FAILURE;
		}
		return;
	}

	if (!envs.set("JAVA_HOME", "/")) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "JAVA_HOME", std::strerror(error));
		throw EXIT_FAILURE;
	}
}
