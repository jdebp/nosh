/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_KEYBOARDIO_H)
#define INCLUDE_KEYBOARDIO_H

#include "FileDescriptorOwner.h"
#include <termios.h>

/// A wrapper class for devices that provide the kbio protocol.
class KeyboardIO :
	public FileDescriptorOwner
{
public:
	KeyboardIO(int fd);
	~KeyboardIO();
	void save_and_set_code_mode();
	void restore();
	void set_LEDs(bool c, bool n, bool s);
protected:
	termios original_attr;
	long kbmode;
};

#endif
