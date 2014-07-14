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

bool
begins_with (
	const std::string & s,
	const std::string & f,
	std::string & r
) {
	const std::string::size_type fl(f.length());
	const std::string::size_type sl(s.length());
	if (sl < fl) return false;
	if (s.substr(0, fl) != f) return false;
	r = s.substr(fl, std::string::npos);
	return true;
}
