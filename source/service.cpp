/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <unistd.h>
#include <cstddef>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <cerrno>
#include "utils.h"
#include "popt.h"

/* The service shim *********************************************************
// **************************************************************************
*/

void
service ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool verbose(false);
	try {
		popt::bool_definition verbose_option('v', "verbose", "Request verbose operation.", verbose);
		popt::definition * top_table[] = {
			&verbose_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{service-name} {command}");

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
	const char * service(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing command name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * command(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unrecognized extra arguments.");
		throw EXIT_FAILURE;
	}

	args.clear();
	args.insert(args.end(), "system-control");
	args.insert(args.end(), command);
	if (verbose)
		args.insert(args.end(), "--verbose");
	args.insert(args.end(), service);
	next_prog = arg0_of(args);
}

void
invoke_rcd ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool quiet(false);
	try {
		popt::bool_definition quiet_option('q', "quiet", "Compatibility option; ignored.", quiet);
		popt::definition * top_table[] = {
			&quiet_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{service-name} {start|stop|force-stop|force-reload|restart}");

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
	const char * service(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing command name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * command(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unrecognized extra arguments.");
		throw EXIT_FAILURE;
	}

	if (0 == std::strcmp("start", command))
		command = "reset";
	else
	if (0 == std::strcmp("stop", command))
		;	// Don't touch it.
	else 
	if (0 == std::strcmp("force-stop", command))
		command = "stop";
	else 
	if (0 == std::strcmp("force-reload", command))
		command = "condrestart";
	else 
	if (0 == std::strcmp("restart", command)) {
		args.clear();
		args.insert(args.end(), "foreground");
		args.insert(args.end(), "system-control");
		args.insert(args.end(), "stop");
		args.insert(args.end(), service);
		args.insert(args.end(), ";");
		args.insert(args.end(), "system-control");
		args.insert(args.end(), "reset");
		args.insert(args.end(), service);
		next_prog = arg0_of(args);
		return;
	}
	else
	{
		std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, service, command, "Unsupported subcommand.");
		throw EXIT_FAILURE;
	}

	args.clear();
	args.insert(args.end(), "system-control");
	args.insert(args.end(), command);
	args.insert(args.end(), service);
	next_prog = arg0_of(args);
}

void
update_rcd ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool force(false);
	try {
		popt::bool_definition force_option('f', "force", "Compatibility option; ignored.", force);
		popt::definition * top_table[] = {
			&force_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{service-name} {enable|disable|remove|defaults}");

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
	const char * service(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing command name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * command(args.front());
	args.erase(args.begin());

	if (0 == std::strcmp("enable", command))
		command = "preset";
	else
	if (0 == std::strcmp("disable", command))
		;	// Don't touch it.
	else 
	if (0 == std::strcmp("remove", command))
		command = "disable";
	else
	if (0 == std::strcmp("defaults", command)) {
defaults:
		if (!args.empty()) {
			std::fprintf(stderr, "%s: WARNING: %s: %s %s\n", prog, service, command, "has not supported arguments since 2013.");
			args.clear();
		}
		command = "preset";
	} else
	if (0 == std::strcmp("start", command)
	||  0 == std::strcmp("stop", command)
	) {
		std::fprintf(stderr, "%s: WARNING: %s: %s %s\n", prog, service, command, "has been superseded by defaults since 2013.");
		goto defaults;
	}
	else
	{
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, command, "Unsupported subcommand.");
		throw EXIT_FAILURE;
	}

	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, command, "Unrecognized extra arguments.");
		throw EXIT_FAILURE;
	}

	args.clear();
	args.insert(args.end(), "system-control");
	args.insert(args.end(), command);
	args.insert(args.end(), service);
	next_prog = arg0_of(args);
}

void
chkconfig ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "{service-name} {command}");

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
	const char * service(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		args.clear();
		args.insert(args.end(), "system-control");
		args.insert(args.end(), "is-enabled");
	} else {
		const char * command(args.front());
		args.erase(args.begin());
		if (!args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unrecognized extra arguments.");
			throw EXIT_FAILURE;
		}

		if (0 == std::strcmp("reset", command))
			command = "preset";
		else
		if (0 == std::strcmp("on", command))
			command = "enable";
		else
		if (0 == std::strcmp("off", command))
			command = "disable";
		else 
		{
			std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, service, command, "Unsupported subcommand.");
			throw EXIT_FAILURE;
		}

		args.clear();
		args.insert(args.end(), "system-control");
		args.insert(args.end(), command);
	}
	args.insert(args.end(), service);
	next_prog = arg0_of(args);
}

void
rc_update ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{add|del} {service-name}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing command name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * command(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing service name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * service(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, command, "Unrecognized extra arguments.");
		throw EXIT_FAILURE;
	}

	if (0 == std::strcmp("add", command)) {
		if (!args.empty())
			std::fprintf(stderr, "%s: WARNING: %s: %s %s\n", prog, service, command, "does not support run-levels.");
		command = "preset";
	} else
	if (0 == std::strcmp("del", command)) {
		if (!args.empty())
			std::fprintf(stderr, "%s: WARNING: %s: %s %s\n", prog, service, command, "does not support run-levels.");
		command = "disable";
	} else
	{
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, command, "Unsupported subcommand.");
		throw EXIT_FAILURE;
	}

	args.clear();
	args.insert(args.end(), "system-control");
	args.insert(args.end(), command);
	args.insert(args.end(), service);
	next_prog = arg0_of(args);
}

void
initctl ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "{command} {service-name}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing command name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * command(args.front());
	args.erase(args.begin());
	const char * service(0);

        if (0 == std::strcmp("version", command)) {
                /* This is unchanged. */;
	} else
	{
		if (args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, command, "Missing service name.");
			throw static_cast<int>(EXIT_USAGE);
		}
		service = args.front();
		args.erase(args.begin());

		if (false
		||  0 == std::strcmp("start", command)
		||  0 == std::strcmp("status", command)
		||  0 == std::strcmp("stop", command)
		)
			/* This is unchanged. */;
		else
		if (0 == std::strcmp("show-config", command))
			command = "show-json";
		else
		{
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, command, "Unsupported subcommand.");
			throw EXIT_FAILURE;
		}
	}
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unrecognized extra arguments.");
		throw EXIT_FAILURE;
	}

        args.clear();
        args.insert(args.end(), "system-control");
        args.insert(args.end(), command);
	if (service)
		args.insert(args.end(), service);
	next_prog = arg0_of(args);
}

void
svcadm ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "{command} {service-name}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing command name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * command(args.front());
	args.erase(args.begin());
	const char * service(0);

        if (0 == std::strcmp("version", command)) {
                /* This is unchanged. */;
	} else
	{
		if (args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, command, "Missing service name.");
			throw static_cast<int>(EXIT_USAGE);
		}
		service = args.front();
		args.erase(args.begin());

		if (0 == std::strcmp("enable", command)) {
			command = "start";
		} else
		if (0 == std::strcmp("disable", command)) {
			command = "stop";
		} else
		{
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, command, "Unsupported subcommand.");
			throw EXIT_FAILURE;
		}
	}
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unrecognized extra arguments.");
		throw EXIT_FAILURE;
	}

        args.clear();
        args.insert(args.end(), "system-control");
        args.insert(args.end(), command);
	if (service)
		args.insert(args.end(), service);
	next_prog = arg0_of(args);
}
