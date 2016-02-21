/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_HOST_ID_H)
#define INCLUDE_HOST_ID_H

#include <stdint.h>
#if !defined(__LINUX__) && !defined(__linux__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <netinet/in.h>
#include "uuid.h"
#else
#include "uuid/uuid.h"
#endif

uint32_t
calculate_host_id (
	const uuid_t & machine_id
) ;
void
write_non_volatile_hostid (
	const char * prog,
	uint32_t hostid
) ;
void
write_volatile_hostid (
	const char * prog,
	uint32_t hostid
) ;

#endif
