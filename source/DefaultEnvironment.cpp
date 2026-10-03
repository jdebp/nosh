/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <paths.h>
#include "DefaultEnvironment.h"

namespace DefaultEnvironment {

// Everyone gets the benefit of the NetBSD /usr/pkg convention.
//
// /usr/pkg is essentially the new /opt, without the company names.
// It is where pre-packaged stuff goes.
//
// Truly locally-added stuff, e.g. stuff compiled from ad hoc source, goes in /usr/local .

// _PATH_STDPATH is wrong for all of these.
//
// It is defined as the PATH that enables execvpe() to access all of the POSIX standard utilities.
// But this is never what we actually want.
//
// On Debian (for example) it omits all sbin directories and /usr/{local,pkg}/{sbin,bin}, but:
//   * we want /usr/{local,pkg}/{sbin,bin} for the system manager, the login subsystem, and the toolset fallback, because that is where the toolset might be installed; and
//   * system programs spawned from the service manager are often in sbin, because they are system-level but not standardized.
//
// On the systems that include /usr/local, it is placed last; meaning the locally-applied stuff is not picked over the stuff it is supposed to supersede.
// We give "local" programs priority over the operating system programs, as the XDG Desktop Specification does for other files.
//
// _PATH_SYSPATH is a FreeBSDism, non-existent on other systems.

// Split-/usr systems:
// We cannot omit /sbin and /bin from the path because we cannot reliably detect that they duplicate /usr/bin and /usr/sbin at this point.
// On some systems, /usr/sbin and /usr/bin are the symbolic links, and don't exist until we have mounted /usr .
// On other systems, /sbin and /bin are the symbolic links, but /usr isn't a mount point and everything is on the root volume.

namespace SystemManager {

extern const char PATH[] = "/usr/local/sbin:/usr/local/bin:/usr/pkg/sbin:/usr/pkg/bin:/usr/sbin:/usr/bin:/sbin:/bin";

#if defined(__OpenBSD__)
extern const char LANG[] = "C";
#else
// https://sourceware.org/glibc/wiki/Proposals/C.UTF-8
extern const char LANG[] = "C.UTF-8";
#endif

}

namespace UserLogin {

// In theory we could omit all of the sbins from this.
// In practice, FreeBSD and NetBSD put several non-superuser-runnable-but-system tools in the sbins.
extern const char PATH[] = "/usr/local/sbin:/usr/local/bin:/usr/pkg/sbin:/usr/pkg/bin:/usr/sbin:/usr/bin:/sbin:/bin";

extern const char TERMPATH[] = "/usr/local/etc/termcap:/etc/termcap:/usr/pkg/etc/termcap:/usr/share/misc/termcap";
extern const char TERMINFO_DIRS[] = "/usr/local/etc/terminfo:/etc/terminfo:/usr/pkg/etc/terminfo:/usr/share/misc/terminfo";

// TrueOS Desktop adds /share to the default search path.
// But it inverts this order, making "local" the lowest priority.
// We give "local" data files priority over the operating system data files, as the XDG Desktop Specification does.
extern const char XDG_DATA_DIRS[] = "/usr/local/share:/usr/pkg/share:/usr/share:/share";

// The default of just /etc/xdg is not enough for the BSDs.
extern const char XDG_CONFIG_DIRS[] = "/usr/local/etc/xdg:/etc/xdg:/usr/pkg/etc/xdg";

#if defined(__OpenBSD__)
extern const char LANG[] = "POSIX";
#else
// https://sourceware.org/glibc/wiki/Proposals/C.UTF-8
extern const char LANG[] = "C.UTF-8";
#endif
extern const char MM_CHARSET[] = "UTF-8";

// POSIX gives us two choices of default line editor; we do not dump ed on people.
extern const char EDITOR[] = "ex";

// POSIX gives us one choice of default visual editor.
extern const char VISUAL[] = "vi";

// pg has gone from the Single Unix Specification.
extern const char PAGER[] = "more";

extern const char SHELL[] = _PATH_BSHELL;

extern const char TZ[] = "UTC";

// This is Debian's generic 'alternatives' name.
extern const char WWWBROWSER[] = "www-browser";

extern const char MANPAGER[] = "more";

}

namespace Toolkit {

// This is the fallback within the chain-loading system if someone has zapped PATH from the environment.
// In theory we could get rid of whichever of /usr/local or /usr/pkg that the toolset is not installed in.
extern const char PATH[] = "/usr/local/sbin:/usr/local/bin:/usr/pkg/sbin:/usr/pkg/bin:/usr/sbin:/usr/bin:/sbin:/bin";

extern const char PAGER[] = "console-tty37-viewer";

extern const char XMLVIEWER[] = "console-docbook-xml-viewer";

extern const char MANPAGER[] = "console-tty37-viewer --discard-control-strings";

}

// Our default is to assume terminals are at least ECMA-48 from 1976, rather than TTY-37 from 1968.  Progress!
extern const char TERM[] = "ansi";

}
