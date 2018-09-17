/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <grp.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "listen.h"

/* Main function ************************************************************
// **************************************************************************
*/

// This must have static storage duration as we are using it in args.
static std::string banner_storage;

void
tcpserver ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool verbose(false);
	const char * backlog = 0;
	const char * connection_limit = 0;
	const char * localname = 0;
	const char * banner = 0;
	bool setuidgid(false);
#if defined(IP_OPTIONS)
	bool no_kill_IP_options(false);
#endif
	bool no_delay(false);
	try {
		bool kill_IP_options(false);
		bool print_errors(false), quiet(false), delay(false), no_remoteinfo(false), no_remotehost(false), no_paranoia(false);

#if defined(IP_OPTIONS)
		popt::bool_definition no_kill_IP_options_option('o', "no-kill-IP-options", "Allow a client to set source routes.", no_kill_IP_options);
		popt::bool_definition kill_IP_options_option('O', "kill-IP-options", "compatibility option; ignored.", kill_IP_options);
#endif
		popt::bool_definition verbose_option('v', "verbose", "Print status information.", verbose);
		popt::bool_definition print_errors_option('Q', "print-errors", "Compatibility option; ignored.", print_errors);
		popt::bool_definition quiet_option('q', "quiet", "Compatibility option; ignored.", quiet);
		popt::bool_definition delay_option('d', "delay", "Compatibility option; ignored.", delay);
		popt::bool_definition no_delay_option('D', "no-delay", "Disable the TCP delay algorithm.", no_delay);
		popt::bool_definition no_remoteinfo_option('R', "no-remote-info", "Compatibility option; ignored.", no_remoteinfo);
		popt::bool_definition no_remotehost_option('H', "no-remote-host", "Compatibility option; ignored.", no_remotehost);
		popt::bool_definition no_paranoia_option('P', "no-paranoia", "Compatibility option; ignored.", no_paranoia);
		popt::bool_definition setuidgid_option('U', "setuidgid-fromenv", "Set UID and GID from environment between listen and accept.", setuidgid);
		popt::string_definition localname_option('l', "localname", "hostname", "Override the local host name.", localname);
		popt::string_definition banner_option('B', "banner", "text", "Print an initial banner in each spawned child.", banner);
		popt::string_definition connection_limit_option('c', "connection-limit", "number", "Specify the limit on the number of simultaneous parallel connections.", connection_limit);
		popt::string_definition backlog_option('b', "backlog", "number", "Specify the listening backlog.", backlog);
		popt::definition * top_table[] = {
#if defined(IP_OPTIONS)
			&no_kill_IP_options_option,
			&kill_IP_options_option,
#endif
			&verbose_option,
			&print_errors_option,
			&quiet_option,
			&delay_option,
			&no_delay_option,
			&no_remotehost_option,
			&no_remoteinfo_option,
			&no_paranoia_option,
			&setuidgid_option,
			&localname_option,
			&banner_option,
			&connection_limit_option,
			&backlog_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{localhost} {localport} {prog}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing listen host name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * listenhost(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing listen port number.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * listenport(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing next program.");
		throw static_cast<int>(EXIT_USAGE);
	}

	if (banner) {
		banner_storage = banner;
		if (!banner_storage.empty() && '\n' == banner_storage.back()) {
			banner_storage.erase(banner_storage.end() - 1);
			if (!banner_storage.empty() && '\r' == banner_storage.back())
				banner_storage.erase(banner_storage.end() - 1);
		}
		args.insert(args.begin(), banner_storage.c_str());
		args.insert(args.begin(), "--");
		args.insert(args.begin(), "--NVT");
		args.insert(args.begin(), "line-banner");
	}
	args.insert(args.begin(), "--");
	if (localname) {
		args.insert(args.begin(), localname);
		args.insert(args.begin(), "--localname");
	}
	if (connection_limit) {
		args.insert(args.begin(), connection_limit);
		args.insert(args.begin(), "--connection-limit");
	}
	if (no_delay)
		args.insert(args.begin(), "--no-delay");
	if (verbose)
		args.insert(args.begin(), "--verbose");
	args.insert(args.begin(), "tcp-socket-accept");
	if (setuidgid)
		args.insert(args.begin(), "setuidgid-fromenv");
	args.insert(args.begin(), listenport);
	args.insert(args.begin(), listenhost);
	args.insert(args.begin(), "--");
#if defined(IP_OPTIONS)
	if (no_kill_IP_options)
		args.insert(args.begin(), "--no-kill-IP-options");
#endif
	if (backlog) {
		args.insert(args.begin(), backlog);
		args.insert(args.begin(), "--backlog");
	}
	args.insert(args.begin(), "tcp-socket-listen");
	next_prog = arg0_of(args);
}
