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

compound_2arg_named_definition::~compound_2arg_named_definition() {}
bool compound_2arg_named_definition::execute(processor & proc, char c)
{
	if (char the_short_name = query_short_name()) {
		if (the_short_name == c) {
			if (const char * text1 = proc.next_arg()) {
				if (const char * text2 = proc.next_arg()) {
					action(proc, text1, text2);
					return true;
				} else
					throw error(long_name, "missing option argument");
			} else
				throw error(long_name, "missing option argument");
		}
	}
	return false;
}
bool compound_2arg_named_definition::execute(processor & proc, char c, const char * text1)
{
	if (!*text1) return false;
	if (char the_short_name = query_short_name()) {
		if (the_short_name == c) {
			if (const char * text2 = proc.next_arg()) {
				action(proc, text1, text2);
				return true;
			} else
				throw error(long_name, "missing option argument");
		}
	}
	return false;
}
bool compound_2arg_named_definition::execute(processor & proc, const char * s)
{
	if (const char * the_long_name = query_long_name()) {
		if (0 == std::strcmp(the_long_name, s)) {
			if (const char * text1 = proc.next_arg()) {
				if (const char * text2 = proc.next_arg()) {
					action(proc, text1, text2);
					return true;
				} else
					throw error(long_name, "missing option argument");
			} else
				throw error(long_name, "missing option argument");
		}
	}
	return false;
}
