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
#include "utils.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
console_resize [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool lines_only(false);
	bool c17bit(false);
	bool c18bit(false);
	bool vt420(false);
	try {
		popt::bool_definition lines_only_option('l', "lines", "Only change the number of lines.", lines_only);
		popt::bool_definition c17bit_option('7', "7bit", "Use 7-bit C1 characters.", c17bit);
		popt::bool_definition c18bit_option('8', "8bit", "Use 8-bit C1 characters instead of UTF-8.", c18bit);
		popt::bool_definition vt420_option('4', "vt420", "Use DECSCPP instead of DECSNLS.", vt420);
		popt::definition * top_table[] = {
			&lines_only_option,
			&c17bit_option,
			&c18bit_option,
			&vt420_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "COLSxROWS");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing size.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * size(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}
	unsigned long columns(0UL), rows(0UL);
	if (!lines_only) {
		const char *p(size);
		columns = std::strtoul(p, const_cast<char **>(&p), 10);
		if (p == size || ('x' != *p && 'X' != *p && 0xD7 != static_cast<unsigned char>(*p))) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, size, "Not a number followed by ×.");
			throw static_cast<int>(EXIT_USAGE);
		}
		size = ++p;
	}
	{
		const char *p(size);
		rows = std::strtoul(p, const_cast<char **>(&p), 10);
		if (p == size ||  *p) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, size, "Not a number or trailing junk.");
			throw static_cast<int>(EXIT_USAGE);
		}
	}
	const char * const csi(c17bit ? "\x1B[" : c18bit ? "\x9B" : "\xC2\x9B");
	if (!lines_only)
		std::cout << csi << columns << "$|";	// DECSCPP
	if (vt420)
		std::cout << csi << rows << 't';	// DECSLPP
	else
		std::cout << csi << rows << "*|";	// DECSNLS
	std::cout << csi << "0;0r";			// DECSTBM
	if (!lines_only) {
		std::cout << csi << "?69h";		// DECSLRMM true
		std::cout << csi << "0;0s";		// DECSLRM
		std::cout << csi << "?69l";		// DECSLRMM false
	}
	throw EXIT_SUCCESS;
}
