/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if defined(__LINUX__) || defined(__linux__)
#include <unistd.h>
#include <sys/uio.h>
#include <sys/mount.h>
#include "utils.h"
#include "nmount.h"
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
				data = data + ",";
			data = data + var;
			if (iov[u + 1U].iov_base && iov[u + 1U].iov_len)
				data = data + "=" + convert(iov[u + 1U]);
		}
	}

	return mount(from.c_str(), path.c_str(), type.c_str(), static_cast<unsigned long>(flags), data.c_str());
}

#endif
