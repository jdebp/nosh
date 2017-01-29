/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_PACK_H)
#define INCLUDE_PACK_H

#include <cstddef>
#include <inttypes.h>

extern
void
pack_bigendian (
	unsigned char * buf,
	uint64_t v,
	std::size_t len
) ;

extern
void
pack_littleendian (
	unsigned char * buf,
	uint64_t v,
	std::size_t len
) ;

#endif
