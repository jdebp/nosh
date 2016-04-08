/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_MACHINE_ID_H)
#define INCLUDE_MACHINE_ID_H

#include <iostream>
#include <string>
#if !defined(__LINUX__) && !defined(__linux__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <netinet/in.h>
#include "uuid.h"
#else
#include "uuid/uuid.h"
#endif

namespace machine_id {

extern uuid_t the_machine_id;

void
erase() ;
void
create() ;
bool
is_null () ;
std::string
human_readable_form() ;
std::string
human_readable_form_compact () ;
bool
read_non_volatile () ;
void
write_non_volatile (
	const char * prog
) ;
bool
read_volatile () ;
void
write_volatile (
	const char * prog
) ;
bool
read_host_uuid () ;
void
write_host_uuid (
	const char * prog
) ;
void
write_non_volatile_hostuuid (
	const char * prog
) ;
void
write_volatile_hostuuid (
	const char * prog
) ;
bool 
validate () ;
bool
read_fallbacks () ;

}

#endif