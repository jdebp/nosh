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

compound_named_definition::~compound_named_definition() {}
bool compound_named_definition::execute(processor & proc, char c)
{
	if (char the_short_name = query_short_name()) {
		if (the_short_name == c) {
			if (const char * text = proc.next_arg()) {
				action(proc, text);
				return true;
			} else
				throw error(long_name, "missing option argument");
		}
	}
	return false;
}
bool compound_named_definition::execute(processor & proc, char c, const char * text)
{
	if (!*text) return false;
	if (char the_short_name = query_short_name()) {
		if (the_short_name == c) {
			action(proc, text);
			return true;
		}
	}
	return false;
}
bool compound_named_definition::execute(processor & proc, const char * s)
{
	if (const char * the_long_name = query_long_name()) {
		if (0 == std::strcmp(the_long_name, s)) {
			if (const char * text = proc.next_arg()) {
				action(proc, text);
				return true;
			} else
				throw error(long_name, "missing option argument");
		}
	}
	return false;
}
