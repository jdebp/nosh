/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstdlib>

#include "popt.h"

using namespace popt;

signed_number_definition::~signed_number_definition() {}
void signed_number_definition::action(processor &, const char * text)
{
	const char * old(text);
	value = std::strtol(text, const_cast<char **>(&text), base);
	if (text == old || *text)
		throw error(old, "not a number");
	set = true;
}
