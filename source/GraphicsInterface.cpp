/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstring>
#include <stdint.h>
#include <sys/mman.h>
#include "CharacterCell.h"
#include "GraphicsInterface.h"

/* Pixels *******************************************************************
// **************************************************************************
*/

static inline
uint16_t
colour15 (const CharacterCell::colour_type & colour)
{
	return (colour.alpha ? 0x8000 : 0x0000) | (uint16_t(colour.red & 0xF8) << 7U) | (uint16_t(colour.green & 0xF8) << 3U) | (uint16_t(colour.blue & 0xF8) >> 3U);
}

static inline
uint16_t
colour16 (const CharacterCell::colour_type & colour)
{
	return (uint16_t(colour.red & 0xF8) << 8U) | (uint16_t(colour.green & 0xFC) << 4U) | (uint16_t(colour.blue & 0xF8) >> 3U);
}

static inline
void
colour24 (uint8_t b[3], const CharacterCell::colour_type & colour)
{
	b[2] = colour.red;
	b[1] = colour.green;
	b[0] = colour.blue;
}

static inline
uint32_t
colour32 (const CharacterCell::colour_type & colour)
{
	return (uint32_t(colour.red) << 16U) | (uint32_t(colour.green) << 8U) | (uint32_t(colour.blue) << 0U);
}

/* Bitmaps and blitting *****************************************************
// **************************************************************************
*/

void 
GraphicsInterface::ScreenBitmap::Plot (unsigned short y, unsigned short x, uint16_t bits, const CharacterCell::colour_type & foreground, const CharacterCell::colour_type & background)
{
	// The stride is in bytes, so this calculation is always in bytes and is common.
	void * const start(static_cast<uint8_t *>(base) + std::size_t(stride) * y);
	switch (depth) {
		case 15U:
		{
			uint16_t * const p(static_cast<uint16_t *>(start) + x);
			const uint16_t f(colour15(foreground)), b(colour15(background));
			for (unsigned off(16U); off-- > 0U; bits >>= 1U) {
				const bool bit(bits & 1U);
				p[off] = bit ? f : b;
			}
			break;
		}
		case 16U:
		{
			uint16_t * const p(static_cast<uint16_t *>(start) + x);
			const uint16_t f(colour16(foreground)), b(colour16(background));
			for (unsigned off(16U); off-- > 0U; bits >>= 1U) {
				const bool bit(bits & 1U);
				p[off] = bit ? f : b;
			}
			break;
		}
		case 24U:
		{
			uint8_t * const p(static_cast<uint8_t *>(start) + 3U * x);
			uint8_t f[3], b[3];
			colour24(f, foreground);
			colour24(b, background);
			for (unsigned off(16U); off-- > 0U; bits >>= 1U) {
				const bool bit(bits & 1U);
				std::memcpy(p + 3U * off, bit ? f : b, 3U);
			}
			break;
		}
		case 32U:
		{
			uint32_t * const p(static_cast<uint32_t *>(start) + x);
			const uint32_t f(colour32(foreground)), b(colour32(background));
			for (unsigned off(16U); off-- > 0U; bits >>= 1U) {
				const bool bit(bits & 1U);
				p[off] = bit ? f : b;
			}
			break;
		}
	}
}

GraphicsInterface::GraphicsInterface(
	void * b, 
	std::size_t z, 
	unsigned short y, 
	unsigned short x, 
	unsigned short s, 
	unsigned short d
) : 
	base(b), 
	size(z), 
	screen(b, y, x, s, d)
{
}

GraphicsInterface::~GraphicsInterface()
{
	munmap(base, size);
}

GraphicsInterface::GlyphBitmap * 
GraphicsInterface::MakeGlyphBitmap()
{
	uint16_t * b(new uint16_t [16U]);
	return new GlyphBitmap(b);
}

void 
GraphicsInterface::BitBLT(ScreenBitmapHandle s, GlyphBitmapHandle g, unsigned short y, unsigned short x, const CharacterCell::colour_type & foreground, const CharacterCell::colour_type & background)
{
	for (unsigned row(0U); row < 16U; ++row) {
		const uint16_t bits(g->Row(row));
		s->Plot(y + row, x, bits, foreground, background);
	}
}
