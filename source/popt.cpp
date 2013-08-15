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

processor::processor(const char * n, definition & d, std::vector<const char *> & f) :
	file_vector(f), name(n), slash(0), def(d), is_stopped(false)
{
}
definition::~definition() {}
