/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_DEFAULT_ENVIRONMENT_H)
#define INCLUDE_DEFAULT_ENVIRONMENT_H

namespace DefaultEnvironment {

/// Default environment variable values set by the system manager (and thus the service manager).
namespace SystemManager {

extern const char PATH[], LANG[];

}

/// Default environment variable values set by the login subsystem.
namespace UserLogin {

extern const char PATH[], TERMPATH[], TERMINFO_DIRS[], XDG_DATA_DIRS[], XDG_CONFIG_DIRS[], LANG[], MM_CHARSET[], EDITOR[], VISUAL[], PAGER[], SHELL[], TZ[], WWWBROWSER[], MANPAGER[];

}

/// Default environment variable values specific to the toolkit.
namespace Toolkit {

extern const char PATH[], PAGER[], XMLVIEWER[], MANPAGER[];

}

extern const char TERM[];

}

#endif
