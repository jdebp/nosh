/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cctype>
#include <cstring>

/* Filename manipulation ****************************************************
// **************************************************************************
*/

const char *
basename_of (
	const char * s
) {
	if (const char * slash = std::strrchr(s, '/'))
		s = slash + 1;
#if defined(__OS2__) || defined(__WIN32__) || defined(__NT__)
	else if (const char * bslash = std::strrchr(s, '\\'))
		s = bslash + 1;
	else if (std::isalpha(s[0]) && ':' == s[1])
		s += 2;
#endif
	return s;
}
