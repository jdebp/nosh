/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstddef>
#include <inttypes.h>
#include "pack.h"

void
pack_bigendian (
	unsigned char * buf,
	uint64_t v,
	std::size_t len
) {
	while (len) {
		--len;
		buf[len] = v & 0xFF;
		v >>= 8;
	}
}

void
pack_littleendian (
	unsigned char * buf,
	uint64_t v,
	std::size_t len
) {
	while (len) {
		--len;
		*buf++ = v & 0xFF;
		v >>= 8;
	}
}
