/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#include "ttyutils.h"

namespace {

	struct simple_rewrite_definition : public popt::simple_named_definition {
	public:
		simple_rewrite_definition(char s, const char * l, const char * d, std::vector<const char *> & n, const char * w0, const char * w1) : simple_named_definition(s, l, d), next_args(n), r0(w0), r1(w1) {}
		virtual ~simple_rewrite_definition();
	protected:
		std::vector<const char *> & next_args;
		const char * const r0, * const r1;
		virtual void action(popt::processor &);
	};

}

simple_rewrite_definition::~simple_rewrite_definition() {}
void simple_rewrite_definition::action(popt::processor & /*proc*/)
{
	next_args.insert(next_args.end(), r0);
	next_args.insert(next_args.end(), r1);
}

/* Main function ************************************************************
// **************************************************************************
*/

void
procstat ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	std::vector<const char *> next_args;
	try {
		simple_rewrite_definition b_option('b', 0, "binary format", next_args, "--format", "binary");
		simple_rewrite_definition c_option('c', 0, "command and arguments format", next_args, "--format", "args");
		simple_rewrite_definition e_option('e', 0, "environment format", next_args, "--format", "envs");
		simple_rewrite_definition f_option('f', 0, "file descriptor format", next_args, "--format", "fds");
		simple_rewrite_definition i_option('i', 0, "process signal format", next_args, "--format", "process-signal");
		simple_rewrite_definition j_option('j', 0, "thread signal format", next_args, "--format", "thread-signal");
		simple_rewrite_definition k_option('k', 0, "kernel stack format", next_args, "--format", "kernel-stack");
		simple_rewrite_definition l_option('l', 0, "resource limits format", next_args, "--format", "resource-limits");
		simple_rewrite_definition r_option('r', 0, "resource usage format", next_args, "--format", "resource-usage");
		simple_rewrite_definition s_option('s', 0, "security credential format", next_args, "--format", "credentials");
		simple_rewrite_definition S_option('S', 0, "cpuset format", next_args, "--format", "cpuset");
		simple_rewrite_definition t_option('t', 0, "thread format", next_args, "--format", "thread");
		simple_rewrite_definition v_option('v', 0, "virtual memory format", next_args, "--format", "vm");
		simple_rewrite_definition x_option('x', 0, "auxiliary vector format", next_args, "--format", "aux");
		// FIXME -h suppresses headers
		popt::definition * top_table[] = {
			&b_option,
			&c_option,
			&e_option,
			&f_option,
			&i_option,
			&j_option,
			&k_option,
			&l_option,
			&r_option,
			&s_option,
			&S_option,
			&t_option,
			&v_option,
			&x_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "");

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

	next_args.insert(next_args.begin(), "list-process-table");

	args = next_args;
	next_prog = arg0_of(args);
}
