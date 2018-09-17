/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <map>
#include <set>
#include <cstddef>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <new>
#include <memory>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "service-manager-client.h"
#include "runtime-dir.h"
#include "home-dir.h"
#include "popt.h"

/* Global data **************************************************************
// **************************************************************************
*/

bool per_user_mode(false);

/* Utilities ****************************************************************
// **************************************************************************
*/

static 
const char * const 
target_bundle_prefixes[6] = {
	"/run/service-bundles/targets/", 
	"/usr/local/etc/service-bundles/targets/", 
	"/etc/service-bundles/targets/", 
	"/usr/local/share/service-bundles/targets/", 
	"/usr/share/service-bundles/targets/", 
	"/var/service-bundles/targets/"
}, * const
service_bundle_prefixes[11] = {
	"/run/service-bundles/services/", 
	"/usr/local/etc/service-bundles/services/", 
	"/etc/service-bundles/services/", 
	"/usr/local/share/service-bundles/services/", 
	"/var/local/service-bundles/services/", 
	"/var/local/sv/",
	"/usr/share/service-bundles/services/", 
	"/var/service-bundles/services/", 
	"/var/sv/",
	"/var/svc.d/",	// Wayne Marshall compatibility
	"/service/"	// Daniel J. Bernstein compatibility
};

int
open_bundle_directory (
	const ProcessEnvironment & envs,
	const char * prefix,
	const char * arg,
	std::string & path,
	std::string & name,
	std::string & suffix
) {
	if (const char * slash = std::strrchr(arg, '/')) {
		path = std::string(arg, slash + 1);
		name = std::string(slash + 1);
		suffix = std::string();
		return open_dir_at(AT_FDCWD, (path + prefix + name + "/").c_str());
	}

	const std::string p(prefix), a(arg);
	bool scan_for_target(false), scan_for_service(false);
	if (ends_in(a, ".target", name)) {
		scan_for_target = true;
	} else
	if (ends_in(a, ".service", name)) {
		scan_for_service = true;
	} else
	if (ends_in(a, ".socket", name)) {
		scan_for_service = true;
	} else
	if (ends_in(a, ".timer", name)) {
		scan_for_service = true;
	} else
	{
		name = a;
		scan_for_target = scan_for_service = true;
	}

	if (per_user_mode) {
		const std::string h(effective_user_home_dir(envs));
		const std::string r(effective_user_runtime_dir());
		if (scan_for_target) {
			suffix = ".target";
			const std::string
			user_target_bundle_prefixes[2] = {
				r + "/service-bundles/targets/",
				h + "/.config/service-bundles/targets/",
			};
			for ( const std::string * q(user_target_bundle_prefixes); q < user_target_bundle_prefixes + sizeof user_target_bundle_prefixes/sizeof *user_target_bundle_prefixes; ++q) {
				path = *q;
				const int bundle_dir_fd(open_dir_at(AT_FDCWD, (path + p + name + "/").c_str()));
				if (0 <= bundle_dir_fd) return bundle_dir_fd;
			}
		}
		if (scan_for_service) {
			suffix = ".service";
			const std::string
			user_service_bundle_prefixes[2] = {
				r + "/service-bundles/services/", 
				h + "/.config/service-bundles/services/", 
			};
			for ( const std::string * q(user_service_bundle_prefixes); q < user_service_bundle_prefixes + sizeof user_service_bundle_prefixes/sizeof *user_service_bundle_prefixes; ++q) {
				path = *q;
				const int bundle_dir_fd(open_dir_at(AT_FDCWD, (path + p + name + "/").c_str()));
				if (0 <= bundle_dir_fd) return bundle_dir_fd;
			}
		}
	} else {
		if (scan_for_target) {
			suffix = ".target";
			for ( const char * const * q(target_bundle_prefixes); q < target_bundle_prefixes + sizeof target_bundle_prefixes/sizeof *target_bundle_prefixes; ++q) {
				path = *q;
				const int bundle_dir_fd(open_dir_at(AT_FDCWD, (path + p + name + "/").c_str()));
				if (0 <= bundle_dir_fd) return bundle_dir_fd;
			}
		}
		if (scan_for_service) {
			suffix = ".service";
			for ( const char * const * q(service_bundle_prefixes); q < service_bundle_prefixes + sizeof service_bundle_prefixes/sizeof *service_bundle_prefixes; ++q) {
				path = *q;
				const int bundle_dir_fd(open_dir_at(AT_FDCWD, (path + p + name + "/").c_str()));
				if (0 <= bundle_dir_fd) return bundle_dir_fd;
			}
		}
	}

	path = std::string();
	name = a;
	suffix = std::string();
	return open_dir_at(AT_FDCWD, (path + p + name + "/").c_str());
}

/* The system-control command ***********************************************
// **************************************************************************
*/

void
system_control ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		// These compatibility options make command completion in the Z and Bourne Again shells slightly smoother.
		// They also prevent install/uninstall scripts in RPM packages from breaking.
		bool full(false), no_legend(false), no_pager(false), no_reload(false), quiet(false);
		popt::bool_definition full_option('\0', "full", "Compatibility option.  Ignored.", full);
		popt::bool_definition no_legend_option('\0', "no-legend", "Compatibility option.  Ignored.", no_legend);
		popt::bool_definition no_pager_option('\0', "no-pager", "Compatibility option.  Ignored.", no_pager);
		popt::bool_definition no_reload_option('\0', "no-reload", "Compatibility option.  Ignored.", no_reload);
		popt::bool_definition quiet_option('\0', "quite", "Compatibility option.  Ignored.", quiet);
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::definition * top_table[] = {
			&user_option,
			&full_option,
			&no_legend_option,
			&no_pager_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", 
				"{"
				"halt|reboot|poweroff|powercycle|"
				"emergency|rescue|normal|init|sysinit|"
				"start|stop|enable|disable|preset|reset|unload-when-stopped|"
				"try-restart|hangup|"
				"is-active|is-loaded|is-enabled|"
				"cat|show|status|show-json|"
				"set-service-env|print-service-env|"
				"convert-systemd-units|convert-fstab-services|"
				"nagios-check-service|load-kernel-module|unload-kernel-module|"
				"is-service-manager-client|"
				"version"
				"}"
				" [arg(s)...]"
		);

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

	// Effectively, all subcommands are implemented by chaining to builtins of the same name.
}
