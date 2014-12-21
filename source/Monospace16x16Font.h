/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_MONOSPACE16X16FONT_H)
#define INCLUDE_MONOSPACE16X16FONT_H

#include <stdint.h>

/// An abstract provider of font glyphs for a monospace 16 by 16 font
class Monospace16x16Font {
public:
	virtual const uint16_t * ReadGlyph (uint32_t character, bool bold, bool faint, bool italic) = 0;
protected:
	~Monospace16x16Font() {}
};

#endif
