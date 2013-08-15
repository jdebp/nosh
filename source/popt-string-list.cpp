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

string_list_definition::~string_list_definition() {}
void string_list_definition::action(processor &, const char * text)
{
	value_list.push_back(text);
}
