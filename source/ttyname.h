/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_TTYNAME_H)
#define INCLUDE_TTYNAME_H

#include <string>

struct ProcessEnvironment;

extern const char * get_controlling_tty_filename (const ProcessEnvironment &) ;	///< a filename that can be opened to get access to the controlling terminal
extern const char * get_line_name (const ProcessEnvironment &) ;	///< the controlling terminal name as presentable to humans
extern std::string id_field_from (const char * s) ;
extern unsigned long get_columns (const ProcessEnvironment &, int);

#endif
