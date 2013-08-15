/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstddef>
#include <inttypes.h>
#include "unpack.h"

extern
uint64_t
unpack_bigendian (
	const void * b,
	std::size_t len
) {
	uint_fast64_t r(0U);
	const unsigned char *p(reinterpret_cast<const unsigned char *>(b));
	while (len) {
		--len;
		r <<= 8;
		r |= *p++;
	}
	return r;
}

extern
uint64_t
unpack_littleendian (
	const void * b,
	std::size_t len
) {
	uint_fast64_t r(0U);
	const unsigned char *p(reinterpret_cast<const unsigned char *>(b));
	while (len) {
		--len;
		r <<= 8;
		r |= p[len];
	}
	return r;
}
