/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "popt.h"
#include "fdutils.h"

/* Control character options ************************************************
// **************************************************************************
*/

struct open_mode_definition : public popt::simple_named_definition {
public:
	open_mode_definition(char s, const char * l, const char * d, int p_m, int & p_v) : simple_named_definition(s, l, d), m(p_m), v(p_v) {}
	virtual void action(popt::processor &);
	virtual ~open_mode_definition();
protected:
	int m;
	int & v;
};
open_mode_definition::~open_mode_definition() {}
void open_mode_definition::action(popt::processor &)
{
	if (v & O_NOCTTY)
		throw popt::error(long_name, "conflicting option");
	v = m;
}

/* Main function ************************************************************
// **************************************************************************
*/

void
fdredir ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	unsigned long permissions = 0666;
	int mode(0);
	try {
		bool non_blocking(false);
		popt::bool_definition non_blocking_option('n', "non-blocking", "Open in non-blocking mode.", non_blocking);
		open_mode_definition read_option('r', "read", "Open the file for read-only.", O_RDONLY|O_NOCTTY, mode);
		open_mode_definition append_clobber_option('a', "append", "Open the file for write-only append.", O_WRONLY|O_NOCTTY|O_CREAT|O_APPEND, mode);
		open_mode_definition append_option('c', "append-noclobber", "Open the file for write-only append but don't overwrite an existing file.", O_WRONLY|O_NONBLOCK|O_NOCTTY|O_CREAT|O_EXCL|O_APPEND, mode);
		open_mode_definition write_clobber_option('w', "write", "Open the file for write only.", O_WRONLY|O_NOCTTY|O_CREAT|O_TRUNC, mode);
		open_mode_definition write_option('x', "write-noclobber", "Open the file for write-only but don't overwrite an existing file.", O_WRONLY|O_NOCTTY|O_CREAT|O_EXCL, mode);
		open_mode_definition update_option('u', "update", "Open the file for read-write.", O_RDWR|O_NOCTTY|O_CREAT, mode);
		open_mode_definition directory_option('d', "directory", "Open the directory for read-execute.", O_RDONLY|O_NOCTTY
#if defined(O_DIRECTORY)
				|O_DIRECTORY
#endif
				, mode);
		popt::unsigned_number_definition permissions_option('m', "mode", "number", "Specify the file permissions.", permissions, 0);
		popt::definition * top_table[] = {
			&non_blocking_option,
			&read_option,
			&write_clobber_option,
			&write_option,
			&append_clobber_option,
			&append_option,
			&update_option,
			&directory_option,
			&permissions_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{fd} {file} {prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		if (non_blocking) mode |= O_NONBLOCK;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing destination file descriptor number.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * to(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing source file descriptor number.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * file(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	const char * old(0);
	old = to;
	const unsigned long d(std::strtoul(to, const_cast<char **>(&to), 0));
	if (to == old || *to) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, old, "Not a number.");
		throw EXIT_FAILURE;
	}

	const bool append(mode & O_APPEND);
	mode &= ~O_APPEND;
	const int s(openat(AT_FDCWD, file, mode, permissions));
	if (0 > s) {
file_error:
		const int error(errno);
		if (0 <= s) close(s);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, file, std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (append) {
		if (0 > lseek(s, 0, SEEK_END)) goto file_error;
	}
	if (mode & O_NONBLOCK) {
		if (0 > set_non_blocking(s, false)) goto file_error;
	}
	if (static_cast<unsigned long>(s) != d) {
		if (0 > dup2(s, d)) goto file_error;
		close(s);
	}
}
