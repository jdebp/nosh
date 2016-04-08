/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstdio>
#include <cstring>
#include <fstream>
#include "host_id.h"
#include "CubeHash.h"

/* library functions for accessing an outdated ID ***************************
// **************************************************************************
*/

#if defined(__LINUX__) || defined(__linux__)
static const char volatile_hostid_filename[] = "/run/hostid";
static const char non_volatile_hostid_filename[] = "/etc/hostid";
#endif

uint32_t
calculate_host_id (
	const uuid_t & machine_id
) {
	// This is Dan Bernstein's SHA-3-AHS256 proposal from 2010-11.
	CubeHash h(16U, 16U, 32U, 32U, 256U);
	const unsigned char * m(reinterpret_cast<const unsigned char *>(&machine_id));
	h.Update(m, sizeof machine_id);
	h.Final();
	uint32_t hostid(0U);
	for ( std::size_t j(0);j < 4; ++j)
		hostid = (hostid << 8) | h.hashval[j];
	return hostid;
}

void
write_non_volatile_hostid (
	const char * prog,
	uint32_t hostid
) {
#if defined(__LINUX__) || defined(__linux__)
	std::ofstream s(non_volatile_hostid_filename);
	if (!s.fail()) {
		s.write(reinterpret_cast<const char *>(&hostid), sizeof hostid);
		return;
	}
	if ((EROFS != errno)
#if defined(MS_BIND)
		|| (0 > mount(volatile_hostid_filename, non_volatile_hostid_filename, "bind", MS_BIND, 0))
#endif
	   ) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, non_volatile_hostid_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
#else
	static_cast<void>(prog);	// Silences a compiler warning.
	static_cast<void>(hostid);	// Silences a compiler warning.
#endif
}

void
write_volatile_hostid (
	const char * prog,
	uint32_t hostid
) {
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	int oid[2] = {CTL_KERN, KERN_HOSTID};
#if !defined(__OpenBSD__)
	const 
#endif
		unsigned long h(hostid);
	if (0 > sysctl(oid, sizeof oid/sizeof *oid, 0, 0, &h, sizeof h)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kern.hostid", std::strerror(error));
	}
#elif defined(__LINUX__) || defined(__linux__)
	std::ofstream s(volatile_hostid_filename);
	if (!s.fail()) {
		s.write(reinterpret_cast<const char *>(&hostid), sizeof hostid);
		return;
	}
	const int error(errno);
	std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, non_volatile_hostid_filename, std::strerror(error));
	throw EXIT_FAILURE;
#else
#error "Don't know how to manipulate host ID on your platform."
#endif
}
