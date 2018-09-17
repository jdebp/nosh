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

std::list<std::string>
split_list (
	const std::string & s
) {
	std::list<std::string> r;
	std::string * current = 0;
	enum { UNQUOTED, DOUBLE, SINGLE } quote(UNQUOTED);
	bool slash(false);
	for (std::string::const_iterator p(s.begin()), e(s.end()); e != p; ++p) {
		const char c(*p);
		if (UNQUOTED == quote && !slash) {
			if (std::isspace(c)) {
				current = 0;
				continue;
			}
		}
		if (!current) {
			r.push_back(std::string());
			current = &r.back();
		}
		if (slash) {
			*current += char(c);
			slash = false;
		} else switch (quote) {
			case SINGLE:
				if ('\'' == c)
					quote = UNQUOTED;
				else
					*current += char(c);
				break;
			case DOUBLE:
				if ('\\' == c)
					slash = true;
				else if ('\"' == c)
					quote = UNQUOTED;
				else
					*current += char(c);
				break;
			case UNQUOTED:
				if ('\\' == c)
					slash = true;
				else if ('\"' == c)
					quote = DOUBLE;
				else if ('\'' == c)
					quote = SINGLE;
				else
					*current += char(c);
				break;
		}
	}
	return r;
}
