/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if defined(__LINUX__) || defined(__linux__)
#include <linux/kd.h>
#else
#include <sys/kbio.h>
#endif
#include <sys/ioctl.h>
#include <unistd.h>
#include "KeyboardIO.h"
#include "ttyutils.h"

KeyboardIO::KeyboardIO(int fd) : 
	FileDescriptorOwner(fd),
	original_attr(),
	kbmode(K_XLATE)
{
}

KeyboardIO::~KeyboardIO()
{
	restore();
}

void
KeyboardIO::restore()
{
	if (-1 != fd) {
		tcsetattr_nointr(fd, TCSADRAIN, original_attr);
		ioctl(fd, KDSKBMODE, kbmode);
	}
}

void 
KeyboardIO::save_and_set_code_mode()
{
	if (-1 != fd) {
		ioctl(fd, KDGKBMODE, &kbmode);
#if defined(__LINUX__) || defined(__linux__)
		ioctl(fd, KDSKBMODE, K_MEDIUMRAW);
#else
		ioctl(fd, KDSKBMODE, K_CODE);
#endif
		if (0 <= tcgetattr_nointr(fd, original_attr))
			tcsetattr_nointr(fd, TCSADRAIN, make_raw(original_attr));
	}
}
