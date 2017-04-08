/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "utils.h"
#include <cstdio>

#include "systemd_names_escape_char.h"

std::string 
systemd_name_unescape ( 
	bool alt,
	bool ext,
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator e(s.end()), q(s.begin()); e != q; ) {
		char c(*q++);
		if (!alt && '-' == c) {
			c = '/';
		} else
		if (ESCAPE_CHAR == c && e != q) {
			c = *q++;
			if ('X' == c || 'x' == c) {
				unsigned v(0U);
				for (unsigned n(0U);n < 2U; ++n) {
					if (e == q) break;
					c = *q;
					if (!std::isxdigit(c)) break;
					++q;
					const unsigned char d(std::isdigit(c) ? (c - '0') : (std::tolower(c) - 'a' + 10));
					v = (v << 4) | d;
				}
				c = char(v);
			} else 
			if (ext) {
				switch (c) {
					case ESCAPE_CHAR:	break;
					case 'a':	c = '@'; break;
					case 'c':	c = ':'; break;
					case 'd':	c = '.'; break;
					case 'h':	c = ';'; break;
					case 'm':	c = '-'; break;
					case 'p':	c = '+'; break;
					case 'u':
						if (e == q) { 
							r += ESCAPE_CHAR;
							break;
						}
						c = *q++;
						if (std::isalpha(c)) 
							c = std::toupper(c);
						break;
					case 'v':	c = ','; break;
					default:
						break;
				}
			}
		}
		r += c;
	}
	return r;
}

std::string 
systemd_name_escape ( 
	bool alt,
	bool ext,
	const std::string & s
) {
	std::string r;
	if (alt) {
		for (std::string::const_iterator e(s.end()), q(s.begin()); e != q; ) {
			const char c(*q++);
			if (ESCAPE_CHAR == c || prohibited_in_alt(c)) {
				char buf[6];
				buf[0] = ESCAPE_CHAR;
				if (ext && ESCAPE_CHAR == c) {
					buf[1] = ESCAPE_CHAR;
					buf[2] = '\0';
				} else
				if (ext && ':' == c) {
					buf[1] = 'c';
					buf[2] = '\0';
				} else
				if (ext && ';' == c) {
					buf[1] = 'h';
					buf[2] = '\0';
				} else
				if (ext && '.' == c) {
					buf[1] = 'd';
					buf[2] = '\0';
				} else
				if (ext && ',' == c) {
					buf[1] = 'v';
					buf[2] = '\0';
				} else
				if (ext && '-' == c) {
					buf[1] = 'm';
					buf[2] = '\0';
				} else
				if (ext && '+' == c) {
					buf[1] = 'p';
					buf[2] = '\0';
				} else
				if (ext && '@' == c) {
					buf[1] = 'a';
					buf[2] = '\0';
				} else
				if (ext && std::isupper(c)) {
					buf[1] = 'u';
					buf[2] = std::tolower(c);
					buf[3] = '\0';
				} else
					snprintf(buf + 1, sizeof buf - 1, "x%02x", c);
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
