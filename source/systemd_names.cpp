/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "utils.h"
#include <cstdio>

#if defined(__LINUX__) || defined(__linux__)
// Linux useradd allows backslash in user/group names.
static const char ESCAPE_CHAR('\\');
#else
// BSD pw useradd is stricter.
static const char ESCAPE_CHAR('_');
#endif

std::string 
systemd_name_unescape ( 
	bool alt,
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator e(s.end()), q(s.begin()); e != q; ) {
		char c(*q++);
		if (!alt && '-' == c) {
			r += '/';
		} else
		if (ESCAPE_CHAR != c) {
			r += c;
		} else
		if (e == q) {
			r += c;
		} else
		{
			c = *q++;
			if ('X' != c && 'x' != c)
				r += c;
			else {
				unsigned v(0U);
				for (unsigned n(0U);n < 2U; ++n) {
					if (e == q) break;
					c = *q;
					if (!std::isxdigit(c)) break;
					++q;
					c = std::isdigit(c) ? (c - '0') : (std::tolower(c) - 'a' + 10);
					v = (v << 4) | c;
				}
				r += char(v);
			}
		}
	}
	return r;
}

std::string 
systemd_name_escape ( 
	bool alt,
	const std::string & s
) {
	std::string r;
	if (alt) {
		for (std::string::const_iterator e(s.end()), q(s.begin()); e != q; ) {
			const char c(*q++);
			if (ESCAPE_CHAR == c || '\\' == c || '@' == c || '/' == c || ':' == c || ',' == c) {
				char buf[6];
				snprintf(buf, sizeof buf, "%cx%02x", ESCAPE_CHAR, c);
				r += std::string(buf);
			} else
				r += c;
		}
	} else {
		for (std::string::const_iterator e(s.end()), q(s.begin()); e != q; ) {
			const char c(*q++);
			if ('/' == c) {
				r += '-';
			} else
			if (ESCAPE_CHAR == c || '-' == c) {
				char buf[6];
				snprintf(buf, sizeof buf, "%cx%02x", ESCAPE_CHAR, c);
				r += std::string(buf);
			} else
				r += c;
		}
	}
	return r;
}
