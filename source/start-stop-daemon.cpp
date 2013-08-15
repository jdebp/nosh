/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cctype>
#include <unistd.h>
#include "popt.h"
#include "utils.h"

static inline 
std::string 
c2d ( unsigned c ) 
{
	std::string r;
	while (c) {
		const unsigned d(c % 10U);
		r = char(d + '0') + r;
		c /= 10U;
	}
	if (r.empty()) r += "0";
	return r;
}

static inline
std::string
systemd_encode (
	const char * s
) {
	if ('/' == s[0] && '\0' == s[1]) return "-";
	if ('/' == *s) ++s;
	std::string r;
	while (char c = *s++) {
		if ('/' == c)
			r += '-';
		else if ('-' == c || '\\' == c || !std::isprint(c))
			r += "\\x" + c2d(c);
		else
			r += c;
	}
	return r;
}

/* Main function ************************************************************
// **************************************************************************
*/

int
main ( 
	int argc, 
	const char * argv[]
) {
	const char * prog(basename_of(argv[0]));
	std::vector<const char *> args;

	bool start(false), stop(false), quiet(false), oknodo(false);
	const char * service = 0;
	const char * retries = 0;
	const char * signal_name = 0;
	const char * pidfile = 0;

	try {
		popt::bool_definition start_option('S', "start", "Start the service.", start);
		popt::bool_definition stop_option('K', "stop", "Stop the service.", stop);
		popt::bool_definition quiet_option('q', "quiet", "Operate quietly.", quiet);
		popt::string_definition retry_option('R', "retry", "schedule", "Wait for the service to stop.", retries);
		popt::string_definition signal_option('s', "signal", "signal-name", "Reload the service instead of stopping it.", signal_name);
		popt::bool_definition oknodo_option('o', "oknodo", "Compatibility option that does nothing.", oknodo);
		popt::string_definition name_option('n', "name", "service-name", "Specify the service name.", service);
		popt::string_definition exec_option('x', "exec", "service-name", "Specify the service name.", service);
		popt::string_definition startas_option('a', "startas", "service-name", "Specify the service name.", service);
		popt::string_definition pidfile_option('p', "pidfile", "signal-name", "Compatibility option that does nothing.", pidfile);
		popt::definition * top_table[] = {
			&start_option,
			&stop_option,
			&quiet_option,
			&retry_option,
			&signal_option,
			&oknodo_option,
			&name_option,
			&exec_option,
			&startas_option,
			&pidfile_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(argv + 1, argv + argc, prog, main_option, new_args);
		p.process(false);
		args = new_args;
		if (p.stopped()) return EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		return EXIT_FAILURE;
	}

	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		return EXIT_FAILURE;
	}

	if (start && stop) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Use exactly one of --start and --stop.");
		return EXIT_FAILURE;
	}
	const char * const command = start ? "start" : stop ? (signal_name ? "reload" : "stop") : "status";

	if (!service) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing service name.");
		return EXIT_FAILURE;
	}

	std::string systemd_service("debian@" + systemd_encode(service) + ".service");

	const char systemctl[] = "systemctl";
	const char * systemctl_args[] = {
		systemctl,
		command,
		systemd_service.c_str(),
		0
	};

	execvp(systemctl, const_cast<char **>(systemctl_args));
	const int error(errno);
	std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, systemctl, std::strerror(error));
	return EXIT_FAILURE;
}
