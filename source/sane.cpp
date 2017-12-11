/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <unistd.h>
#include <termios.h>
#include "ttyutils.h"

// Like the BSD cfmakesane() but more flexible and available outwith BSD.
termios
sane (
	bool no_tostop,
	bool 
#if defined(IUTF8)
	no_utf_8
#endif
) {
	termios t;

	t.c_iflag = IGNPAR|IGNBRK|BRKINT|ICRNL|IXON|IMAXBEL;		// Unlike "stty sane", we don't set ISTRIP; it's 1970s Think.
#if defined(IUTF8)
	if (!no_utf_8) 
		t.c_iflag |= IUTF8;
#endif
	t.c_oflag = OPOST|ONLCR;
	t.c_cflag = CS8|CREAD|CLOCAL|HUPCL;
	t.c_lflag = ISIG|ICANON|IEXTEN|ECHO|ECHOE|ECHOK|ECHOCTL|ECHOKE;
	if (!no_tostop) 
		t.c_lflag |= TOSTOP;
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
	t.c_cc[VERASE] = '\x7F';	// DEL ^?
	t.c_cc[VKILL] = '\x15';		// SI  ^O
	t.c_cc[VEOF] = '\x04';		// EOT ^D
	t.c_cc[VINTR] = '\x03';		// ETX ^C
	t.c_cc[VQUIT] = '\x1C';		// FS  ^\ .
	t.c_cc[VSTART] = '\x11';	// DC1 ^Q
	t.c_cc[VSTOP] = '\x13';		// DC3 ^S
#if defined(VERASE2)
	t.c_cc[VERASE2] = '\x08';	// BS  ^H
#endif
#if defined(VWERASE)
	t.c_cc[VWERASE] = '\x17';	// ETB ^W
#endif
#if defined(VREPRINT)
	t.c_cc[VREPRINT] = '\x12';	// DC2 ^R
#endif
#if defined(VSUSP)
	t.c_cc[VSUSP] = '\x1A';		// SUB ^Z
#endif
#if defined(VDSUSP)
	t.c_cc[VDSUSP] = '\x19';	// EM  ^Y
#endif
#if defined(VLNEXT)
	t.c_cc[VLNEXT] = '\x16';	// SYN ^V
#endif
#if defined(VDISCARD)
	t.c_cc[VDISCARD] = '\x0F';	// SI  ^O
#endif
#if defined(VSTATUS)
	t.c_cc[VSTATUS] = '\x14';	// DC4 ^T
#endif
	cfsetspeed(&t, 38400);	// We don't want to accidentally hang up the terminal by setting 0 BPS.

	return t;
}
