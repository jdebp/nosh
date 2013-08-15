/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <unistd.h>
#include <sys/uio.h>
#include "utils.h"
#include <string>

std::string
convert (
	const struct iovec & v
) {
	const char * b(static_cast<const char *>(v.iov_base));
	const bool nul(v.iov_len && !b[v.iov_len - 1]);
	return std::string(b, v.iov_len - (nul ? 1 : 0));
}

std::string
fspath_from_mount (
	struct iovec * iov,
	unsigned int ioc
) {
	for (unsigned int u(0U); u + 1U < ioc; u += 2U) {
		const std::string var(convert(iov[u]));
		if ("fspath" == var)
			return convert(iov[u + 1U]);
	}

	return std::string();
}
