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
#include "popt.h"

/* Utilities ****************************************************************
// **************************************************************************
*/

static inline
std::vector<std::string>
lookup_bundle_directories (
	const char * prog,
	const std::vector<const char *> & args,
	const ProcessEnvironment & envs
) {
	std::vector<std::string> r;
	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		std::string path, name, suffix;
		const int rc(open_bundle_directory(envs, "", *i, path, name, suffix));
		if (0 > rc) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, *i, std::strerror(error));
			throw EXIT_FAILURE;
		}
		r.insert(r.end(), path + name);
		close(rc);
	}
	return r;
}

static inline
std::vector<const char *>
convert (
	const std::vector<std::string> & args
) {
	std::vector<const char *> r;
	for (std::vector<std::string>::const_iterator i(args.begin()); args.end() != i; ++i)
		r.insert(r.end(), i->c_str());
//	r.insert(r.end(), 0);
	return r;
}

/* System control subcommands ***********************************************
// **************************************************************************
*/

static std::vector<std::string> args_storage;

static
void
common_subcommand ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	const ProcessEnvironment & envs,
	const char * command,
	const char * arg
) {
	const char * prog(basename_of(args[0]));
	const char * log_lines(0);
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::string_definition log_lines_option('\0', "log-lines", "number", "Control the number of log lines printed.", log_lines);
		popt::definition * main_table[] = {
			&user_option,
			&log_lines_option
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

	args_storage = lookup_bundle_directories(prog, args, envs);
	args = convert(args_storage);
	if (arg)
		args.insert(args.begin(), arg);
	if (log_lines) {
		args.insert(args.begin(), log_lines);
		args.insert(args.begin(), "--log-lines");
	}
	args.insert(args.begin(), command);
	next_prog = arg0_of(args);
}

void
find ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	common_subcommand(next_prog, args, envs, "ls", "-1d");
}

void
show_json ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	common_subcommand(next_prog, args, envs, "service-show", "--json");
}

void
show ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	common_subcommand(next_prog, args, envs, "service-show", NULL);
}

void
status ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	common_subcommand(next_prog, args, envs, "service-status", NULL);
}

void
try_restart ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	common_subcommand(next_prog, args, envs, "service-control", "--terminate");
}

void
unload_when_stopped ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	common_subcommand(next_prog, args, envs, "service-control", "--exit");
}

void
hangup ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	common_subcommand(next_prog, args, envs, "service-control", "--hangup-main");
}

void
is_active ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	common_subcommand(next_prog, args, envs, "service-is-up", NULL);
}

void
is_loaded ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	common_subcommand(next_prog, args, envs, "service-is-ok", NULL);
}

void
is_enabled ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	common_subcommand(next_prog, args, envs, "service-is-enabled", NULL);
}
