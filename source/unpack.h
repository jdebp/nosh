/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_UNPACK_H)
#define INCLUDE_UNPACK_H

#include <cstddef>
#include <inttypes.h>

extern
uint64_t
unpack_bigendian (
	const void * b,
	std::size_t len
);

extern
uint64_t
unpack_littleendian (
	const void * b,
	std::size_t len
);

#endif
