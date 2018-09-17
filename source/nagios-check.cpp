/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <vector>
#include <map>
#include <set>
#include <iostream>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <new>
#include <memory>
#include <inttypes.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "FileDescriptorOwner.h"
#include "unpack.h"
#include "service-manager-client.h"
#include "service-manager.h"
#include "popt.h"

/* Nagios check subcommands *************************************************
// **************************************************************************
*/

static unsigned long min_seconds(2);

enum {
	EXIT_NAGIOS_OK = 0,
	EXIT_NAGIOS_WARNING = 1,
	EXIT_NAGIOS_CRITICAL = 2,
	EXIT_NAGIOS_UNKNOWN = 3
};

static
void
set (
	int & status,
	const int code
) {
	switch (status) {
		default:
		case EXIT_NAGIOS_OK:
			if (EXIT_NAGIOS_UNKNOWN == code) status = code;
			// deliberately fall through
			[[clang::fallthrough]];
		case EXIT_NAGIOS_UNKNOWN:
			if (EXIT_NAGIOS_WARNING == code) status = code;
			// deliberately fall through
			[[clang::fallthrough]];
		case EXIT_NAGIOS_WARNING:
			if (EXIT_NAGIOS_CRITICAL == code) status = code;
			// deliberately fall through
			[[clang::fallthrough]];
		case EXIT_NAGIOS_CRITICAL:
			break;
	}
}

static inline
void
check ( 
	const ProcessEnvironment & envs,
	const int bundle_dir_fd,
	const std::string & name,
	std::vector<std::string> & not_loaded,
	std::vector<std::string> & loading,
	std::vector<std::string> & clock_skewed,
	std::vector<std::string> & stopped,
	std::vector<std::string> & started,
	std::vector<std::string> & disabled,
	std::vector<std::string> & below_min_seconds,
	std::vector<std::string> & failed,
	int & rc
) {
	timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	const uint64_t z(time_to_tai64(envs, TimeTAndLeap(now.tv_sec, false)));

	const FileDescriptorOwner supervise_dir_fd(open_supervise_dir(bundle_dir_fd));
	if (0 > supervise_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stdout, "ERROR: %s/%s: %s\n", name.c_str(), "supervise", std::strerror(error));
		set(rc, EXIT_NAGIOS_CRITICAL);
		return;
	}

	const FileDescriptorOwner service_dir_fd(open_service_dir(bundle_dir_fd));
	if (0 > service_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stdout, "ERROR: %s/%s: %s\n", name.c_str(), "service", std::strerror(error));
		set(rc, EXIT_NAGIOS_CRITICAL);
		return;
	}
	const bool initially_up(is_initially_up(service_dir_fd.get()));
	const bool ready_after_run(is_ready_after_run(service_dir_fd.get()));

	if (!is_ok(supervise_dir_fd.get())) {
		const int error(errno);
		if (ENXIO == error)
			not_loaded.push_back(name);
		else {
			std::fprintf(stdout, "ERROR: %s/%s: %s\n", name.c_str(), "supervise/ok", std::strerror(error));
			set(rc, EXIT_NAGIOS_CRITICAL);
		}
		return;
	}

	const FileDescriptorOwner status_fd(open_read_at(supervise_dir_fd.get(), "status"));
	if (0 > status_fd.get()) {
		const int error(errno);
		std::fprintf(stdout, "ERROR: %s/%s: %s\n", name.c_str(), "status", std::strerror(error));
		set(rc, EXIT_NAGIOS_CRITICAL);
		return;
	}
	char status[STATUS_BLOCK_SIZE];
	const ssize_t b(read(status_fd.get(), status, sizeof status));

	if (b < DAEMONTOOLS_STATUS_BLOCK_SIZE) {
		loading.push_back(name);
		return;
	}

	const uint64_t s(unpack_bigendian(status, 8));

	if (z < s) {
		clock_skewed.push_back(name);
		return;
	}

	const uint64_t secs(z - s);

	if (b < ENCORE_STATUS_BLOCK_SIZE) {
		const uint32_t p(unpack_littleendian(status + THIS_PID_OFFSET, 4));
		if (!p) {
			if (initially_up) {
				stopped.push_back(name);
			} else {
				disabled.push_back(name);
			}
			return;
		} else
		if (secs < min_seconds) {
			below_min_seconds.push_back(name);
			return;
		}
	} else {
		switch (status[ENCORE_STATUS_OFFSET]) {
			case encore_status_stopped:
				if (ready_after_run) {
					if (has_exited_run(b, status))
						break;
				}
				if (initially_up) {
					stopped.push_back(name);
				} else {
					disabled.push_back(name);
				}
				return;
			case encore_status_started:
				started.push_back(name);
				return;
			case encore_status_starting:
			case encore_status_stopping:
				if (secs < min_seconds) {
					below_min_seconds.push_back(name);
					return;
				}
				break;
			case encore_status_running:
				if (ready_after_run) {
					if (has_main_pid(status)) {
						started.push_back(name);
						return;
					}
				} else {
					if (secs < min_seconds) {
						below_min_seconds.push_back(name);
						return;
					}
				}
				break;
			default:
			case encore_status_failed:
				failed.push_back(name);
				return;
		}
	}
}

static
void
print ( 
	const char * label,
	const std::vector<std::string> & names,
	int & rc,
	const int code
) {
	if (names.empty()) return;
	std::fprintf(stdout, "%s:", label);
	for (std::vector<std::string>::const_iterator i(names.begin()); names.end() != i; ++i) {
		const std::string & s(*i);
		std::fprintf(stdout, " %s", s.c_str());
	}
	std::fputc('\n', stdout);
	set(rc, code);
}

void
nagios_check [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool critical_if_below_min(false);
	try {
		popt::unsigned_number_definition min_seconds_option('s', "min-seconds", "seconds", "Specify the minimum number of seconds for a service to be running.", min_seconds, 0);
		popt::bool_definition critical_if_below_min_option('\0', "critical-if-below-min", "Being below the minimum number of seconds is a critical failure.", critical_if_below_min);
		popt::definition * main_table[] = {
			&min_seconds_option,
			&critical_if_below_min_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "{service(s)...}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw static_cast<int>(EXIT_NAGIOS_OK);
	} catch (const popt::error & e) {
		std::fprintf(stdout, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_NAGIOS_UNKNOWN);
	}

	int rc(EXIT_NAGIOS_OK);
	std::vector<std::string> not_loaded, loading, clock_skewed, stopped, started, disabled, below_min_seconds, failed;
	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		std::string path, name, suffix;
		const int bundle_dir_fd(open_bundle_directory(envs, "", *i, path, name, suffix));
		const std::string p(path + name);
		if (0 > bundle_dir_fd) {
			const int error(errno);
			std::fprintf(stdout, "ERROR: %s: %s\n", p.c_str(), std::strerror(error));
			set(rc, EXIT_NAGIOS_CRITICAL);
			continue;
		}
		check(envs, bundle_dir_fd, p, not_loaded, loading, clock_skewed, stopped, started, disabled, below_min_seconds, failed, rc);
		close(bundle_dir_fd);
	}
	print("not loaded", not_loaded, rc, EXIT_NAGIOS_CRITICAL);
	print("clock skewed", clock_skewed, rc, EXIT_NAGIOS_CRITICAL);
	print("stopped", stopped, rc, EXIT_NAGIOS_CRITICAL);
	print("started", started, rc, EXIT_NAGIOS_WARNING);
	print("failed/restarting", failed, rc, EXIT_NAGIOS_CRITICAL);
	print("still loading", loading, rc, EXIT_NAGIOS_WARNING);
	print("below minimum runtime", below_min_seconds, rc, critical_if_below_min ? EXIT_NAGIOS_CRITICAL : EXIT_NAGIOS_WARNING);
	print("disabled", disabled, rc, EXIT_NAGIOS_OK);
	if (EXIT_NAGIOS_OK == rc && disabled.empty())
		std::fprintf(stdout, "OK\n");

	throw rc;
}
