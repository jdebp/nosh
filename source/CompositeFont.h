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
		enum Weight { LIGHT, MEDIUM, DEMIBOLD, BOLD, BLACK };
		enum Slant { UPRIGHT, ITALIC, OBLIQUE };

		Font ( Weight w, Slant s, unsigned short y, unsigned short x ) : weight(w), slant(s), height(y), width(x) {}
		virtual ~Font() = 0;

		UnicodeMap::const_iterator end() const { return unicode_map.end(); }
		UnicodeMap::const_iterator find(uint32_t) const;
		Weight query_weight() const { return weight; }
		Slant query_slant() const { return slant; }
		unsigned short query_height() const { return height; }
		unsigned short query_width() const { return width; }
		unsigned short query_row_size() const { return (width + 7U) / 8U; }

		virtual void Read(std::size_t, uint16_t b[16]) = 0;
		void AddMapping(uint32_t c, std::size_t g, std::size_t l);
	protected:
		Weight weight;
		Slant slant;
		unsigned short height, width;
	private:
		UnicodeMap unicode_map;
	};
	struct MemoryFont : public Font {
		MemoryFont(Weight w, Slant s, unsigned short y, unsigned short x, void * b, std::size_t z, std::size_t o) : Font(w, s, y, x), base(b), size(z), offset(o) {}
	protected:
		void * base;
		std::size_t size, offset;
		~MemoryFont() {}
		virtual void Read(std::size_t, uint16_t b[16]);
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
		unsigned short original_height, original_width;

		~FileFont() ;
		virtual void Read(std::size_t, uint16_t b[16]);
		unsigned short query_cell_size() const { return ((original_width + 7U) / 8U) * original_height; }
	};

	~CombinedFont();

	MemoryFont * AddMemoryFont(Font::Weight, Font::Slant, unsigned short y, unsigned short x, void * b, std::size_t z, std::size_t o);
	MemoryMappedFont * AddMemoryMappedFont(Font::Weight, Font::Slant, unsigned short y, unsigned short x, void * b, std::size_t z, std::size_t o);
	FileFont * AddFileFont(int, Font::Weight, Font::Slant, unsigned short y, unsigned short x);

	virtual const uint16_t * ReadGlyph (uint32_t character, bool bold, bool faint, bool italic);
protected:
	typedef std::list<Font *> FontList;
	FontList fonts;
	uint16_t synthetic[16];

	const uint16_t * ReadGlyph (Font &, std::size_t glyph_number, bool synthesize_bold, bool synthesize_oblique);
	const uint16_t * ReadGlyph (uint32_t character, Font::Weight w, Font::Slant s, bool synthesize_bold, bool synthesize_oblique);
};

#endif
