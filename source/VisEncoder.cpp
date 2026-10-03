/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <string>
#include "config/hasvis.h"
#if defined(HAS_VIS)
#include <vis.h>
#endif
#include "VisEncoder.h"

#if !defined(HAS_VIS)
namespace {
	enum { VIS_WHITE = 1, VIS_SAFE = 2 };
	bool iswhite(unsigned char c) { return 0x20 == c || 0x09 == c || 0x0A == c; }
	// This is a small subset of what actual vis() can do.
	void
	vis (
		char * b,
		char p,
		int flags,
		int
	) {
		unsigned char c(static_cast<unsigned char>(p));
		if (c > 0x20 && c < 0x7F && '\\' != c) {
			*b++ = c;
		} else
		{
			*b++ = '\\';
			if (!(flags & VIS_WHITE) && iswhite(c))
				*b++ = p;
			else
			if ((flags & VIS_SAFE) && std::isspace(c))
				*b++ = p;
			else
			if (0xA0 == c) {
				*b++ = '2';
				*b++ = '4';
				*b++ = '0';
			} else
			if (0x20 == c) {
				*b++ = '0';
				*b++ = '4';
				*b++ = '0';
			} else
			if ('\\' == c) {
				*b++ = '1';
				*b++ = '3';
				*b++ = '4';
			} else
			{
				while (0x80 <= c) {
					*b++ = 'M';
					c -= 0x80;
				}
				if (c < 0x20) {
					*b++ = '^';
					c += 0x40;
					*b++ = c;
				} else
				if (0x7F == c) {
					*b++ = '^';
					*b++ = '?';
				} else
				{
					*b++ = '-';
					*b++ = c;
				}
			}
		}
		*b++ = '\0';
	}
}
#endif

std::string
VisEncoder::process (
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator p(s.begin()), e(s.end()); p != e; ++p) {
		char b[5];
		vis(b, *p, VIS_WHITE, 0);
		r += b;
	}
	return r;
}

std::string
VisEncoder::process_only_unsafe (
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator p(s.begin()), e(s.end()); p != e; ++p) {
		char b[5];
		vis(b, *p, VIS_SAFE, 0);
		r += b;
	}
	return r;
}
