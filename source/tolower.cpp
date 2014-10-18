/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <string>
#include <cctype>
#include "utils.h"

/* String manipulation ******************************************************
// **************************************************************************
*/

std::string
tolower (
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) 
		r += std::tolower(*p);
	return r;
}
