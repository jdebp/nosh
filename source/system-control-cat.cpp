/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstddef>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/wait.h>
#include "utils.h"
#include "service-manager-client.h"
#include "popt.h"

/* System control subcommands ***********************************************
// **************************************************************************
*/

void
cat ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::definition * main_table[] = {
			&user_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "{service(s)...}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing service name(s).");
		throw static_cast<int>(EXIT_USAGE);
	}

	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		std::string path, name, suffix;
		const int bundle_dir_fd(open_bundle_directory(envs, "", *i, path, name, suffix));
		if (0 > bundle_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, *i, std::strerror(error));
			throw EXIT_FAILURE;
		}
		const int service_dir_fd(open_service_dir(bundle_dir_fd));
		if (0 > service_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s%s/%s: %s\n", prog, path.c_str(), name.c_str(), "service", std::strerror(error));
			throw EXIT_FAILURE;
		}
		const pid_t child(fork());
		if (0 > child) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "fork", std::strerror(error));
			throw EXIT_FAILURE;
		}
		if (0 == child) {
			fchdir(service_dir_fd);
			close(service_dir_fd);
			close(bundle_dir_fd);
			args.clear();
			args.insert(args.end(), "grep");
			args.insert(args.end(), "^");
			args.insert(args.end(), "start");
			args.insert(args.end(), "stop");
			args.insert(args.end(), "run");
			if (0 <= access("service", F_OK))
				args.insert(args.end(), "service");
			args.insert(args.end(), "restart");
			args.insert(args.end(), 0);
			next_prog = arg0_of(args);
			return;
		} else {
			int status, code;
			wait_blocking_for_exit_of(child, status, code);
		}
		close(service_dir_fd);
		close(bundle_dir_fd);
	}

	throw EXIT_SUCCESS;
}
