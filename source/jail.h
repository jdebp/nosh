/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_JAIL_H)
#define INCLUDE_JAIL_H

struct ProcessEnvironment;

extern
bool 
am_in_jail(const ProcessEnvironment & envs);
extern
bool 
set_dynamic_hostname_is_allowed();

#endif
