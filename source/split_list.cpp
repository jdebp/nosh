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
	std::string q;
	for (std::string::size_type p(0); s.length() != p; ++p) {
		const char c(s[p]);
		if (!std::isspace(c)) {
			q += c;
		} else {
			if (!q.empty()) {
				r.push_back(q);
				q.clear();
			}
		}
	}
	if (!q.empty()) r.push_back(q);
	return r;
}
