/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <stdint.h>
#endif
#include <cstdlib>
#include "jail.h"
#include "ProcessEnvironment.h"

#if defined(__FreeBSD__) || defined(__DragonFly__)
static inline
int			/// \returns standard syscall style return value \retval negative error \retval 0 false \retval positive true
am_in_bsd_jail ()
{
	int oid[CTL_MAXNAME];
	std::size_t len = sizeof oid/sizeof *oid;
	const int r(sysctlnametomib("security.jail.jailed", oid, &len));
	if (0 > r) return r;
	unsigned char buf[sizeof(uint32_t)];
	std::size_t siz(sizeof buf);
	const int s(sysctl(oid, len, buf, &siz, 0, 0));
	if (0 > s) return s;
	return *reinterpret_cast<const uint32_t *>(buf);
}
#endif

extern
bool 
am_in_jail(const ProcessEnvironment & envs) 
{
#if defined(__FreeBSD__) || defined(__DragonFly__)
	const int r(am_in_bsd_jail());
	if (r >= 0) return r > 0;
#endif
	// Other ways of detecting containers:
	//	* existence of /proc/vz and non-existence of /proc/bc on OpenVZ

	// systemd protocol for containers
	return !!envs.query("container");
}
