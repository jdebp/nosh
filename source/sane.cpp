/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <unistd.h>
#include <termios.h>
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__) || defined(__linux__) || defined(__LINUX__)
#include <sys/ttydefaults.h>
#endif
#if defined(__linux__) || defined(__LINUX__)
#include <sys/ioctl.h>	// For struct winsize on Linux
#endif
#include "ttyutils.h"
#include "ControlCharacters.h"

namespace {

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__) || defined(__linux__) || defined(__LINUX__)
#if !defined(TTYDEF_LFLAG_NOECHO)
#define TTYDEF_LFLAG_NOECHO (TTYDEF_LFLAG&~(ECHO|ECHOE|ECHOKE|ECHOCTL))
#endif
#if !defined(TTYDEF_LFLAG_ECHO)
#define TTYDEF_LFLAG_ECHO (TTYDEF_LFLAG|(ECHO|ECHOE|ECHOKE|ECHOCTL))
#endif
#else
enum {
	CERASE		= DEL,	// ^?
	CKILL		= NAK,	// ^U
	CEOF		= EOT,	// ^D
	CINTR		= ETX,	// ^C
	CQUIT		= FS, 	// ^\ .
	CSTART		= DC1,	// ^Q
	CSTOP		= DC3,	// ^S
	CERASE2		= BS, 	// ^H
	CWERASE		= ETB,	// ^W
	CREPRINT	= DC2,	// ^R
	CSUSP		= SUB,	// ^Z
	CDSUSP		= EM, 	// ^Y
	CLNEXT		= SYN,	// ^V
	CDISCARD	= SI, 	// ^O
	CSTATUS		= DC4,	// ^T
	CMIN		= 1,
	CTIME		= 0,
};
enum {
	TTYDEF_IFLAG =	BRKINT|ICRNL|IMAXBEL|IXON|IXANY,
	TTYDEF_OFLAG =	OPOST|ONLCR,
	TTYDEF_LFLAG_NOECHO =	ICANON|ISIG|IEXTEN,
	TTYDEF_LFLAG_ECHO =	TTYDEF_LFLAG_NOECHO|ECHO|ECHOE|ECHOKE|ECHOCTL,
	TTYDEF_LFLAG =	TTYDEF_LFLAG_ECHO,
	TTYDEF_CFLAG =	CREAD|CS8|HUPCL,
	TTYDEF_SPEED =	B921600,
};
#endif

}

// Like the BSD cfmakesane() but available outwith BSD.
// This does not touch the hardware control flags or speeds.
termios
enable_canonical_software_processing (
	const termios & original
) {
	termios t(original);

	// Unlike "stty sane", we don't set ISTRIP; it's 1970s Think.
	t.c_iflag = (TTYDEF_IFLAG&~ISTRIP)|IGNPAR|IGNBRK|IXANY;
	t.c_oflag = TTYDEF_OFLAG;
	t.c_lflag = TTYDEF_LFLAG_ECHO|ECHOK;

#if defined(_POSIX_VDISABLE)
	// See IEEE 1003.1 Interpretation Request #27 for why _POSIX_VDISABLE is not usable as a preprocessor expression.
       	if (-1 != _POSIX_VDISABLE)
		for (unsigned i(0); i < sizeof t.c_cc/sizeof *t.c_cc; ++i) t.c_cc[i] = _POSIX_VDISABLE;
	else {
#endif
	const int pd(pathconf("/", _PC_VDISABLE));
	for (unsigned i(0); i < sizeof t.c_cc/sizeof *t.c_cc; ++i) t.c_cc[i] = pd;
#if defined(_POSIX_VDISABLE)
	}
#endif
	t.c_cc[VERASE] = CERASE;
	t.c_cc[VKILL] = CKILL;
	t.c_cc[VEOF] = CEOF;
	t.c_cc[VINTR] = CINTR;
	t.c_cc[VQUIT] = CQUIT;
	t.c_cc[VSTART] = CSTART;
	t.c_cc[VSTOP] = CSTOP;
	// We don't need to set these for canonical mode, but some programs set non-canonical mode without explicitly setting these as well.
	// So we set them to the defaults here, and a badly written program that only turns off ICANON will at least get the defaults.
	t.c_cc[VTIME] = CTIME;
	t.c_cc[VMIN] = CMIN;
#if defined(VERASE2)
	t.c_cc[VERASE2] = CERASE2;
#endif
#if defined(VEOL)
	t.c_cc[VEOL] = CEOL;
#endif
#if defined(VEOL2)
	// t.c_cc[VEOL2] = CEOL2;
#endif
#if defined(VSWTC)
	// t.c_cc[VSWTC] = CSWTC;
#endif
#if defined(VWERASE)
	t.c_cc[VWERASE] = CWERASE;
#endif
#if defined(VREPRINT)
	t.c_cc[VREPRINT] = CREPRINT;
#endif
#if defined(VSUSP)
	t.c_cc[VSUSP] = CSUSP;
#endif
#if defined(VDSUSP)
	t.c_cc[VDSUSP] = CDSUSP;
#endif
#if defined(VLNEXT)
	t.c_cc[VLNEXT] = CLNEXT;
#endif
#if defined(VDISCARD)
	t.c_cc[VDISCARD] = CDISCARD;
#endif
#if defined(VSTATUS)
	t.c_cc[VSTATUS] = CSTATUS;
#endif

	return t;
}

namespace {

inline
termios
enable_canonical_software_processing (
	const termios & original,
	bool no_tostop,
	bool no_utf_8
) {
	termios t(enable_canonical_software_processing(original));

#if defined(IUTF8)
	if (!no_utf_8)
		t.c_iflag |= IUTF8;
#else
	static_cast<void>(no_utf_8);	// Silences a compiler warning.
#endif
	if (!no_tostop)
		t.c_lflag |= TOSTOP;

	return t;
}

}

// Create a termios from scratch suitable for initializing a local virtual terminal.
// This initializes the hardware flags and speeds.
termios
make_default_local_virtual (
	bool no_tostop,
	bool no_utf_8
) {
	termios t = {};

	t.c_cflag = TTYDEF_CFLAG;
	t.c_cflag |= CLOCAL;		// Always local
	cfsetspeed(&t, TTYDEF_SPEED);	// We don't want to accidentally hang up the terminal by setting 0 BPS.

	return enable_canonical_software_processing(t, no_tostop, no_utf_8);
}

/// Ignore attempts to have a zero size terminal.
void
sane (
	struct winsize & size
) {
	if (0 == size.ws_row) size.ws_row = 24;
	if (0 == size.ws_col) size.ws_col = 80;
}
