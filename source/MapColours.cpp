/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <stdint.h>
#include "CharacterCell.h"

CharacterCell::colour_type
Map16Colour (
	uint8_t c
) {
	c %= 16U;
	if (7U == c) {
		// Dark white is brighter than bright black.
		return CharacterCell::colour_type(ALPHA_FOR_COLOURED,0xBF,0xBF,0xBF);
	} else if (4U == c) {
		// Everyone fusses about dark blue, and no choice is perfect.
		// This choice is Web Indigo.
		return CharacterCell::colour_type(ALPHA_FOR_COLOURED,0x4F,0x00,0x7F);
	} else {
		if (8U == c) c = 7U;	// Substitute original dark white for bright black, which otherwise would work out the same as dark black.
		const uint8_t h((c & 8U)? 255U : 127U), b(c & 4U), g(c & 2U), r(c & 1U);
		return CharacterCell::colour_type(ALPHA_FOR_COLOURED,r ? h : 0U,g ? h : 0U,b ? h : 0U);
	}
}

CharacterCell::colour_type
Map256Colour (
	uint8_t c
) {
	if (c < 16U) {
		return Map16Colour(c);
	} else if (c < 232U) {
		c -= 16U;
		uint8_t b(c % 6U), g((c / 6U) % 6U), r(c / 36U);
		if (r >= 4U) r += r - 3U;
		if (g >= 4U) g += g - 3U;
		if (b >= 4U) b += b - 3U;
		return CharacterCell::colour_type(ALPHA_FOR_COLOURED,r * 32U,g * 32U,b * 32U);
	} else {
		c -= 232U;
		if (c >= 16U) c += c - 15U;
		return CharacterCell::colour_type(ALPHA_FOR_COLOURED,c * 8U,c * 8U,c * 8U);
	}
}
