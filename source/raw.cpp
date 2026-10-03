/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <termios.h>
#include "ttyutils.h"

// Turn off all software processing in the line discipline to make it suitable for a full-screen TUI.
// Like the BSD cfmakeraw().
// This does not touch the hardware control flags or speeds.
// It does not affect the tostop setting, either.
termios
disable_canonical_software_processing (
	const termios & ti
) {
	termios t(ti);
	t.c_iflag &= ~(IGNPAR|IGNBRK|BRKINT|ICRNL|IGNCR|IXON|IXOFF|IMAXBEL|ISTRIP
#if defined(IUTF8)
			|IUTF8
#endif
			);
	t.c_oflag &= ~(OPOST|ONLCR|OCRNL
#if defined(ONOEOT)
			|ONOEOT
#endif
			|ONOCR|ONLRET);
	t.c_lflag &= ~(ISIG|ICANON|IEXTEN|ECHO|ECHOE|ECHOK|ECHOCTL|ECHOKE|ECHONL);
	// On local terminals, allow for reading an entire DECFNK sequence in one gulp with a 0.1 second timeout.
	// On non-local terminals, return characters one by one immediately that they are available.
	t.c_cc[VMIN] = (t.c_cflag & CLOCAL) ? 5 : 1;
	t.c_cc[VTIME] = (t.c_cflag & CLOCAL) ? 1 : 0;
	return t;
}

termios
disable_tostop (
	const termios & ti
) {
	termios t(ti);
	t.c_lflag &= ~TOSTOP;
	return t;
}
