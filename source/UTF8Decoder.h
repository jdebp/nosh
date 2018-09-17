/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_UTF8DECODER_H)
#define INCLUDE_UTF8DECODER_H

#include <stdint.h>

class UTF8Decoder 
{
public:
	class UCS32CharacterSink
	{
	public:
		virtual void ProcessDecodedUTF8(uint32_t character, bool decoder_error, bool overlong) = 0;
	};
	UTF8Decoder(UCS32CharacterSink &);
	void Process(uint_fast8_t);
protected:
	UCS32CharacterSink & sink;
	unsigned short expected_continuation_bytes;
	uint_fast32_t assemblage, minimum;
	void SendGood();
	void SendBad();
};

#endif
