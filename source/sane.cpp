/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <unistd.h>
#include <termios.h>
#include "utils.h"

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
	for (unsigned i(0); i < sizeof t.c_cc/sizeof *t.c_cc; ++i) t.c_cc[i] = '\0';
#else
	const int pd(pathconf("/", _PC_VDISABLE));
	for (unsigned i(0); i < sizeof t.c_cc/sizeof *t.c_cc; ++i) t.c_cc[i] = pd;
#endif
	t.c_cc[VERASE] = '\010';
	t.c_cc[VKILL] = '\025';
	t.c_cc[VEOF] = '\004';
	t.c_cc[VINTR] = '\003';
	t.c_cc[VQUIT] = '\034';
	t.c_cc[VSTART] = '\021';
	t.c_cc[VSTOP] = '\023';
#if defined(VWERASE)
	t.c_cc[VWERASE] = '\027';
#endif
#if defined(VREPRINT)
	t.c_cc[VREPRINT] = '\022';
#endif
#if defined(VSUSP)
	t.c_cc[VSUSP] = '\032';
#endif
#if defined(VDSUSP)
	t.c_cc[VDSUSP] = '\032';
#endif
#if defined(VLNEXT)
	t.c_cc[VLNEXT] = '\026';
#endif
#if defined(VDISCARD)
	t.c_cc[VDISCARD] = '\017';
#endif
#if defined(VSTATUS)
	t.c_cc[VSTATUS] = '\024';
#endif
	cfsetspeed(&t, 38400);	// We don't want to accidentally hang up the terminal by setting 0 BPS.

	return t;
}
