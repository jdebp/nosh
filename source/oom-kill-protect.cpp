/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/procctl.h>
#endif
#include <unistd.h>
#include "utils.h"
#include "ProcessEnvironment.h"
#include "popt.h"
#include "FileStar.h"

/* Main function ************************************************************
// **************************************************************************
*/

#if defined(__LINUX__) || defined(__linux__)
static const char oom_score_filename[] = "/proc/self/oom_score_adj";
#endif

void
oom_kill_protect ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool oknoaccess(false);
	try {
		popt::bool_definition oknoaccess_option('\0', "oknoaccess", "Do not fail if the OOM score adjustment file is not overwritable.", oknoaccess);
		popt::definition * top_table[] = {
			&oknoaccess_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "level prog");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing protection level.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * arg(args.front());
	args.erase(args.begin());

	if (0 == std::strcmp(arg, "fromenv")) {
		const char * env(envs.query("oomprotect"));
		if (!env) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "oomprotect", "Missing environment value");
			throw EXIT_FAILURE;
		}
		arg = env;
	}

	const char * end(arg);
	long int l(std::strtol(arg, const_cast<char **>(&end), 0));

#if defined(__LINUX__) || defined(__linux__)
	int protect(0);
	if (end != arg && !*end) {
		if (l < -1000 || l > 1000) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, arg, "Bad protection level");
			throw EXIT_FAILURE;
		}
		protect = static_cast<int>(l);
	} else {
		const std::string r(tolower(arg));
		if (is_bool_true(r))
			protect = -1000;
		else
		if (is_bool_false(r))
			protect = 0;
		else {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, arg, "Bad protection level");
			throw EXIT_FAILURE;
		}
	}
	FileStar procfile(std::fopen(oom_score_filename, "w"));
	if (!procfile) {
		const int error(errno);
		if (!oknoaccess || (EPERM != error && EACCES != error)) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, oom_score_filename, std::strerror(error));
			throw EXIT_FAILURE;
		}
	}
	if (0 >= std::fprintf(procfile, "%d\n", protect)
	||  0 > std::fflush(procfile)
	) {
		const int error(errno);
		if (!oknoaccess || (EPERM != error && EACCES != error)) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, oom_score_filename, std::strerror(error));
			throw EXIT_FAILURE;
		}
	}
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	bool protect(false);
	if (end != arg && !*end)
		protect = l < -999;
	else {
		const std::string r(tolower(arg));
		if (is_bool_true(r))
			protect = true;
		else 
		if (is_bool_false(r))
			protect = false;
		else {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, arg, "Bad protection level");
			throw EXIT_FAILURE;
		}
	}

	int flags(protect ? PPROT_SET : PPROT_CLEAR);
	if (0 > procctl(P_PID, getpid(), PROC_SPROTECT, &flags)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "procctl", std::strerror(error));
		throw EXIT_FAILURE;
	}
#else
	if (end == arg || *end) {
		const std::string r(tolower(arg));
		if (!is_bool_true(r) && !is_bool_false(r)) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, arg, "Bad protection level");
			throw EXIT_FAILURE;
		}
	}
#endif

	next_prog = arg0_of(args);
}
