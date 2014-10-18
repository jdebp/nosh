/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <unistd.h>
#include <cstddef>
#include <cstring>
#include "utils.h"
#include "popt.h"

/* Table of commands ********************************************************
// **************************************************************************
*/

// There are no extra personalities over and above the built-in commands.
extern const
struct command 
personalities[] = {
	{	"",		0		},
};
const std::size_t num_personalities = 0;

/* System commands **********************************************************
// **************************************************************************
*/

static inline
bool
stripfast (
	const char * & prog
) {
	if ('f' != prog[0] || 'a' != prog[1] || 's' != prog[2] || 't' != prog[3]) return false;
	prog += 4;
	return true;
}

void
reboot_poweroff_halt_command (
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	bool force(stripfast(prog));
	if (0 == std::strcmp(prog, "boot")) prog = "reboot";
	try {
		bool poweroff(false);
		bool halt(false);
		bool reboot(false);
		popt::bool_definition force_option('f', "force", "Bypass service shutdown jobs.", force);
		popt::bool_definition poweroff_option('p', "poweroff", "Power off, even if this is not the poweroff command.", poweroff);
		popt::bool_definition halt_option('h', "halt", "Halt, even if this is not the halt command.", halt);
		popt::bool_definition reboot_option('r', "reboot", "Reboot, even if this is not the reboot command.", reboot);
		popt::definition * main_table[] = {
			&force_option,
			&poweroff_option,
			&halt_option,
			&reboot_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;

		if (poweroff) prog = "poweroff";
		else if (reboot) prog = "reboot";
		else if (halt) prog = "halt";
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	args.clear();
	if (force) {
		args.insert(args.end(), "system-control");
		args.insert(args.end(), prog);
		args.insert(args.end(), "--force");
	} else {
		// Stuff that we put into args has to have static storage duration.
		static char opt[3] = { '-', ' ', '\0' };

		args.insert(args.end(), "shutdown");
		opt[1] = prog[0];
		args.insert(args.end(), opt);
		// systemd shutdown has this sensible default if the time specification is omitted.
		// BSD shutdown does not.
		args.insert(args.end(), "now");
	}
	args.insert(args.end(), 0);
	next_prog = arg0_of(args);
}

// Some commands are thin wrappers around system-control subcommands of the same name.
void
wrap_system_control_subcommand (
	const char * & next_prog,
	std::vector<const char *> & args
) {
	args.insert(args.begin(), "system-control");
	next_prog = arg0_of(args);
}

void
rcctl (
	const char * & next_prog,
	std::vector<const char *> & args
) {
	next_prog = args[0] = "system-control";
}

void
init ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	if (1 == getpid())
		next_prog = "system-manager";
	else
		next_prog = args[0] = "telinit";
}
