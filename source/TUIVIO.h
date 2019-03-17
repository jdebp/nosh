/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_TUIVIO_H)
#define INCLUDE_TUIVIO_H

class TUIDisplayCompositor;
#include "CharacterCell.h"

/// \brief VIO-style access to a TUIDisplayCompositor
///
/// This is modelled roughly after the VIO API of OS/2.
/// It is legal to supply negative and out of bounds coordinates; clipping is performed.
struct TUIVIO
{
	TUIVIO(TUIDisplayCompositor & comp);

	void WriteNCharsAttr (long row, long col, const CharacterCell::attribute_type & attr, const ColourPair & colour, CharacterCell::character_type c, unsigned n);
	void WriteNAttrs (long row, long col, const CharacterCell::attribute_type & attr, const ColourPair & colour, unsigned n);
	void WriteCharStrAttr (long row, long col, const CharacterCell::attribute_type & attr, const ColourPair & colour, const CharacterCell::character_type * b, std::size_t l);
	void WriteCharStrAttr (long row, long col, const CharacterCell::attribute_type & attr, const ColourPair & colour, const char * b, std::size_t l);
	void WriteCharStr (long row, long col, const CharacterCell::character_type * b, std::size_t l);
	void WriteCharStr (long row, long col, const char * b, std::size_t l);

	void Print (long row, long & col, const CharacterCell::attribute_type & attr, const ColourPair & colour, CharacterCell::character_type c);
	void Print (long row, long & col, const CharacterCell::attribute_type & attr, const ColourPair & colour, const CharacterCell::character_type * b, std::size_t l);
	void Print (long row, long & col, const CharacterCell::attribute_type & attr, const ColourPair & colour, const char * b, std::size_t l);
	void Print (long row, long & col, const CharacterCell::character_type * b, std::size_t l);
	void Print (long row, long & col, const char * b, std::size_t l);
	void PrintFormatted (long row, long & col, const CharacterCell::attribute_type & attr, const ColourPair & colour, const char * format, ...);
	void PrintFormatted (long row, long & col, const char * format, ...);

protected:
	TUIDisplayCompositor & c;

	void poke_quick (long row, long col, const CharacterCell::attribute_type & attr, const ColourPair & colour, CharacterCell::character_type c);
	void poke_quick (long row, long col, const CharacterCell::attribute_type & attr, const ColourPair & colour, const CharacterCell::character_type * b, std::size_t l);
	void poke_quick (long row, long col, const CharacterCell::attribute_type & attr, const ColourPair & colour, const char * b, std::size_t l);
	void poke_quick (long row, long col, const CharacterCell::character_type * b, std::size_t l);
	void poke_quick (long row, long col, const char * b, std::size_t l);
};

#endif
