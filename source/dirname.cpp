/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include "utils.h"

/* Filename manipulation ****************************************************
// **************************************************************************
*/

std::string
dirname_of (
	const std::string & s
) {
	const char * p(s.c_str());
	const char * b(basename_of(p));
	if (p + 1 < b) 
		return std::string(p, b - 1);
	else if (p < b) 
		return std::string(p, b);
	else
		return ".";
}
