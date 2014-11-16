/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <string>
#include <cstdio>
#include <cstring>
#include "utils.h"

bool
read_line (
	FILE * f,
	std::string & l
) {
	l.clear();
	for (;;) {
		const int c(std::fgetc(f));
		if (EOF == c) return !l.empty();
		if ('\n' == c) return true;
		l += static_cast<unsigned char>(c);
	}
}
