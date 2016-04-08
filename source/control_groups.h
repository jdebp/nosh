/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_CONTROL_GROUPS_H)
#define INCLUDE_CONTROL_GROUPS_H

#include <cstdio>
#include <string>

FILE *
open_my_control_group_info(
	const char * const name
) ;
bool
read_my_control_group (
	FILE * f,
	const char * const name,
	std::string & g
) ;

#endif
