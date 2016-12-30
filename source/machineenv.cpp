/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#if defined(_GNU_SOURCE)
#include <sys/utsname.h>
#else
#include <climits>
#endif
#include <unistd.h>
#include "utils.h"
#include "popt.h"
#include "machine_id.h"

static inline
void
set (
	const char * var,
	const char * val
) {
	if (val)
		setenv(var, val, 1);
	else
		unsetenv(var);
}

/* Main function ************************************************************
// **************************************************************************
*/

void
machineenv ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[0] = {};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "prog");

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

	machine_id::erase();
	if (!machine_id::read_non_volatile() && !machine_id::read_fallbacks())
	       machine_id::create();
	set("MACHINEID", machine_id::human_readable_form_compact().c_str());

#if defined(_GNU_SOURCE)
	struct utsname uts;
	uname(&uts);
	set("HOSTNAME", uts.nodename);
	set("DOMAINNAME", uts.domainname);
#elif defined(HOST_NAME_MAX)
	char name[HOST_NAME_MAX + 1];
	gethostname(name, sizeof name);
	set("HOSTNAME", name);
	getdomainname(name, sizeof name);
	set("DOMAINNAME", name);
#else
	const long max(sysconf(_SC_HOST_NAME_MAX));
	if (0 > max) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "HOST_NAME_MAX", std::strerror(error));
		throw EXIT_FAILURE;
	}
	std::string name(max + 1, ' ');
	gethostname(const_cast<char *>(name.data()), max + 1);
	set("HOSTNAME", name.c_str());
	getdomainname(const_cast<char *>(name.data()), max + 1);
	set("DOMAINNAME", name.c_str());
#endif
}
