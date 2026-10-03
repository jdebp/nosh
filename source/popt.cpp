/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "popt.h"

using namespace popt;

processor::processor(const char * n0, const ProcessEnvironment & e, definition & d, std::vector<const char *> & f) :
	file_vector(f), envs(e), n(n0), slash(0), def(d), is_stopped(false)
{
}
definition::~definition() {}
