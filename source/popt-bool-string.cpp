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

const char bool_string_definition::a[] = "boolean";
bool_string_definition::~bool_string_definition() {}
void bool_string_definition::action(processor & /*proc*/, const char * text)
{
        if (0 == std::strcmp(text, "on"))
                value = true;
        else
        if (0 == std::strcmp(text, "off"))
                value = false;
        else
        if (0 == std::strcmp(text, "true"))
                value = true;
        else
        if (0 == std::strcmp(text, "false"))
                value = false;
        else
        if (0 == std::strcmp(text, "yes"))
                value = true;
        else
        if (0 == std::strcmp(text, "no"))
                value = false;
        else
        if (0 == std::strcmp(text, "1"))
                value = true;
        else
        if (0 == std::strcmp(text, "0"))
                value = false;
        else
		throw error(text, "not a boolean");
        set = true;
}
