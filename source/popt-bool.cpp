/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "popt.h"

using namespace popt;

bool_definition::~bool_definition() {}
void bool_definition::action(processor & /*proc*/)
{
	value = true;
}
