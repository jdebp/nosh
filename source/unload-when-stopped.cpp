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
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "service-manager-client.h"
#include "FileDescriptorOwner.h"
#include "popt.h"

/* System control subcommands ***********************************************
// **************************************************************************
*/

void
unload_when_stopped [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
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

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing directory name(s).");
		throw static_cast<int>(EXIT_USAGE);
	}

#if 1	/// \todo TODO: Eventually we can switch off this mechanism.
	FileDescriptorOwner socket_fd(connect_service_manager_socket(!per_user_mode, prog));
	if (0 > socket_fd.get()) throw EXIT_FAILURE;
#endif

	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		std::string path, name, suffix;
		FileDescriptorOwner bundle_dir_fd(open_bundle_directory("", *i, path, name, suffix));
		if (0 > bundle_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, *i, std::strerror(error));
			throw EXIT_FAILURE;
		}
		FileDescriptorOwner supervise_dir_fd(open_supervise_dir(bundle_dir_fd.get()));
		if (0 > supervise_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, *i, "supervise", std::strerror(error));
			throw EXIT_FAILURE;
		}
		unload_when_stopped(supervise_dir_fd.get());
#if 1	/// \todo TODO: Eventually we can switch off this mechanism.
		unload (prog, socket_fd.get(), supervise_dir_fd.get());
#endif
	}

	throw EXIT_SUCCESS;
}
