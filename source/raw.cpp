/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <termios.h>
#include "utils.h"

termios
make_raw (
	const termios & ti
) {
	termios t(ti);
	t.c_iflag &= ~(IGNPAR|IGNBRK|BRKINT|ICRNL|IGNCR|IXON|IXOFF|IMAXBEL|ISTRIP
#if defined(IUTF8)
			|IUTF8
#endif
			);
	t.c_oflag &= ~(OPOST|ONLCR);
	t.c_cflag &= ~(CSIZE|PARENB);
	t.c_lflag &= ~(ISIG|ICANON|IEXTEN|ECHO|ECHOE|ECHOK|ECHOCTL|ECHOKE|ECHONL);
	return t;
}
