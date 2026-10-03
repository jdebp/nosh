/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstring>

#include "popt.h"

using namespace popt;

const char tui_level_definition::a[] = "level";
tui_level_definition::~tui_level_definition() {}
void tui_level_definition::action(popt::processor & /*proc*/, const char * text)
{
	if (0 == std::strcmp(text, "unicode")) {
		v = 0;
		set = true;
	} else
	if (0 == std::strcmp(text, "old-unicode")) {
		v = 1;
		set = true;
	} else
	if (0 == std::strcmp(text, "ascii")) {
		v = 2;
		set = true;
	} else
		throw popt::error(text, "TUI level specification is not {unicode|old-unicode|ascii}");
}
