/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <string>
#include <cctype>
#include "utils.h"

/* String conversion ********************************************************
// **************************************************************************
*/

unsigned 
val ( 
	const std::string & s 
) {
	unsigned v = 0;
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		const char c(*p);
		if (!std::isdigit(c)) break;
		v = v * 10U + static_cast<unsigned char>(c - '0');
	}
	return v;
}
