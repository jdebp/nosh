/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define MAKE_IOVEC(x) { const_cast<char *>(x), sizeof x }
#define ZERO_IOVEC() { nullptr, 0 }

#define FROM MAKE_IOVEC("from")
#define FSTYPE MAKE_IOVEC("fstype")
#define FSPATH MAKE_IOVEC("fspath")

#include "config/hasnmount.h"

// Some platforms don't supply their own nmount().
#if !defined(HAS_NMOUNT)
#include <sys/uio.h>

extern "C" int nmount (struct iovec * iov, unsigned int ioc, int flags) ;
#endif
