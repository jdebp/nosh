/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <string>
#include "utils.h"

/* String generation *******************************************************
// **************************************************************************
*/

std::string
multi_line_comment (
	const std::string & s
) {
	std::string r;
	bool bol(true);
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		const char c(*p);
		if ('\n' == c) 
			bol = true;
		else if (bol) {
			r += '#';
			bol = false;
		}
		r += c;
	}
	if (!bol) r += '\n';
	return r;
}
