/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <string>
#include "utils.h"

extern
bool
is_bool_true (
	const std::string & r
) {
	return "yes" == r || "on" == r || "true" == r;
}

extern
bool
is_bool_false (
	const std::string & r
) {
	return "no" == r || "off" == r || "false" == r;
}
