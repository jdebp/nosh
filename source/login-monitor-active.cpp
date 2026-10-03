/* coPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <vector>
#include <map>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <cctype>
#include <unistd.h>
#include "config/hasutmpx.h"
#include "config/hasutmp.h"
#if defined(HAS_UTMPX)
#include <utmpx.h>
#elif defined(HAS_UTMP)
#include <utmp.h>
#include <paths.h>
#else
#error "Don't know how to count logged in users on your platform."
#endif
#if defined(__LINUX__) || defined(__linux__)
#include "kqueue_linux.h"
#else
#include <sys/event.h>
#endif
#include "kqueue_common.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "popt.h"
#include "fdutils.h"
#include "utils.h"
#include "SignalManagement.h"

#if defined(HAS_UTMPX)
#if !defined(_PATH_UTMPX)
#if defined(__FreeBSD__) || defined(__DragonFly__)
#define _PATH_UTMPX "/var/run/utx.active"
#endif
#endif
#endif

/* Utilities ****************************************************************
// **************************************************************************
*/

namespace {

typedef std::map<std::string, bool> changemap;
typedef std::set<std::string> userset, serviceset;

std::string
rtrim (
	const char * s,
	std::size_t l
) {
	while (l > 0) {
		--l;
		if (s[l] && !std::isspace(s[l]))
			return std::string(s, l + 1);
	}
	return std::string();
}

changemap
update(
	userset & active_users
) {
	changemap changes;

	// Default to making all currently active users inactive, and empty the active users set.
	for (userset::iterator p(active_users.begin()), e(active_users.end()); e != p; p = active_users.erase(p))
		changes.insert(changemap::value_type(*p, false));

	// Repopulate the active users set.
#if defined(HAS_UTMPX)
	setutxent();
	while (struct utmpx * u = getutxent()) {
		if (USER_PROCESS != u->ut_type) continue;
		const std::string name(rtrim(u->ut_user, sizeof u->ut_user));
		active_users.insert(name);
	}
	endutxent();
#elif defined(HAS_UTMP)
	const FileStar file(std::fopen(_PATH_UTMP, "r"));
	if (file) {
		struct utmp u;
		for (;;) {
			const size_t n(std::fread(&u, sizeof u, 1, file));
			if (n < 1) break;
			if (!u.ut_name[0] || !u.ut_line[0]) continue;
			const std::string name(rtrim(u.ut_name, sizeof u.ut_name));
			active_users.insert(name);
		}
	}
#else
#error "Don't know how to enumerate logged in users on your platform."
#endif

	for (userset::const_iterator p(active_users.begin()), e(active_users.end()); e != p; ++p) {
		const changemap::iterator i(changes.find(*p));
		if (changes.end() != i)
			// Remove the inactivation of a still-active user.
			changes.erase(i);
		else
			// Activate a newly active user.
			changes.insert(changemap::value_type(*p, true));
	}

	return changes;
}

inline
void
apply (
	const changemap & changes,
	serviceset & resets,
	serviceset & stops,
	const char * target_prefix,
	const char * manager_prefix,
	const char * runtime_prefix
) {
	for (changemap::const_iterator p(changes.begin()), e(changes.end()); e != p; ++p) {
		const std::string user_target_name(target_prefix + p->first + ".target");
		const std::string user_manager_service_name(manager_prefix + p->first + ".service");
		const std::string user_runtime_service_name(runtime_prefix + p->first + ".service");
		if (p->second) {
			resets.insert(user_target_name);
			resets.insert(user_manager_service_name);
			resets.insert(user_runtime_service_name);
			stops.erase(user_target_name);
			stops.erase(user_manager_service_name);
			stops.erase(user_runtime_service_name);
		} else
		{
			resets.erase(user_target_name);
			resets.erase(user_manager_service_name);
			resets.erase(user_runtime_service_name);
			stops.insert(user_target_name);
			stops.insert(user_manager_service_name);
			stops.insert(user_runtime_service_name);
		}
	}
}

}

/* Main function ************************************************************
// **************************************************************************
*/

void
login_monitor_active (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
#if defined(HAS_UTMPX)
	const char * active_file_name(_PATH_UTMPX);
#elif defined(HAS_UTMP)
	const char * active_file_name(_PATH_UTMP);
#endif
	const char * target_prefix("user@");
	const char * manager_prefix("user-services@");
	const char * runtime_prefix("user-runtime@");
	bool verbose(false);
	try {
		popt::bool_definition verbose_option('v', "verbose", "Verbose mode.", verbose);
		popt::string_definition active_file_name_option('\0', "active-file", "filename", "Use this instead as the active login table filename.", active_file_name);
		popt::string_definition target_prefix_option('\0', "target-prefix", "string", "Prefix each target name with this (template) name.", target_prefix);
		popt::string_definition manager_prefix_option('\0', "manager-service-prefix", "string", "Prefix each manager service name with this (template) name.", manager_prefix);
		popt::string_definition runtime_prefix_option('\0', "runtime-service-prefix", "string", "Prefix each runtime service name with this (template) name.", runtime_prefix);
		popt::definition * top_table[] = {
			&target_prefix_option,
			&manager_prefix_option,
			&runtime_prefix_option,
			&verbose_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGTSTP, SIGALRM, SIGPIPE, SIGQUIT, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGTSTP, SIGALRM, SIGPIPE, SIGQUIT, 0);

#if defined(UTXDB_ACTIVE)
	if (0 > setutxdb(UTXDB_ACTIVE, active_file_name)) die_errno(prog, envs, active_file_name);
#endif

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) die_errno(prog, envs, "kqueue");
	const FileDescriptorOwner active_file_fd(open_read_at(AT_FDCWD, active_file_name));
	if (0 > active_file_fd.get()) die_errno(prog, envs, active_file_name);

	std::vector<struct kevent> ip;
	append_event(ip, active_file_fd.get(), EVFILT_VNODE, EV_ADD|EV_ENABLE|EV_CLEAR, NOTE_WRITE, 0, nullptr);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, nullptr);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, nullptr);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, nullptr);
	append_event(ip, SIGTSTP, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, nullptr);
	append_event(ip, SIGALRM, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, nullptr);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, nullptr);
	append_event(ip, SIGQUIT, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, nullptr);

	// These must have static storage duration as we are using them in args.
	static serviceset reset_names, stop_names;

	userset active_users;

	update(active_users);

	const struct timespec immediate_timeout = { 0, 0 };
	bool in_shutdown(false);
	for (;;) {
		if (in_shutdown) break;
		struct kevent e;
		const bool has_idle_work(!reset_names.empty() || !stop_names.empty());
		const int rc(kevent(queue.get(), ip.data(), ip.size(), &e, 1, has_idle_work ? &immediate_timeout : nullptr));
		ip.clear();
		if (0 > rc) {
			if (EINTR == errno) continue;
			die_errno(prog, envs, "kevent");
		} else
		if (1 > rc) {
			if (!reset_names.empty()) {
				const pid_t system_control_pid(fork());
				if (-1 == system_control_pid) {
					die_errno(prog, envs, "fork");
				} else if (0 == system_control_pid) {
					args.clear();
					args.push_back("system-control");
					args.push_back("reset");
					if (verbose)
						args.push_back("--verbose");
					for (serviceset::const_iterator p(reset_names.begin()), end(reset_names.end()); p != end; ++p)
						args.push_back(p->c_str());
					next_prog = arg0_of(args);
					return;
				} else
				{
					int status, code;
					if (0 >= wait_blocking_for_exit_of(system_control_pid, status, code)) {
						die_errno(prog, envs, "wait");
					}
				}
				reset_names.clear();
			}
			if (!stop_names.empty()) {
				const pid_t system_control_pid(fork());
				if (-1 == system_control_pid) {
					die_errno(prog, envs, "fork");
				} else if (0 == system_control_pid) {
					args.clear();
					args.push_back("system-control");
					args.push_back("stop");
					if (verbose)
						args.push_back("--verbose");
					for (serviceset::const_iterator p(stop_names.begin()), end(stop_names.end()); p != end; ++p)
						args.push_back(p->c_str());
					next_prog = arg0_of(args);
					return;
				} else
				{
					int status, code;
					if (0 >= wait_blocking_for_exit_of(system_control_pid, status, code)) {
						die_errno(prog, envs, "wait");
					}
				}
				stop_names.clear();
			}
		}

		switch (e.filter) {
			case EVFILT_VNODE:
			{
				const int fd(e.ident);
				if (active_file_fd.get() == fd) {
					const changemap changes(update(active_users));
					apply(changes, reset_names, stop_names, target_prefix, manager_prefix, runtime_prefix);
				}
				break;
			}
			case EVFILT_SIGNAL:
				switch (e.ident) {
					case SIGHUP:
					case SIGTERM:
					case SIGINT:
					case SIGPIPE:
					case SIGQUIT:
						in_shutdown = true;
						break;
					case SIGTSTP:
						std::fprintf(stderr, "%s: INFO: %s\n", prog, "Paused.");
						raise(SIGSTOP);
						std::fprintf(stderr, "%s: INFO: %s\n", prog, "Continued.");
						break;
					case SIGALRM:
					default:
#if defined(DEBUG)
						std::fprintf(stderr, "%s: DEBUG: signal event ident %lu fflags %x\n", prog, e.ident, e.fflags);
#endif
						break;
				}
				break;
			default:
#if defined(DEBUG)
				std::fprintf(stderr, "%s: DEBUG: event filter %hd ident %lu fflags %x\n", prog, e.filter, e.ident, e.fflags);
#endif
				break;
		}
	}
	throw EXIT_SUCCESS;
}
