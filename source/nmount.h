/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define MAKE_IOVEC(x) { const_cast<char *>(x), sizeof x }
#define ZERO_IOVEC() { 0, 0 }

#define FROM MAKE_IOVEC("from")
#define FSTYPE MAKE_IOVEC("fstype")
#define FSPATH MAKE_IOVEC("fspath")

// These platforms don't supply their own nmount().
#if defined(__LINUX__) || defined(__linux__) || defined(__OpenBSD__)
#include <sys/uio.h>

extern "C" int nmount (struct iovec * iov, unsigned int ioc, int flags) ;
#endif
