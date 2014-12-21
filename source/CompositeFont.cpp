/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <list>
#include <stdint.h>
#include <cstddef>
#include <sys/mman.h>
#include "Monospace16x16Font.h"
#include "CompositeFont.h"
#include "vtfont.h"

static inline
uint16_t
expand ( uint8_t c )
{
	uint16_t r(0U);
	for (unsigned n(0U); n < 8U; ++n) {
		const bool bit(c & 1U);
		if (bit) r |= 0xC000;
		c >>= 1U;
		r >>= 2U;
	}
	return r;
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

void 
CombinedFont::MemoryFont::Read(std::size_t g, uint16_t b[16])
{
	const void * start(static_cast<const char *>(base) + offset);
	if (query_row_size() > 1U) {
		const uint16_t *glyph(static_cast<const uint16_t (*)[16]>(start)[g]);
		for (unsigned row(0U); row < height; ++row) b[row] = glyph[row];
	} else {
		const uint8_t *glyph(static_cast<const uint8_t (*)[16]>(start)[g]);
		for (unsigned row(0U); row < height; ++row) b[row] = static_cast<uint16_t>(glyph[row]) << 8U;
	}
	for (unsigned row(height); row < 16U; ++row) b[row] = 0U;
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
	Font(w, s, x <= 8U && y <= 8U ? y * 2U : y, x <= 8U && y <= 8U ? x * 2U : x), 
	FileDescriptorOwner(f), 
	original_height(y), 
	original_width(x) 
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

void 
CombinedFont::FileFont::Read(std::size_t g, uint16_t b[16])
{
	const off_t start(GlyphOffset(g));
	if (original_width <= 8U) {
		uint8_t glyph[16];
		pread(fd, glyph, original_height < sizeof glyph ? original_height : sizeof glyph, start);
		if (original_height <= 8U)
			for (unsigned row(original_height); row-- > 0; ) b[row * 2U + 1U] = b[row * 2U] = expand(glyph[row]);
		else {
			for (unsigned row(0U); row < original_height; ++row) b[row] = static_cast<uint16_t>(glyph[row]) << 8U;
			for (unsigned row(original_height); row < 16U; ++row) b[row] = 0U;
		}
	} else {
		uint16_t glyph[16];
		pread(fd, glyph, original_height < sizeof glyph ? original_height : sizeof glyph, start);
		for (unsigned row(0U); row < original_height; ++row) b[row] = glyph[row];
		for (unsigned row(original_height); row < 16U; ++row) b[row] = 0U;
	}
}

CombinedFont::Font::UnicodeMap::const_iterator
CombinedFont::Font::find (
	uint32_t character
) const {
	const UnicodeMapEntry one = { character, 0U, 1U };
	UnicodeMap::const_iterator p(std::lower_bound(unicode_map.begin(), unicode_map.end(), one));
	if (p < unicode_map.end() && !p->Contains(character)) p = unicode_map.end();
	return p;
}

void 
CombinedFont::Font::AddMapping(uint32_t character, std::size_t glyph_number, std::size_t count)
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

CombinedFont::FileFont * 
CombinedFont::AddFileFont(int d, Font::Weight w, Font::Slant s, unsigned short y, unsigned short x)
{
	FileFont * f(new FileFont(d, w, s, y, x));
	if (f) fonts.push_back(f);
	return f;
}

const uint16_t * 
CombinedFont::ReadGlyph (
	CombinedFont::Font & font, 
	std::size_t glyph_number, 
	bool synthesize_bold, 
	bool synthesize_oblique
) {
	font.Read(glyph_number, synthetic);
	if (const unsigned int slack = 16U - font.query_width()) {
		if (synthesize_oblique)
			for (unsigned row(0U); row < 16U; ++row) synthetic[row] >>= (((16U - row) * slack) / 16U);
		else
			for (unsigned row(0U); row < 16U; ++row) synthetic[row] >>= (slack / 2U);
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
		Font::UnicodeMap::const_iterator map_entry(font->find(character));
		if (font->end() == map_entry) continue;
		return ReadGlyph(*font, character - map_entry->codepoint + map_entry->glyph_number, synthesize_bold, synthesize_oblique);
	}
	return 0;
}

inline
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

