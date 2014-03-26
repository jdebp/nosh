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
ends_in (
	const std::string & s,
	const std::string & p,
	std::string & r
) {
	const std::string::size_type pl(p.length());
	const std::string::size_type sl(s.length());
	if (sl < pl) return false;
	if (s.substr(sl - pl, pl) != p) return false;
	r = s.substr(0, sl - pl);
	return true;
}
