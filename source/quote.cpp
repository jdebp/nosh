/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "utils.h"
#include <cstdio>

std::string
quote_for_nosh (
	const std::string & s
) {
	std::string r;
	bool quote(s.empty());
	for (std::string::const_iterator p(s.begin()), e(s.end()); e != p; ++p) {
		const char c(*p);
		if (!std::isalnum(c) && '/' != c && '-' != c && '_' != c && '.' != c) {
			quote = true;
			if ('\"' == c || '\\' == c)
				r += '\\';
		}
		r += c;
	}
	if (quote) r = "\"" + r + "\"";
	return r;
}

static inline
int	/// \retval 0 no quotes \retval 1 single quote \retval 2 double quotes
quoting_level_needed (
	const std::string & s
) {
	if (s.empty()) return 1;
	int r(0);
	for (std::string::const_iterator p(s.begin()), e(s.end()); e != p; ++p) {
		const char c(*p);
		if (std::isalnum(c) || '/' == c || '-' == c || '.' == c || '_' == c)
			continue;
		if (std::isspace(c)) {
			if (r < 1) r = 1;
		} else
		if ('\0' == c) {
			if (r < 2) r = 2;
		} else
		if ('\"' == c) {
			if (r < 1) r = 1;
		} else
		if ('\'' == c) {
			if (r < 1) r = 2;
		} else
		{
			if (r < 1) r = 1;
		}
	}
	return r;
}

std::string
quote_for_sh (
	const std::string & s
) {
	std::string r;
	switch (quoting_level_needed(s)) {
		case 0:
			r = s;
			break;
		default:
		case 1:
			r += '\'';
			// In the sh rules \ is never an escape character and there is no way to quote ' .
			for (std::string::const_iterator p(s.begin()), e(s.end()); e != p; ++p) {
				const char c(*p);
				if ('\'' == c)
					r += "'\\'";
				r += c;
			}
			r += '\'';
			break;
		case 2:
			r += '\"';
			// sh has some quite wacky rules where \ is mostly not an escape character.
			for (std::string::const_iterator p(s.begin()), e(s.end()); e != p; ++p) {
				const char c(*p);
				if ('\"' == c || '\\' == c || '$' == c || '`' == c) {
					r += '\\';
					r += c;
				} else
				if ('\r' == c || '\n' == c) {
					r += "\"'";
					r += c;
					r += "'\"";
				} else
				{
					r += c;
				}
			}
			r += '\"';
			break;
	}
	return r;
}
