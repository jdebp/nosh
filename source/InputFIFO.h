/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_INPUTFIFO_H)
#define INCLUDE_INPUTFIFO_H

#include <cstddef>
#include <stdint.h>
#include "FileDescriptorOwner.h"

class InputFIFO :
	public FileDescriptorOwner
{
public:
	InputFIFO(int);
	void ReadInput();
	bool HasMessage() const { return input_read >= sizeof(uint32_t); }
	uint32_t PullMessage();
protected:
	char input_buffer[4];
	std::size_t input_read;
};

#endif
