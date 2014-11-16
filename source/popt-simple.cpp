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

simple_named_definition::~simple_named_definition() {}
bool simple_named_definition::execute(processor & proc, char c)
{
	if (char the_short_name = query_short_name()) {
		if (the_short_name == c) {
			action(proc);
			return true;
		}
	}
	return false;
}
bool simple_named_definition::execute(processor &, char, const char *)
{
	return false;
}
bool simple_named_definition::execute(processor & proc, const char * s)
{
	if (const char * the_long_name = query_long_name()) {
		if (0 == std::strcmp(the_long_name, s)) {
			action(proc);
			return true;
		}
	}
	return false;
}
