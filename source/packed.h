/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_PACKED_H)
#define INCLUDE_PACKED_H

#if defined(__LINUX__) || defined(__linux__)
#define __packed
#else
#include <sys/cdefs.h>
#endif

#endif
