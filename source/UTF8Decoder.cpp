/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <stdint.h>
#include "UTF8Decoder.h"

UTF8Decoder::UTF8Decoder(UCS32CharacterSink & s) :
	sink(s),
	expected_continuation_bytes(0U),
	assemblage(0U),
	minimum(0U)
{
}

inline
void 
UTF8Decoder::SendGood()
{
	sink.ProcessDecodedUTF8(assemblage, false, assemblage < minimum);
	minimum = 0U;
	expected_continuation_bytes = 0U;
}

inline
void 
UTF8Decoder::SendBad()
{
	assemblage = assemblage << (6U * expected_continuation_bytes);
	sink.ProcessDecodedUTF8(assemblage, true, assemblage < minimum);
	minimum = 0U;
	expected_continuation_bytes = 0U;
}

void 
UTF8Decoder::Process(uint_fast8_t c)
{
	if ((0x80 & c) == 0x00) {
		// 0x2x_0nnn_nnnn
		if (0 < expected_continuation_bytes)
			SendBad();
		assemblage = c;
		minimum = 0U;
		SendGood();
	} else
	if ((0xC0 & c) == 0x80) {
		// ... 0x2x_10nn_nnnn ...
		if (0 == expected_continuation_bytes) {
			assemblage = c;
			minimum = 0U;
			SendBad();
		} else {
			--expected_continuation_bytes;
			assemblage = (assemblage << 6U) | (c & 0x3F);
			if (0 == expected_continuation_bytes) 
				SendGood();
		}
	} else
	if ((0xE0 & c) == 0xC0) {
		// 0x2x_110n_nnnn 0x2x_10nn_nnnn
		if (0 < expected_continuation_bytes)
			SendBad();
		expected_continuation_bytes = 1U;
		assemblage = c & 0x1F;
		minimum = 0x00000080;	// Code points less than 0x2x_0000_0000_0000_0000_0000_0000_1000_0000 have a shorter encoding.
	} else
	if ((0xF0 & c) == 0xE0) {
		// 0x2x_1110_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn
		if (0 < expected_continuation_bytes)
			SendBad();
		expected_continuation_bytes = 2U;
		assemblage = c & 0x0F;
		minimum = 0x00000800;	// Code points less than 0x2x_0000_0000_0000_0000_0000_1000_0000_0000 have a shorter encoding.
	} else
	if ((0xF8 & c) == 0xF0) {
		// 0x2x_1111_0nnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn
		if (0 < expected_continuation_bytes)
			SendBad();
		expected_continuation_bytes = 3U;
		assemblage = c & 0x07;
		minimum = 0x00010000;	// Code points less than 0x2x_0000_0000_0000_0001_0000_0000_0000_0000 have a shorter encoding.
	} else
	if ((0xFC & c) == 0xF8) {
		// 0x2x_1111_10nn 0x2x_10nn_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn
		if (0 < expected_continuation_bytes)
			SendBad();
		expected_continuation_bytes = 4U;
		assemblage = c & 0x03;
		minimum = 0x00200000;	// Code points less than 0x2x_0000_0000_0010_0000_0000_0000_0000_0000 have a shorter encoding.
	} else
	if ((0xFE & c) == 0xFC) {
		// 0x2x_1111_110n 0x2x_10nn_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn
		if (0 < expected_continuation_bytes)
			SendBad();
		expected_continuation_bytes = 5U;
		assemblage = c & 0x01;
		minimum = 0x04000000;	// Code points less than 0x2x_0000_0100_0000_0000_0000_0000_0000_0000 have a shorter encoding.
	} else {
		// 0x2x_1111_111n
		if (0 < expected_continuation_bytes)
			SendBad();
		assemblage = c;
		minimum = 0U;
		SendBad();
	}
}
