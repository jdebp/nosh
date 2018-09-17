/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "nmount.h"

#if defined(__LINUX__) || defined(__linux__)

#include <unistd.h>
#include <sys/uio.h>
#include <sys/mount.h>
#include "utils.h"
#include <string>

extern "C"
int
nmount (
	struct iovec * iov,
	unsigned int ioc,
	int flags
) {
	std::string path, type, from, data;
	// NULL data is different from empty data in Linux mount() for at least the cgroup2 filesystem.
	bool hasdata(false);

	for (unsigned int u(0U); u + 1U < ioc; u += 2U) {
		const std::string var(convert(iov[u]));
		if ("from" == var)
			from = convert(iov[u + 1U]);
		else
		if ("fstype" == var)
			type = convert(iov[u + 1U]);
		else
		if ("fspath" == var)
			path = convert(iov[u + 1U]);
		else 
		{
			hasdata = true;
			if (!data.empty())
				data += ",";
			data += var;
			if (iov[u + 1U].iov_base && iov[u + 1U].iov_len)
				data += "=" + convert(iov[u + 1U]);
		}
	}

	return mount(from.c_str(), path.c_str(), type.c_str(), static_cast<unsigned long>(flags), hasdata ? data.c_str() : 0);
}

#elif defined(__OpenBSD__)

#include <cerrno>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/mount.h>
#include "utils.h"
#include <string>

extern "C"
int
nmount (
	struct iovec * iov,
	unsigned int ioc,
	int flags
) {
	std::string path, type, from, data;

	for (unsigned int u(0U); u + 1U < ioc; u += 2U) {
		const std::string var(convert(iov[u]));
		if ("from" == var)
			from = convert(iov[u + 1U]);
		else
		if ("fstype" == var)
			type = convert(iov[u + 1U]);
		else
		if ("fspath" == var)
			path = convert(iov[u + 1U]);
		else 
		{
			if (!data.empty())
				data += ",";
			data += var;
			if (iov[u + 1U].iov_base && iov[u + 1U].iov_len)
				data += "=" + convert(iov[u + 1U]);
		}
	}

	return errno = ENOSYS, -1;	/// FIXME \bug This should work out what to do and call the real mount().
}

#endif
