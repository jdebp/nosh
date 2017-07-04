/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_GRAPHICSINTERFACE_H)
#define INCLUDE_GRAPHICSINTERFACE_H

#include <cstddef>
#include <stdint.h>
#include "CharacterCell.h"

class GraphicsInterface {
protected:
	struct ScreenBitmap;
	struct GlyphBitmap;
public:
	typedef ScreenBitmap * ScreenBitmapHandle;
	typedef GlyphBitmap * GlyphBitmapHandle;

	GraphicsInterface(void * b, std::size_t z, unsigned short y, unsigned short x, unsigned short s, unsigned short d);
	~GraphicsInterface();

	ScreenBitmapHandle GetScreenBitmap() { return &screen; }

	void BitBLT(ScreenBitmapHandle, GlyphBitmapHandle, unsigned short y, unsigned short x, const CharacterCell::colour_type & foreground, const CharacterCell::colour_type & background);
	void BitBLTMask(ScreenBitmapHandle, GlyphBitmapHandle, GlyphBitmapHandle, unsigned short y, unsigned short x, const CharacterCell::colour_type foregrounds[2], const CharacterCell::colour_type backgrounds[2]);
	void BitBLTAlpha(ScreenBitmapHandle, GlyphBitmapHandle, unsigned short y, unsigned short x, const CharacterCell::colour_type & colour);

	void DeleteGlyphBitmap(GlyphBitmap * handle) { delete handle; }
	GlyphBitmap * MakeGlyphBitmap();

protected:
	struct ScreenBitmap {
		void * const base;
		const unsigned short yres, xres, stride, depth;
		ScreenBitmap(void * b, unsigned short y, unsigned short x, unsigned short s, unsigned short d) : base(b), yres(y), xres(x), stride(s), depth(d) {}
		void Plot (unsigned short y, unsigned short x, uint16_t bits, const CharacterCell::colour_type & foreground, const CharacterCell::colour_type & background);
		void Plot (unsigned short y, unsigned short x, uint16_t bits, uint16_t mask, const CharacterCell::colour_type foregrounds[2], const CharacterCell::colour_type backgrounds[2]);
		void AlphaBlend (unsigned short y, unsigned short x, uint16_t bits, const CharacterCell::colour_type & colour);
	};
	struct GlyphBitmap {
		uint16_t * base;
		GlyphBitmap(uint16_t * b) : base(b) {}
		~GlyphBitmap() { delete[] base; }
		void Plot (std::size_t row, uint16_t bits) { base[row] = bits; }
		uint16_t Row (std::size_t row) const { return base[row]; }
	};
	void * const base;
	const std::size_t size;
	ScreenBitmap screen;

};

#endif
