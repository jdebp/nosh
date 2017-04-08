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

static inline
uint_fast16_t
Expand8To16 (
	uint_fast16_t c
) {
	uint_fast16_t r(0U);
	for (unsigned n(0U); n < 8U; ++n) {
		r <<= 2U;
		if (c & 0x8000) r |= 0x0003;
		c <<= 1U;
	}
	return r;
}

static inline
CombinedFont::Font::UnicodeMap::const_iterator
find (
	const CombinedFont::Font::UnicodeMap & unicode_map,
	uint32_t character
) {
	const CombinedFont::Font::UnicodeMapEntry one = { character, 0U, 1U };
	CombinedFont::Font::UnicodeMap::const_iterator p(std::lower_bound(unicode_map.begin(), unicode_map.end(), one));
	if (p < unicode_map.end() && !p->Contains(character)) p = unicode_map.end();
	return p;
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

CombinedFont::Font::~Font ()
{
}

bool 
CombinedFont::MemoryFont::Read(uint32_t character, uint16_t b[16], unsigned short & h, unsigned short & w)
{
	UnicodeMap::const_iterator map_entry(find(unicode_map, character));
	if (unicode_map.end() == map_entry) return false;
	const std::size_t g(character - map_entry->codepoint + map_entry->glyph_number);
	const void * start(static_cast<const char *>(base) + offset);
	if (width > 8U) {
		const uint16_t *glyph(static_cast<const uint16_t (*)[16]>(start)[g]);
		for (unsigned row(0U); row < height; ++row) b[row] = glyph[row];
	} else {
		const uint8_t *glyph(static_cast<const uint8_t (*)[16]>(start)[g]);
		for (unsigned row(0U); row < height; ++row) b[row] = static_cast<uint16_t>(glyph[row]) << 8U;
	}
	w = width;
	h = height;
	return true;
}

CombinedFont::MemoryMappedFont::~MemoryMappedFont ()
{
	munmap(base, size);
}

CombinedFont::FileFont::FileFont(
	int f, 
	CombinedFont::Font::Weight w, 
	CombinedFont::Font::Slant s, 
	unsigned short y, 
	unsigned short x
) : 
	Font(w, s), 
	FileDescriptorOwner(f), 
	height(y), 
	width(x) 
{
}

CombinedFont::LeftFileFont::LeftFileFont(
	int f, 
	CombinedFont::Font::Weight w, 
	CombinedFont::Font::Slant s, 
	unsigned short y, 
	unsigned short x
) : 
	FileFont(f, w, s, y, x)
{
}

CombinedFont::LeftRightFileFont::LeftRightFileFont(
	int f, 
	CombinedFont::Font::Weight w, 
	CombinedFont::Font::Slant s, 
	unsigned short y, 
	unsigned short x
) : 
	FileFont(f, w, s, y, x)
{
}

CombinedFont::FileFont::~FileFont()
{
}

off_t 
CombinedFont::FileFont::GlyphOffset (std::size_t g) 
{
	return sizeof (bsd_vtfont_header) + query_cell_size() * g;
}

bool 
CombinedFont::LeftFileFont::Read(uint32_t character, uint16_t b[16], unsigned short & h, unsigned short & w)
{
	UnicodeMap::const_iterator map_entry(find(unicode_map, character));
	if (unicode_map.end() == map_entry) return false;
	const std::size_t g(character - map_entry->codepoint + map_entry->glyph_number);
	const off_t start(GlyphOffset(g));
	if (width <= 8U) {
		uint8_t glyph[16];
		pread(fd, glyph, height < sizeof glyph ? height : sizeof glyph, start);
		for (unsigned row(0U); row < height; ++row) b[row] = static_cast<uint16_t>(glyph[row]) << 8U;
	} else {
		uint16_t glyph[16];
		pread(fd, glyph, height * sizeof *glyph < sizeof glyph ? height * sizeof *glyph : sizeof glyph, start);
		for (unsigned row(0U); row < height; ++row) b[row] = be16toh(glyph[row]);
	}
	w = width;
	h = height;
	return true;
}

bool 
CombinedFont::LeftRightFileFont::Read(uint32_t character, uint16_t b[16], unsigned short & h, unsigned short & w)
{
	UnicodeMap::const_iterator left_map_entry(find(left_map, character));
	UnicodeMap::const_iterator right_map_entry(find(right_map, character));

	if (left_map.end() == left_map_entry && right_map.end() == right_map_entry) return false;

	uint8_t left_glyph[16] = { 0 }, right_glyph[16] = { 0 };

	if (left_map.end() != left_map_entry) {
		const std::size_t g(character - left_map_entry->codepoint + left_map_entry->glyph_number);
		const off_t start(GlyphOffset(g));
		pread(fd, left_glyph, height < sizeof left_glyph ? height : sizeof left_glyph, start);
	}

	if (right_map.end() != right_map_entry) {
		const std::size_t g(character - right_map_entry->codepoint + right_map_entry->glyph_number);
		const off_t start(GlyphOffset(g));
		pread(fd, right_glyph, height < sizeof right_glyph ? height : sizeof right_glyph, start);
	}

	if (left_map.end() == left_map_entry) {
		for (unsigned row(0U); row < height; ++row) b[row] = static_cast<uint16_t>(right_glyph[row]) << 8U;
		w = 8U;
	} else
	if (right_map.end() == right_map_entry) {
		for (unsigned row(0U); row < height; ++row) b[row] = static_cast<uint16_t>(left_glyph[row]) << 8U;
		w = 8U;
	} else
	{
		for (unsigned row(0U); row < height; ++row) b[row] = (static_cast<uint16_t>(left_glyph[row]) << 8U) | static_cast<uint16_t>(right_glyph[row]);
		w = 16U;
	}
	h = height;

	return true;
}

void 
CombinedFont::Font::AddMapping(CombinedFont::Font::UnicodeMap & unicode_map, uint32_t character, std::size_t glyph_number, std::size_t count)
{
	const UnicodeMapEntry map_entry = { character, glyph_number, count };
	unicode_map.push_back(map_entry);
}

CombinedFont::~CombinedFont()
{
	for (FontList::iterator i(fonts.begin()); i != fonts.end(); i = fonts.erase(i))
		delete *i;
}

CombinedFont::MemoryFont * 
CombinedFont::AddMemoryFont(CombinedFont::Font::Weight w, CombinedFont::Font::Slant s, unsigned short y, unsigned short x, void * b, std::size_t z, std::size_t o) 
{ 
	MemoryFont * f(new MemoryFont(w, s, y, x, b, z, o));
	if (f) fonts.push_back(f);
	return f;
}

CombinedFont::MemoryMappedFont * 
CombinedFont::AddMemoryMappedFont(CombinedFont::Font::Weight w, CombinedFont::Font::Slant s, unsigned short y, unsigned short x, void * b, std::size_t z, std::size_t o) 
{ 
	MemoryMappedFont * f(new MemoryMappedFont(w, s, y, x, b, z, o));
	if (f) fonts.push_back(f);
	return f;
}

CombinedFont::LeftFileFont * 
CombinedFont::AddLeftFileFont(int d, Font::Weight w, Font::Slant s, unsigned short y, unsigned short x)
{
	LeftFileFont * f(new LeftFileFont(d, w, s, y, x));
	if (f) fonts.push_back(f);
	return f;
}

CombinedFont::LeftRightFileFont * 
CombinedFont::AddLeftRightFileFont(int d, Font::Weight w, Font::Slant s, unsigned short y, unsigned short x)
{
	LeftRightFileFont * f(new LeftRightFileFont(d, w, s, y, x));
	if (f) fonts.push_back(f);
	return f;
}

bool 
CombinedFont::has_bold() const
{
	for (FontList::const_iterator fontit(fonts.begin()); fontit != fonts.end(); ++fontit) {
		const Font * font(*fontit);
		if (Font::LIGHT == font->query_weight() && !font->empty()) 
			return true;
	}
	return false;
}

bool 
CombinedFont::has_faint() const
{
	for (FontList::const_iterator fontit(fonts.begin()); fontit != fonts.end(); ++fontit) {
		const Font * font(*fontit);
		if (Font::BOLD == font->query_weight() && !font->empty()) 
			return true;
	}
	return false;
}

const uint16_t * 
CombinedFont::ReadGlyph (
	CombinedFont::Font & font, 
	uint32_t character, 
	bool synthesize_bold, 
	bool synthesize_oblique
) {
	unsigned short width, height;
	if (!font.Read(character, synthetic, height, width)) return 0;
	const unsigned short y_slack(16U - height);
	if (y_slack) {
		if (8U <= y_slack) {
			for (unsigned row(height); row--; ) synthetic[row * 2U] = synthetic[row * 2U + 1U] = synthetic[row];
			for (unsigned row(height * 2U); row < 16U; ++row) synthetic[row] = 0U;
		} else
			for (unsigned row(height); row < 16U; ++row) synthetic[row] = 0U;
	}
	if (const unsigned short x_slack = 16U - width) {
		if (synthesize_oblique)
			for (unsigned row(0U); row < 16U; ++row) synthetic[row] >>= (((16U - row) * x_slack) / 16U);
		else
		// Square quarter-sized (or smaller) glyphs will have already been doubled in height, and are now doubled in width.
		// Half-width (or smaller) rectangular glyphs for box drawing and block graphic characters can also be doubled in width.
		if (8U <= x_slack && (8U <= y_slack || UnicodeCategorization::IsDrawing(character)))
			for (unsigned row(0U); row < 16U; ++row) synthetic[row] = Expand8To16(synthetic[row]);
		// Larger than half-width rectangular glyphs for horizontally repeatable characters are repeated horizontally to the right.
		if (8U > x_slack && UnicodeCategorization::IsHorizontallyRepeatable(character)) {
			const uint_fast16_t mask((1U << x_slack) - 1U);
			for (unsigned row(0U); row < 16U; ++row) synthetic[row] |= (synthetic[row] >> 8U) & mask;
		} else
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
		if (w != font->query_weight() || s != font->query_slant()) continue;
		if (const uint16_t * r = ReadGlyph(*font, character, synthesize_bold, synthesize_oblique))
			return r;
	}
	return 0;
}

const uint16_t *
CombinedFont::ReadGlyph (uint32_t character, bool bold, bool faint, bool italic)
{
	if (faint) {
		if (bold) {
			if (italic) {
				if (const uint16_t * const f = ReadGlyph(character, CombinedFont::Font::DEMIBOLD, CombinedFont::Font::ITALIC, false, false))
					return f;
				if (const uint16_t * const f = ReadGlyph(character, CombinedFont::Font::DEMIBOLD, CombinedFont::Font::OBLIQUE, false, false))
					return f;
			}
			if (const uint16_t * const f = ReadGlyph(character, CombinedFont::Font::DEMIBOLD, CombinedFont::Font::UPRIGHT, false, italic)) 
				return f;
		}
		if (italic) {
			if (const uint16_t * const f = ReadGlyph(character, CombinedFont::Font::LIGHT, CombinedFont::Font::ITALIC, bold, false))
				return f;
			if (const uint16_t * const f = ReadGlyph(character, CombinedFont::Font::LIGHT, CombinedFont::Font::OBLIQUE, bold, false))
				return f;
		}
		if (const uint16_t * const f = ReadGlyph(character, CombinedFont::Font::LIGHT, CombinedFont::Font::UPRIGHT, bold, italic)) 
			return f;
	}
	if (bold) {
		if (italic) {
			if (const uint16_t * const f = ReadGlyph(character, CombinedFont::Font::BOLD, CombinedFont::Font::ITALIC, false, false))
				return f;
			if (const uint16_t * const f = ReadGlyph(character, CombinedFont::Font::BOLD, CombinedFont::Font::OBLIQUE, false, false))
				return f;
		}
		if (const uint16_t * const f = ReadGlyph(character, CombinedFont::Font::BOLD, CombinedFont::Font::UPRIGHT, false, italic)) 
			return f;
	}
	if (italic) {
		if (const uint16_t * const f = ReadGlyph(character, CombinedFont::Font::MEDIUM, CombinedFont::Font::ITALIC, bold, false))
			return f;
		if (const uint16_t * const f = ReadGlyph(character, CombinedFont::Font::MEDIUM, CombinedFont::Font::OBLIQUE, bold, false))
			return f;
	}
	if (const uint16_t * const f = ReadGlyph(character, CombinedFont::Font::MEDIUM, CombinedFont::Font::UPRIGHT, bold, italic)) 
		return f;
	return 0;
}
