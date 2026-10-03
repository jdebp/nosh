/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <list>
#include <stdint.h>
#include <cstddef>
#include <sys/mman.h>
#if defined(__LINUX__) || defined(__linux__)
#include <endian.h>
#else
#include <sys/endian.h>
#endif
#include "Monospace16x16Font.h"
#include "CompositeFont.h"
#include "UnicodeClassification.h"
#include "vtfont.h"

namespace {

inline
uint_fast16_t
Expand8To16 (
	const uint_fast16_t c
) {
	uint_fast16_t r(0U), m(0x8000), s(0xC000);
	for (unsigned n(0U); n < 8U; ++n) {
		if (c & m) r |= s;
		s >>= 2U;
		m >>= 1U;
	}
	return r;
}

}

/* The abstract base font class and Unicode map handling ********************
// **************************************************************************
*/

namespace {

inline
CombinedFont::Font::UnicodeMap::const_iterator
find (
	const CombinedFont::Font::UnicodeMap & unicode_map,
	uint32_t character
) {
	const CombinedFont::Font::UnicodeMapEntry one = { character, 0U, 1U };
	const CombinedFont::Font::UnicodeMap::const_iterator e(unicode_map.end());
	CombinedFont::Font::UnicodeMap::const_iterator p(std::lower_bound(unicode_map.begin(), e, one));
	if (p < e && !p->Contains(character)) p = e;
	return p;
}

}

bool
CombinedFont::Font::UnicodeMapEntry::operator < (
	const CombinedFont::Font::UnicodeMapEntry & b
) const {
	return codepoint + count <= b.codepoint;
}

inline
bool
CombinedFont::Font::UnicodeMapEntry::Contains (
	uint32_t character
) const {
	return codepoint <= character && character < codepoint + count;
}

void
CombinedFont::Font::AddMapping(CombinedFont::Font::UnicodeMap & unicode_map, uint32_t character, std::size_t glyph_number, std::size_t count)
{
	const UnicodeMapEntry map_entry = { character, glyph_number, count };
	unicode_map.push_back(map_entry);
}

CombinedFont::Font::~Font ()
{
}

CombinedFont::MemoryMappedFont::~MemoryMappedFont ()
{
	munmap(const_cast<void*>(real_base), size);
}

/* Raw file format fonts ****************************************************
// **************************************************************************
*/

bool
CombinedFont::RawFileFont::Read(uint32_t character, Weight wt, uint16_t b[16], unsigned short & h, unsigned short & w)
{
	if (weight != wt) return false;
	UnicodeMap::const_iterator map_entry(find(unicode_map, character));
	if (unicode_map.end() == map_entry) return false;
	const std::size_t g(character - map_entry->codepoint + map_entry->glyph_number);
	const void * start(static_cast<const char *>(base) + offset);
	if (width > 8U) {
		const uint16_t *glyph(static_cast<const uint16_t (*)[16]>(start)[g]);
		for (unsigned row(0U); row < height && row < 16U; ++row) b[row] = glyph[row];
	} else {
		const uint8_t *glyph(static_cast<const uint8_t (*)[16]>(start)[g]);
		for (unsigned row(0U); row < height && row < 16U; ++row) b[row] = static_cast<uint16_t>(glyph[row]) << 8U;
	}
	w = width;
	h = height;
	return true;
}

/* VT file format files *****************************************************
// **************************************************************************
*/

bool
CombinedFont::VTFileFont::CheckWeight(Weight wt, std::size_t & vtfont_index)
{
	switch (wt) {
		case LIGHT:
			if (!faint) return false;
			vtfont_index = 0U;
			return true;
		case MEDIUM:
			if (faint) return false;
			vtfont_index = 0U;
			return true;
		case DEMIBOLD:
			if (!faint) return false;
			vtfont_index = 2U;
			return true;
		case BOLD:
			if (faint) return false;
			vtfont_index = 2U;
			return true;
		default:
			return false;
	}
}

bool
CombinedFont::LeftVTFileFont::Read(uint32_t character, Weight wt, uint16_t b[16], unsigned short & h, unsigned short & w)
{
	if (16U < width) return false;
	std::size_t vtfont_index;
	if (!CheckWeight(wt, vtfont_index)) return false;
	const void * const glyph_start(reinterpret_cast<const uint8_t *>(static_cast<const unsigned char *>(base) + offset));
	for (;; vtfont_index -= 2U) {
		const UnicodeMap & unicode_map(unicode_maps[vtfont_index]);
		const UnicodeMap::const_iterator map_entry(find(unicode_map, character));
		if (unicode_map.end() != map_entry) {
			const std::size_t g(character - map_entry->codepoint + map_entry->glyph_number);
			unsigned short h0(height);
			if (width <= 8U) {
				const uint8_t * glyph(reinterpret_cast<const uint8_t *>(glyph_start) + g * height);
				while (16U < h0 && 0U == glyph[0]) { ++glyph; --h0; }
				for (unsigned short row(0U); row < h0 && row < 16U; ++row) b[row] = static_cast<uint16_t>(glyph[row]) << 8U;
			} else {
				const uint16_t * glyph(reinterpret_cast<const uint16_t *>(glyph_start) + g * height);
				while (16U < h0 && 0U == glyph[0]) { ++glyph; --h0; }
				for (unsigned short row(0U); row < h0 && row < 16U; ++row) b[row] = be16toh(glyph[row]);
			}
			w = width;
			h = h0 > 16U ? 16U : h0;
			return true;
		}
		if (0U == vtfont_index) return false;
	}
}

bool
CombinedFont::LeftRightVTFileFont::Read(uint32_t character, Weight wt, uint16_t b[16], unsigned short & h, unsigned short & w)
{
	if (16U < width) return false;
	std::size_t vtfont_index;
	if (!CheckWeight(wt, vtfont_index)) return false;
	const void * const glyph_start(reinterpret_cast<const uint8_t *>(static_cast<const unsigned char *>(base) + offset));
	for (std::size_t left_vtfont_index(vtfont_index); ; left_vtfont_index -= 2U) {
		const UnicodeMap & unicode_map(unicode_maps[left_vtfont_index]);
		const UnicodeMap::const_iterator map_entry(find(unicode_map, character));
		if (unicode_map.end() != map_entry) {
			const std::size_t g(character - map_entry->codepoint + map_entry->glyph_number);
			if (width <= 8U) {
				const uint8_t * glyph(reinterpret_cast<const uint8_t *>(glyph_start) + g * height);
				for (unsigned short row(0U); row < height && row < 16U; ++row) b[row] = static_cast<uint16_t>(glyph[row]) << 8U;
			} else {
				const uint16_t * glyph(reinterpret_cast<const uint16_t *>(glyph_start) + g * height);
				for (unsigned short row(0U); row < height && row < 16U; ++row) b[row] = be16toh(glyph[row]);
			}
			break;
		}
		if (0U == left_vtfont_index) return false;
	}
	w = width;
	if (width <= 8U) {
		for (std::size_t right_vtfont_index(vtfont_index); ; right_vtfont_index -= 2U) {
			const UnicodeMap & unicode_map(unicode_maps[right_vtfont_index + 1U]);
			const UnicodeMap::const_iterator map_entry(find(unicode_map, character));
			if (unicode_map.end() != map_entry) {
				const std::size_t g(character - map_entry->codepoint + map_entry->glyph_number);
				const uint8_t * glyph(reinterpret_cast<const uint8_t *>(glyph_start) + g * height);
				for (unsigned short row(0U); row < height && row < 16U; ++row) b[row] = (b[row] & (~0x00FF << (8U - width))) | (static_cast<uint16_t>(glyph[row]) << (8U - width));
				w = width * 2U;
				break;
			}
			if (0U == right_vtfont_index) break;
		}
	}
	h = height > 16U ? 16U : height;
	return true;
}

/* The combined font class **************************************************
// **************************************************************************
*/

CombinedFont::~CombinedFont()
{
	for (FontList::iterator i(fonts.begin()); i != fonts.end(); i = fonts.erase(i))
		delete *i;
}

CombinedFont::RawFileFont *
CombinedFont::AddRawFileFont(CombinedFont::Font::Weight w, CombinedFont::Font::Slant s, unsigned short y, unsigned short x, const void * b, std::size_t z, std::size_t o)
{
	RawFileFont * f(new RawFileFont(w, s, y, x, b, z, o));
	if (f) fonts.push_back(f);
	return f;
}

CombinedFont::LeftVTFileFont *
CombinedFont::AddLeftVTFileFont(bool faint, Font::Slant s, unsigned short y, unsigned short x, const void * b, const void * rb, std::size_t z, std::size_t o)
{
	LeftVTFileFont * f(new LeftVTFileFont(faint, s, y, x, b, rb, z, o));
	if (f) fonts.push_back(f);
	return f;
}

CombinedFont::LeftRightVTFileFont *
CombinedFont::AddLeftRightVTFileFont(bool faint, Font::Slant s, unsigned short y, unsigned short x, const void * b, const void * rb, std::size_t z, std::size_t o)
{
	LeftRightVTFileFont * f(new LeftRightVTFileFont(faint, s, y, x, b, rb, z, o));
	if (f) fonts.push_back(f);
	return f;
}

const uint16_t *
CombinedFont::ReadGlyph (
	CombinedFont::Font & font,
	uint32_t character,
	CombinedFont::Font::Weight w,
	bool synthesize_bold,		///< never called with a bold weight: synthesize bold from non-bold
	bool synthesize_oblique		///< never called with an oblique slant: synthesize oblique from non-oblique
) {
	unsigned short width, height;
	if (!font.Read(character, w, synthetic, height, width)) return nullptr;
	const unsigned short y_slack(16U - height);
	if (y_slack) {
		if (8U <= y_slack) {
			for (unsigned row(height); row--; ) synthetic[row * 2U] = synthetic[row * 2U + 1U] = synthetic[row];
			for (unsigned row(height * 2U); row < 16U; ++row) synthetic[row] = 0U;
		} else
			for (unsigned row(height); row < 16U; ++row) synthetic[row] = 0U;
	}
	if (unsigned short x_slack = 16U - width) {
		if (synthesize_oblique) {
			for (unsigned row(0U); row < 16U; ++row) synthetic[row] >>= (((16U - row) * x_slack) / 16U);
			x_slack = 0U;
		}
		// Square quarter-sized (or smaller) glyphs will have already been doubled in height, and are now doubled in width.
		// Half-width (or smaller) rectangular glyphs for box drawing and block graphic characters can also be doubled in width.
		if (8U <= x_slack && (8U <= y_slack || UnicodeCategorization::IsDrawing(character))) {
			for (unsigned row(0U); row < 16U; ++row) synthetic[row] = Expand8To16(synthetic[row]);
			x_slack = 0U;
		}
		// Half-width or larger rectangular glyphs for horizontally repeatable characters are repeated horizontally to the right.
		// Non-horizontally-repeatable or smaller than half-width characters are centred.
		if (8U >= x_slack && UnicodeCategorization::IsHorizontallyRepeatable(character)) {
			const uint_fast16_t mask((1U << x_slack) - 1U);
			for (unsigned row(0U); row < 16U; ++row) synthetic[row] |= (synthetic[row] >> 8U) & mask;
			x_slack = 0U;
		}
		if (x_slack)
			for (unsigned row(0U); row < 16U; ++row) synthetic[row] >>= (x_slack / 2U);
	}
	if (synthesize_bold)
		for (unsigned row(0U); row < 16U; ++row) synthetic[row] |= synthetic[row] >> 1U;
	return synthetic;
}

inline
const uint16_t *
CombinedFont::ReadGlyph (
	uint32_t character,
	CombinedFont::Font::Weight w,
	CombinedFont::Font::Slant s,
	bool synthesize_bold,
	bool synthesize_oblique
) {
	for (FontList::iterator fontit(fonts.begin()); fontit != fonts.end(); ++fontit) {
		Font * font(*fontit);
		if (s != font->query_slant()) continue;
		if (const uint16_t * r = ReadGlyph(*font, character, w, synthesize_bold, synthesize_oblique))
			return r;
	}
	return nullptr;
}

const uint16_t *
CombinedFont::ReadGlyph (uint32_t character, bool bold, bool faint, bool italic)
{
	if (faint) {
		if (bold) {
			if (italic) {
				if (const uint16_t * const f = ReadGlyph(character, Font::DEMIBOLD, Font::ITALIC, false, false))
					return f;
				if (const uint16_t * const f = ReadGlyph(character, Font::DEMIBOLD, Font::OBLIQUE, false, false))
					return f;
			}
			if (const uint16_t * const f = ReadGlyph(character, Font::DEMIBOLD, Font::UPRIGHT, false, italic))
				return f;
		}
		if (italic) {
			if (const uint16_t * const f = ReadGlyph(character, Font::LIGHT, Font::ITALIC, bold, false))
				return f;
			if (const uint16_t * const f = ReadGlyph(character, Font::LIGHT, Font::OBLIQUE, bold, false))
				return f;
		}
		if (const uint16_t * const f = ReadGlyph(character, Font::LIGHT, Font::UPRIGHT, bold, italic))
			return f;
	} else
	{
		if (bold) {
			if (italic) {
				if (const uint16_t * const f = ReadGlyph(character, Font::BOLD, Font::ITALIC, false, false))
					return f;
				if (const uint16_t * const f = ReadGlyph(character, Font::BOLD, Font::OBLIQUE, false, false))
					return f;
			}
			if (const uint16_t * const f = ReadGlyph(character, Font::BOLD, Font::UPRIGHT, false, italic))
				return f;
		}
		if (italic) {
			if (const uint16_t * const f = ReadGlyph(character, Font::MEDIUM, Font::ITALIC, bold, false))
				return f;
			if (const uint16_t * const f = ReadGlyph(character, Font::MEDIUM, Font::OBLIQUE, bold, false))
				return f;
		}
		if (const uint16_t * const f = ReadGlyph(character, Font::MEDIUM, Font::UPRIGHT, bold, italic))
			return f;
	}
	return nullptr;
}
