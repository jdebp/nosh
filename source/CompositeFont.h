/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_COMPOSITEFONT_H)
#define INCLUDE_COMPOSITEFONT_H

#include <vector>
#include <list>
#include <stdint.h>
#include <unistd.h>
#include <cstddef>
#include "Monospace16x16Font.h"
#include "FileDescriptorOwner.h"

class CombinedFont :
	public Monospace16x16Font
{
public:
	struct Font {
		struct UnicodeMapEntry {
			uint32_t codepoint;
			std::size_t glyph_number, count;
			bool operator < ( const UnicodeMapEntry & ) const ;
			bool Contains (uint32_t) const;
		};
		typedef std::vector<UnicodeMapEntry> UnicodeMap;
		enum Weight { LIGHT, MEDIUM, DEMIBOLD, BOLD, NUM_WEIGHTS };
		enum Slant { UPRIGHT, ITALIC, OBLIQUE, NUM_SLANTS };

		Font ( Weight w, Slant s ) : weight(w), slant(s) {}
		virtual ~Font() = 0;

		Weight query_weight() const { return weight; }
		Slant query_slant() const { return slant; }

		virtual bool Read(uint32_t, uint16_t b[16], unsigned short &, unsigned short &) = 0;
		virtual bool empty() const = 0;
	protected:
		Weight weight;
		Slant slant;
		void AddMapping(UnicodeMap & unicode_map, uint32_t character, std::size_t glyph_number, std::size_t count);
	};
	struct MemoryFont : public Font {
		MemoryFont(Weight w, Slant s, unsigned short y, unsigned short x, void * b, std::size_t z, std::size_t o) : Font(w, s), base(b), size(z), offset(o), height(y), width(x) {}
		void AddMapping(uint32_t c, std::size_t g, std::size_t l) { Font::AddMapping(unicode_map, c, g, l); }
	protected:
		void * base;
		std::size_t size, offset;
		unsigned short height, width;
		UnicodeMap unicode_map;
		~MemoryFont() {}
		virtual bool Read(uint32_t, uint16_t b[16], unsigned short &, unsigned short &);
		virtual bool empty() const { return unicode_map.empty(); }
	};
	struct MemoryMappedFont : public MemoryFont {
		MemoryMappedFont(Weight w, Slant s, unsigned short y, unsigned short x, void * b, std::size_t z, std::size_t o) : MemoryFont(w, s, y, x, b, z, o) {}
	protected:
		~MemoryMappedFont();
	};
	struct FileFont : public Font, public FileDescriptorOwner {
		FileFont(int f, Weight w, Slant s, unsigned short y, unsigned short x);
		off_t GlyphOffset(std::size_t g) ;
	protected:
		unsigned short height, width;

		~FileFont() ;
		unsigned short query_cell_size() const { return ((width + 7U) / 8U) * height; }
	};
	struct LeftFileFont : public FileFont {
		LeftFileFont(int f, Weight w, Slant s, unsigned short y, unsigned short x);
		void AddMapping(uint32_t c, std::size_t g, std::size_t l) { FileFont::AddMapping(unicode_map, c, g, l); }
	protected:
		UnicodeMap unicode_map;
		virtual bool Read(uint32_t, uint16_t b[16], unsigned short &, unsigned short &);
		virtual bool empty() const { return unicode_map.empty(); }
	};
	struct LeftRightFileFont : public FileFont {
		LeftRightFileFont(int f, Weight w, Slant s, unsigned short y, unsigned short x);
		void AddLeftMapping(uint32_t c, std::size_t g, std::size_t l) { FileFont::AddMapping(left_map, c, g, l); }
		void AddRightMapping(uint32_t c, std::size_t g, std::size_t l) { FileFont::AddMapping(right_map, c, g, l); }
	protected:
		UnicodeMap left_map, right_map;
		virtual bool Read(uint32_t, uint16_t b[16], unsigned short &, unsigned short &);
		virtual bool empty() const { return left_map.empty() && right_map.empty(); }
	};

	~CombinedFont();

	MemoryFont * AddMemoryFont(Font::Weight, Font::Slant, unsigned short y, unsigned short x, void * b, std::size_t z, std::size_t o);
	MemoryMappedFont * AddMemoryMappedFont(Font::Weight, Font::Slant, unsigned short y, unsigned short x, void * b, std::size_t z, std::size_t o);
	LeftFileFont * AddLeftFileFont(int, Font::Weight, Font::Slant, unsigned short y, unsigned short x);
	LeftRightFileFont * AddLeftRightFileFont(int, Font::Weight, Font::Slant, unsigned short y, unsigned short x);

	bool has_bold() const;
	bool has_faint() const;

	virtual const uint16_t * ReadGlyph (uint32_t character, bool bold, bool faint, bool italic);
protected:
	typedef std::list<Font *> FontList;
	FontList fonts;
	uint16_t synthetic[16];

	const uint16_t * ReadGlyph (Font &, uint32_t character, bool synthesize_bold, bool synthesize_oblique);
	const uint16_t * ReadGlyph (uint32_t character, Font::Weight w, Font::Slant s, bool synthesize_bold, bool synthesize_oblique);
};

#endif
