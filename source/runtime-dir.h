/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_RUNTIME_DIR_H)
#define INCLUDE_RUNTIME_DIR_H

#include <string>

struct ProcessEnvironment;

extern std::string login_user_runtime_dir(const ProcessEnvironment &);
extern std::string effective_user_runtime_dir();
extern void get_user_dirs_for(const std::string &, std::string &, std::string &, std::string &, std::string &, std::string &);

#endif
