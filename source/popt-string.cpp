/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "popt.h"

using namespace popt;

string_definition::~string_definition() {}
void string_definition::action(processor &, const char * text)
{
	value = text;
}
