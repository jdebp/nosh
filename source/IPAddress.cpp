/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstddef>
#include <climits>
#include <cstring>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "IPAddress.h"

namespace {

	inline
	unsigned
	popcount (
		unsigned char v
	) {
		unsigned r(0U);
		while (v) {
			++r;
			v &= v - 1U;	// Clear the rightmost 1 bit.
		}
		return r;
	}

	inline
	unsigned
	ctz (
		unsigned char v
	) {
		unsigned char nv(~v);
		unsigned r(0U);
		while ((nv & 1U) == 1U) {
			++r;
			nv >>= 1U;
		}
		return r;
	}

	inline
	unsigned
	popcount (
		const void * v,
		std::size_t l
	) {
		unsigned r(0U);
		for (const unsigned char * u(reinterpret_cast<const unsigned char *>(v)); l; ++u, --l)
			r += popcount(*u);
		return r;
	}

	inline
	unsigned
	ctz (
		const void * v,
		std::size_t l
	) {
		unsigned r(0U);
		const unsigned char * u(reinterpret_cast<const unsigned char *>(v));
		while (l) {
			--l;
			if (u[l]) {
				r += ctz(u[l]);
				break;
			} else
				r += CHAR_BIT;
		}
		return r;
	}

	bool
	RawIsPrefix (
		const void * v,
		std::size_t l,
		unsigned & prefixlen
	) {
#if defined(__GNUC__)
		// These are just optimizations, and do not have to be exhaustive or comprehensive.
		if (l == sizeof(uint32_t)) {
			const uint32_t v32(be32toh(*reinterpret_cast<const uint32_t *>(v)));
			if (sizeof(unsigned int) == sizeof(uint32_t)) {
				prefixlen = __builtin_popcount(v32);
				return __builtin_ctz(v32) + prefixlen == l * CHAR_BIT;
			} else
			if (sizeof(unsigned long) == sizeof(uint32_t)) {
				prefixlen = __builtin_popcountl(v32);
				return __builtin_ctzl(v32) + prefixlen == l * CHAR_BIT;
			}
		}
#endif
		prefixlen = popcount(v, l);
		return ctz(v, l) + prefixlen == l * CHAR_BIT;
	}

}

namespace IPAddress {

	void
	SetPrefix (
		void * v,
		std::size_t l,
		unsigned prefixlen
	) {
		if (l == sizeof(uint32_t)) {
			uint32_t & v32(*reinterpret_cast<uint32_t *>(v));
			v32 = htobe32(-uint32_t(1) << (32U - prefixlen));
			return;
		}
		std::memset(v, '\0', l);
		unsigned char * c(reinterpret_cast<unsigned char *>(v));
		const unsigned char ones(-static_cast<unsigned char>(1));
		while (prefixlen >= CHAR_BIT) {
			*c++ = ones;
			prefixlen -= CHAR_BIT;
		}
		if (prefixlen > 0U)
			*c++ = ones << (CHAR_BIT - prefixlen);
	}

	bool
	IsPrefix (
		const sockaddr & addr,
		unsigned & prefixlen
	) {
		switch (addr.sa_family) {
			case AF_INET:
			{
				const struct sockaddr_in & addr4(*reinterpret_cast<const struct sockaddr_in *>(&addr));
				return RawIsPrefix(&addr4.sin_addr.s_addr, sizeof addr4.sin_addr.s_addr, prefixlen);
			}
			case AF_INET6:
			{
				const struct sockaddr_in6 & addr6(*reinterpret_cast<const struct sockaddr_in6 *>(&addr));
				return RawIsPrefix(&addr6.sin6_addr.s6_addr, sizeof addr6.sin6_addr.s6_addr, prefixlen);
			}
			default:
				return false;
		}
	}

	bool
	IsLinkLocal (
		const in6_addr & a
	) {
		return IN6_IS_ADDR_LINKLOCAL(&a);
	}

}

in6_addr
operator ~ (
	const in6_addr & r
) {
	in6_addr v(r);
	for (std::size_t i(0U); i < sizeof v.s6_addr; ++i)
		v.s6_addr[i] = ~v.s6_addr[i];
	return v;
}

in6_addr
operator & (
	const in6_addr & l,
	const in6_addr & r
) {
	in6_addr v;
	for (std::size_t i(0U); i < sizeof v.s6_addr; ++i)
		v.s6_addr[i] = l.s6_addr[i] & r.s6_addr[i];
	return v;
}

in6_addr
operator | (
	const in6_addr & l,
	const in6_addr & r
) {
	in6_addr v;
	for (std::size_t i(0U); i < sizeof v.s6_addr; ++i)
		v.s6_addr[i] = l.s6_addr[i] | r.s6_addr[i];
	return v;
}

in_addr
operator ~ (
	const in_addr & r
) {
	in_addr v(r);
	v.s_addr = ~v.s_addr;
	return v;
}

in_addr
operator & (
	const in_addr & l,
	const in_addr & r
) {
	in_addr v;
	v.s_addr = l.s_addr & r.s_addr;
	return v;
}

in_addr
operator | (
	const in_addr & l,
	const in_addr & r
) {
	in_addr v;
	v.s_addr = l.s_addr | r.s_addr;
	return v;
}
