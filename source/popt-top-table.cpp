/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <iostream>
#include <unistd.h>
#include <cstring>

#include "popt.h"
#include "ttyname.h"
#include "TerminalCapabilities.h"
#include "ECMA48Output.h"

using namespace popt;

top_table_definition::~top_table_definition() {}
bool top_table_definition::execute(processor & proc, char c)
{
	if ('?' == c) { do_help(proc); return true; }
	return table_definition::execute(proc, c);
}
bool top_table_definition::execute(processor & proc, const char * s)
{
	if (0 == std::strcmp(s, "help")) { do_help(proc); return true; }
	if (0 == std::strcmp(s, "usage")) { do_usage(proc); return true; }
	return table_definition::execute(proc, s);
}
void top_table_definition::do_usage(processor & proc)
{
	TerminalCapabilities caps(proc.envs);
	ECMA48Output ecma48(caps, std::cout, true /* C1 is 7-bit aliased */, false /* C1 is not raw 8-bit */);
	const bool do_colour(ECMA48Output::query_use_colours(proc.envs, STDOUT_FILENO));
	std::string shorts("?");
	gather_combining_shorts(shorts);
	if (do_colour) ecma48.set_underline(true);
	std::cout << "Usage";
	if (do_colour) ecma48.set_underline(false);
	std::cout << ": ";
	if (do_colour) ecma48.set_boldface(true);
	std::cout << proc.name();
	if (do_colour) ecma48.set_boldface(false);
	std::cout << " [";
	if (do_colour) ecma48.set_boldface(true);
	std::cout.put('-') << shorts;
	if (do_colour) ecma48.set_boldface(false);
	std::cout << "] [";
	if (do_colour) ecma48.set_boldface(true);
	std::cout << "--help";
	if (do_colour) ecma48.set_boldface(false);
	std::cout << "] [";
	if (do_colour) ecma48.set_boldface(true);
	std::cout << "--usage";
	if (do_colour) ecma48.set_boldface(false);
	std::cout << "] ";
	long_usage(std::cout, ecma48, do_colour);
	std::cout << arguments_description << '\n';
	proc.stop();
}
void top_table_definition::do_help(processor & proc)
{
	TerminalCapabilities caps(proc.envs);
	ECMA48Output ecma48(caps, std::cout, true /* C1 is 7-bit aliased */, false /* C1 is not raw 8-bit */);
	const bool do_colour(ECMA48Output::query_use_colours(proc.envs, STDOUT_FILENO));
	do_usage(proc);
	std::cout.put('\n');
	help(std::cout, ecma48, do_colour);
	proc.stop();
}
