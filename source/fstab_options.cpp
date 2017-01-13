/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <string>
#include "utils.h"

std::list<std::string>
split_fstab_options (
	const char * o
) {
	std::list<std::string> r;
	if (o) {
		std::string q;
		while (*o) {
			const char c(*o++);
			if (c != ',') {
				q += c;
			} else {
				if (!q.empty()) {
					r.push_back(q);
					q.clear();
				}
			}
		}
		if (!q.empty()) r.push_back(q);
	}
	return r;
}

void
delete_fstab_option (
	std::list<std::string> & options,
	const char * o
) {
	for (std::list<std::string>::iterator i(options.begin()); options.end() != i; ) {
		if (*i == o)
			i = options.erase(i);
		else
			++i;
	}
}

bool
has_option (
	const std::list<std::string> & options,
	std::string prefix,
	std::string & remainder
) {
	prefix += '=';
	for (std::list<std::string>::const_iterator i(options.begin()); options.end() != i; ++i) {
		if (begins_with(*i, prefix, remainder))
			return true;
	}
	return false;
}

bool
has_option (
	const std::list<std::string> & options,
	const std::string & opt
) {
	for (std::list<std::string>::const_iterator i(options.begin()); options.end() != i; ++i) {
		if (*i == opt)
			return true;
	}
	return false;
}
