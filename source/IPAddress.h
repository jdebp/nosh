/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_IPADDRESS_H)
#define INCLUDE_IPADDRESS_H

#include <stdint.h>

namespace IPAddress {

	extern void SetPrefix (void * v, std::size_t l, unsigned prefixlen) ;
	extern bool IsPrefix (const sockaddr & addr, unsigned & prefixlen) ;
	extern bool IsLinkLocal (const in6_addr & a) ;

	extern inline
	void
	SetPrefix (
		in_addr & addr,
		unsigned long prefixlen
	) {
		SetPrefix(&addr, sizeof addr, prefixlen);
	}

	extern inline
	void
	SetPrefix (
		in6_addr & addr,
		unsigned long prefixlen
	) {
		SetPrefix(&addr, sizeof addr, prefixlen);
	}

}

extern in6_addr operator ~ (const in6_addr & r) ;
extern in6_addr operator & (const in6_addr & l, const in6_addr & r) ;
extern in6_addr operator | (const in6_addr & l, const in6_addr & r) ;

extern in_addr operator ~ (const in_addr & r) ;
extern in_addr operator & (const in_addr & l, const in_addr & r) ;
extern in_addr operator | (const in_addr & l, const in_addr & r) ;

#endif
