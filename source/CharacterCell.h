/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_CHARACTERCELL_H)
#define INCLUDE_CHARACTERCELL_H

#include <stdint.h>

struct CursorSprite {
	typedef uint8_t attribute_type;
	enum {
		VISIBLE = 1U << 0U,
		BLINK = 1U << 1U,
	};
	enum glyph_type {
		UNDERLINE = 0U,
		BAR = 1U,
		BOX = 2U,
		BLOCK = 3U,
		STAR = 4U
	};
};
struct PointerSprite {
	typedef uint8_t attribute_type;
	enum {
		VISIBLE = 1U << 0U,
	};
};
struct CharacterCell {
	struct colour_type {
		colour_type(uint8_t a, uint8_t r, uint8_t g, uint8_t b) : alpha(a), red(r), green(g), blue(b) {}
		colour_type() : alpha(), red(), green(), blue() {}
		uint8_t alpha, red, green, blue;
	};
	typedef uint16_t attribute_type;

	CharacterCell(uint32_t c, attribute_type a, colour_type f, colour_type b) : character(c), attributes(a), foreground(f), background(b) {}
	CharacterCell() : character(), attributes(), foreground(), background() {}

	uint32_t character;
	attribute_type attributes;
	colour_type foreground, background;

	enum {
		BOLD = 1U << 0U,
		ITALIC = 1U << 1U,
		BLINK = 1U << 3U,
		INVERSE = 1U << 4U,
		STRIKETHROUGH = 1U << 5U,
		INVISIBLE = 1U << 6U,
		FAINT = 1U << 7U,
		UNDERLINES = 7U << 8U,
			SIMPLE_UNDERLINE = 1U << 8U,
			DOUBLE_UNDERLINE = 2U << 8U,
			CURLY_UNDERLINE = 3U << 8U,
			DOTTED_UNDERLINE = 4U << 8U,
			DASHED_UNDERLINE = 5U << 8U,
	};
};

inline
bool
operator != (
	const CharacterCell::colour_type & a,
	const CharacterCell::colour_type & b
) {
	return a.alpha != b.alpha || a.red != b.red || a.green != b.green || a.blue != b.blue;
}

extern
CharacterCell::colour_type
Map16Colour (
	uint8_t c
) ;
extern
CharacterCell::colour_type
Map256Colour (
	uint8_t c
) ;

enum {
	ALPHA_FOR_ERASED	= 0,
	ALPHA_FOR_DEFAULT	= 1,
	ALPHA_FOR_COLOURED	= 2,
};

#endif
