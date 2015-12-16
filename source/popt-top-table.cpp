/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <cctype>

#include "popt.h"

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
	std::string shorts("?");
	gather_combining_shorts(shorts);
	std::cout << "Usage: " << proc.name << " [-" << shorts << "] [--help] [--usage] ";
	long_usage();
	std::cout << arguments_description << '\n';
	proc.stop();
}
void top_table_definition::do_help(processor & proc)
{
	do_usage(proc);
	std::cout.put('\n');
	help();
	proc.stop();
}
