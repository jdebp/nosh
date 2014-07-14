/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <string>
#include "utils.h"

/* String manipulation ******************************************************
// **************************************************************************
*/

std::string
ltrim (
	const std::string & s
) {
	for (std::string::size_type p(0); s.length() != p; ++p) {
		if (!std::isspace(s[p])) return s.substr(p, std::string::npos);
	}
	return std::string();
}

std::string
rtrim (
	const std::string & s
) {
	for (std::string::size_type p(s.length()); p > 0; ) {
		--p;
		if (!std::isspace(s[p])) return s.substr(0, p + 1);
	}
	return std::string();
}
