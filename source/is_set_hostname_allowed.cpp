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
#include <cstddef>
#include "jail.h"

#if defined(__FreeBSD__) || defined(__DragonFly__)
int			/// \returns standard syscall style return value \retval negative error \retval 0 false \retval positive true
bsd_set_dynamic_hostname_is_allowed ()
{
	int oid[CTL_MAXNAME];
	std::size_t len = sizeof oid/sizeof *oid;
	const int r(sysctlnametomib("security.jail.set_hostname_allowed", oid, &len));
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
set_dynamic_hostname_is_allowed() 
{
#if defined(__FreeBSD__) || defined(__DragonFly__)
	const int r(bsd_set_dynamic_hostname_is_allowed());
	if (r >= 0) return r > 0;
#endif
	return false;
}
