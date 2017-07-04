/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_HOME_DIR_H)
#define INCLUDE_HOME_DIR_H

#include <string>

struct ProcessEnvironment;

extern std::string effective_user_home_dir(const ProcessEnvironment &);

#endif
