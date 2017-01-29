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

string_pair_list_definition::~string_pair_list_definition() {}
void string_pair_list_definition::action(processor &, const char * text1, const char * text2)
{
	value_list.push_back(pair_type(text1,text2));
}
