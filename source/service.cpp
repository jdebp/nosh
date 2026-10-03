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
	ProcessEnvironment & envs
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
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) die_missing_service_name(prog, envs);
	const char * service(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		die_missing_argument(prog, envs, "command name");
	}
	const char * command(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	args.clear();
	args.push_back("system-control");
	args.push_back(command);
	if (verbose)
		args.push_back("--verbose");
	args.push_back(service);
	next_prog = arg0_of(args);
}

void
invoke_rcd (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool user(false);
	try {
		popt::bool_definition user_option('\0', "user", "Operate upon per-user services.", user);
		bool quiet(false), skip_systemd_native(false);
		popt::bool_definition quiet_option('q', "quiet", "Compatibility option; ignored.", quiet);
		popt::bool_definition skip_systemd_native_option('\0', "skip-systemd-native", "Compatibility option; ignored.", skip_systemd_native);
		popt::definition * top_table[] = {
			&user_option,
			&quiet_option,
			&skip_systemd_native_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{service-name} {start|stop|force-stop|force-reload|restart}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) die_missing_service_name(prog, envs);
	const char * service(args.front());
	args.erase(args.begin());
	if (args.empty()) die_missing_argument(prog, envs, "command name");
	const char * command(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	if (0 == std::strcmp("start", command))
		command = "reset";
	else
	if (0 == std::strcmp("stop", command))
		;	// Don't touch it.
	else
	if (0 == std::strcmp("status", command))
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
		args.push_back("foreground");
		args.push_back("system-control");
		if (user)
			args.push_back("--user");
		args.push_back("stop");
		args.push_back(service);
		args.push_back(";");
		args.push_back("system-control");
		if (user)
			args.push_back("--user");
		args.push_back("reset");
		args.push_back(service);
		next_prog = arg0_of(args);
		return;
	}
	else
	if (0 == std::strcmp("try-restart", command))
		command = "condrestart";
	else
		die_unsupported_command(prog, envs, service, command);

	args.clear();
	args.push_back("system-control");
	if (user)
		args.push_back("--user");
	args.push_back(command);
	args.push_back(service);
	next_prog = arg0_of(args);
}

void
update_rcd (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool force(false);
	bool user(false);
	try {
		popt::bool_definition user_option('\0', "user", "Operate upon per-user services.", user);
		popt::bool_definition force_option('f', "force", "Compatibility option; ignored.", force);
		popt::definition * top_table[] = {
			&user_option,
			&force_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{service-name} {enable|disable|remove|defaults}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) die_missing_service_name(prog, envs);
	const char * service(args.front());
	args.erase(args.begin());
	if (args.empty()) die_missing_argument(prog, envs, "command name");
	const char * command(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	if (0 == std::strcmp("enable", command)
	||  0 == std::strcmp("start", command)
	||  0 == std::strcmp("defaults", command)
	)
		command = "preset";
	else
	if (0 == std::strcmp("disable", command))
		;	// Don't touch it.
	else
	if (0 == std::strcmp("remove", command)
	||  0 == std::strcmp("stop", command)
	)
		command = "disable";
	else
		die_unsupported_command(prog, envs, command);

	args.clear();
	args.push_back("system-control");
	if (user)
		args.push_back("--user");
	args.push_back(command);
	args.push_back(service);
	next_prog = arg0_of(args);
}

void
deb_systemd_invoke (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool user(false);
	try {
		popt::bool_definition user_option('\0', "user", "Operate upon per-user services.", user);
		popt::definition * top_table[] = {
			&user_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{service-name} {start|stop|restart}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) die_missing_service_name(prog, envs);
	const char * service(args.front());
	args.erase(args.begin());
	if (args.empty()) die_missing_argument(prog, envs, "command name");
	const char * command(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	if (0 == std::strcmp("start", command))
		command = "reset";
	else
	if (0 == std::strcmp("stop", command))
		;	// Don't touch it.
	else
	if (0 == std::strcmp("restart", command)) {
		args.clear();
		args.push_back("foreground");
		args.push_back("system-control");
		if (user)
			args.push_back("--user");
		args.push_back("stop");
		args.push_back(service);
		args.push_back(";");
		args.push_back("system-control");
		if (user)
			args.push_back("--user");
		args.push_back("reset");
		args.push_back(service);
		next_prog = arg0_of(args);
		return;
	}
	else
	if (0 == std::strcmp("try-restart", command))
		command = "condrestart";
	else
		die_unsupported_command(prog, envs, service, command);

	args.clear();
	args.push_back("system-control");
	if (user)
		args.push_back("--user");
	args.push_back(command);
	args.push_back(service);
	next_prog = arg0_of(args);
}

void
chkconfig (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		bool force(false);
		popt::bool_definition force_option('f', "force", "Compatibility option; ignored.", force);
		popt::definition * top_table[] = {
			&force_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{service-name} [reset|on|off]");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) die_missing_service_name(prog, envs);
	const char * service(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		args.clear();
		args.push_back("system-control");
		args.push_back("is-enabled");
	} else {
		const char * command(args.front());
		args.erase(args.begin());
		if (!args.empty()) die_unexpected_argument(prog, args, envs);

		if (0 == std::strcmp("reset", command))
			command = "preset";
		else
		if (0 == std::strcmp("on", command))
			command = "enable";
		else
		if (0 == std::strcmp("off", command))
			command = "disable";
		else
			die_unsupported_command(prog, envs, service, command);

		args.clear();
		args.push_back("system-control");
		args.push_back(command);
	}
	args.push_back(service);
	next_prog = arg0_of(args);
}

void
rc_update (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{add|del} {service-name}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) {
		die_missing_argument(prog, envs, "command name");
	}
	const char * command(args.front());
	args.erase(args.begin());
	if (args.empty()) die_missing_service_name(prog, envs);
	const char * service(args.front());
	args.erase(args.begin());
#if 0	// Let the subcommands handle this.
	if (!args.empty()) die_unexpected_argument(prog, args, envs);
#endif

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
		die_unsupported_command(prog, envs, command);

	args.clear();
	args.push_back("system-control");
	args.push_back(command);
	args.push_back(service);
	next_prog = arg0_of(args);
}

void
initctl (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, nullptr, "Main options", "version|start|status|stop|show-config {service-name}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) {
		die_missing_argument(prog, envs, "command name");
	}
	const char * command(args.front());
	args.erase(args.begin());
	const char * service(nullptr);

        if (0 == std::strcmp("version", command)) {
                /* This is unchanged. */
	} else
	{
		if (args.empty()) {
			die_invalid_argument(prog, envs, command, "Missing service name.");
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
			die_unsupported_command(prog, envs, command);
	}
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

        args.clear();
        args.push_back("system-control");
        args.push_back(command);
	if (service)
		args.push_back(service);
	next_prog = arg0_of(args);
}

// Some commands are thin wrappers around initctl subcommands of the same name.
void
wrap_initctl_subcommand (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	args.insert(args.begin(), "initctl");
	next_prog = arg0_of(args);
}

void
svcadm (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, nullptr, "Main options", "version|enable|disable {service-name}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) {
		die_missing_argument(prog, envs, "command name");
	}
	const char * command(args.front());
	args.erase(args.begin());
	const char * service(nullptr);

        if (0 == std::strcmp("version", command)) {
                /* This is unchanged. */
	} else
	{
		if (args.empty()) die_missing_service_name(prog, envs);
		service = args.front();
		args.erase(args.begin());

		if (0 == std::strcmp("enable", command)) {
			command = "start";
		} else
		if (0 == std::strcmp("disable", command)) {
			command = "stop";
		} else
			die_unsupported_command(prog, envs, command);
	}
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

        args.clear();
        args.push_back("system-control");
        args.push_back(command);
	if (service)
		args.push_back(service);
	next_prog = arg0_of(args);
}
