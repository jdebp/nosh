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
	typedef uint8_t attribute_type;

	CharacterCell(uint32_t c, attribute_type a, colour_type f, colour_type b) : character(c), attributes(a), foreground(f), background(b) {}
	CharacterCell() : character(), attributes(), foreground(), background() {}

	uint32_t character;
	attribute_type attributes;
	colour_type foreground, background;

	enum {
		BOLD = 1U << 0U,
		ITALIC = 1U << 1U,
		UNDERLINE = 1U << 2U,
		BLINK = 1U << 3U,
		INVERSE = 1U << 4U,
		STRIKETHROUGH = 1U << 5U,
		INVISIBLE = 1U << 6U,
		FAINT = 1U << 7U,
	};
};

#endif
