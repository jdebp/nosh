/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <cerrno>
#include <unistd.h>
#include <fstab.h>
#include "utils.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
get_mount_where [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{what}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing \"what\".");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * what(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unrecognized argument(s).");
		throw static_cast<int>(EXIT_USAGE);
	}
	if (1 > setfsent()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unable to open fstab database.");
		throw static_cast<int>(EXIT_TEMPORARY_FAILURE);
	}
	struct fstab * entry(getfsspec(what));
	if (!entry) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, what, "No such mounted device in the fstab database.");
		throw EXIT_FAILURE;
	}
	const char * type(entry->fs_type);
	if ((0 == std::strcmp(type, "xx"))
	||  (0 == std::strcmp(type, "sw"))
	) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, type, "This type of fstab database entry does not represent a mount point.");
		throw EXIT_FAILURE;
	}
	const char * where(entry->fs_file);
	std::cout << where << '\n';
	endfsent();

	throw EXIT_SUCCESS;
}

void
get_mount_what [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{where}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing \"where\".");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * where(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unrecognized argument(s).");
		throw static_cast<int>(EXIT_USAGE);
	}
	if (1 > setfsent()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unable to open fstab database.");
		throw static_cast<int>(EXIT_TEMPORARY_FAILURE);
	}
	struct fstab * entry(getfsfile(where));
	if (!entry) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, where, "No such mount point in the fstab database.");
		throw EXIT_FAILURE;
	}
	const char * type(entry->fs_type);
	if ((0 == std::strcmp(type, "xx"))
	||  (0 == std::strcmp(type, "sw"))
	) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, type, "This type of fstab database entry does not represent a mount point.");
		throw EXIT_FAILURE;
	}
	const char * what(entry->fs_spec);
	std::cout << what << '\n';
	endfsent();

	throw EXIT_SUCCESS;
}
