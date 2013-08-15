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

unsigned_number_definition::~unsigned_number_definition() {}
void unsigned_number_definition::action(processor &, const char * text)
{
	const char * old(text);
	value = std::strtoul(text, const_cast<char **>(&text), base);
	if (text == old || *text)
		throw error(old, "not a number");
	set = true;
}
