/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <unistd.h>
#include <ttyent.h>
#include "terminal_database.h"

#if defined(TTY_ONIFCONSOLE)
static inline
bool
is_current_console (
	const struct ttyent & entry
) {
#if defined(__FreeBSD__) || defined(__DragonFly__)
	int oid[CTL_MAXNAME];
	std::size_t len(sizeof oid/sizeof *oid);
	const int r(sysctlnametomib("kern.console", oid, &len));
	if (0 > r) return false;
	std::size_t siz;
	const int s(sysctl(oid, len, 0, &siz, 0, 0));
	if (0 > s) return false;
	std::vector<char> buf(siz, char());
	const int t(sysctl(oid, len, buf.data(), &siz, 0, 0));
	if (0 > t) return false;
	const char * avail(std::memchr(buf.data(), '/', siz));
	if (!avail) return false;
	*avail++ = '\0';
	for (const char * p(buf.data()), * e(0); *p; p = e) {
		e = std::strchr(p, ',');
		if (e) *e++ = '\0'; else e = std::strchr(p, '\0');
		if (0 == std::strcmp(p, entry.ty_name)) return true;
	}
	return false;
#elif defined(__LINUX__) || defined(__linux__)
	return false;
#else
#error "Don't know how to check the current console name on your platform."
#endif
}
#endif

bool
is_on (
	const struct ttyent & entry
) {
	return (entry.ty_status & TTY_ON) 
#if defined(TTY_ONIFCONSOLE)
		|| ((entry.ty_status & TTY_ONIFCONSOLE) && is_current_console(entry))
#endif
	;
}
