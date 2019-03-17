/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstddef>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include "CharacterCell.h"
#include "TUIDisplayCompositor.h"
#include "TUIVIO.h"

/* The TUIVIO class *********************************************************
// **************************************************************************
*/

TUIVIO::TUIVIO(
        TUIDisplayCompositor & comp
) :
        c(comp)
{
}

inline
void
TUIVIO::poke_quick (
	long row,
	long col,
	const CharacterCell::attribute_type & attr,
	const ColourPair & colour,
	CharacterCell::character_type character
) {
	c.poke(row, col, CharacterCell(character, attr, colour));
}

inline
void
TUIVIO::poke_quick (
	long row,
	long col,
	const CharacterCell::attribute_type & attr,
	const ColourPair & colour,
	const CharacterCell::character_type * b,
	std::size_t l
) {
	CharacterCell cell('\0', attr, colour);
	while (l) {
		--l;
		cell.character = *b++;
		c.poke(row, col++, cell);
	}
}

inline
void
TUIVIO::poke_quick (
	long row,
	long col,
	const CharacterCell::attribute_type & attr,
	const ColourPair & colour,
	const char * b,
	std::size_t l
) {
	CharacterCell cell('\0', attr, colour);
	while (l) {
		--l;
		cell.character = *b++;
		c.poke(row, col++, cell);
	}
}

inline
void
TUIVIO::poke_quick (
	long row,
	long col,
	const CharacterCell::character_type * b,
	std::size_t l
) {
	while (l) {
		--l;
		CharacterCell cell(c.new_at(row, col));
		cell.character = *b++;
		c.poke(row, col, cell);
		++col;
	}
}

inline
void
TUIVIO::poke_quick (
	long row,
	long col,
	const char * b,
	std::size_t l
) {
	while (l) {
		--l;
		CharacterCell cell(c.new_at(row, col));
		cell.character = *b++;
		c.poke(row, col, cell);
		++col;
	}
}

void
TUIVIO::WriteNCharsAttr (
        long row,
        long col,
        const CharacterCell::attribute_type & attr,
        const ColourPair & colour,
        CharacterCell::character_type character,
        unsigned n
) {
	if (row < 0 || c.query_h() <= row || c.query_w() <= col || (col < 0 && -col >= n))
		return;
	if (col < 0) {
		n -= -col;
		col = 0;
	}
	if (col + n > c.query_w()) n = c.query_w() - col;
	const CharacterCell cell(character, attr, colour);
	while (n) {
		--n;
		c.poke(row, col, cell);
                ++col;
	}
}

void
TUIVIO::WriteNAttrs (
        long row,
        long col,
        const CharacterCell::attribute_type & attr,
        const ColourPair & colour,
        unsigned n
) {
	if (row < 0 || c.query_h() <= row || c.query_w() <= col || (col < 0 && -col >= n))
		return;
	if (col < 0) {
		n -= -col;
		col = 0;
	}
	if (col + n > c.query_w()) n = c.query_w() - col;
	CharacterCell cell('\0', attr, colour);
	while (n) {
		--n;
		cell.character = c.new_at(row, col).character;
		c.poke(row, col, cell);
                ++col;
	}
}

void
TUIVIO::WriteCharStrAttr (
	long row,
	long col,
	const CharacterCell::attribute_type & attr,
	const ColourPair & colour,
	const CharacterCell::character_type * b,
	std::size_t l
) {
	if (row < 0 || c.query_h() <= row || c.query_w() <= col || (col < 0 && static_cast<std::size_t>(-col) >= l))
		return;
	if (col < 0) {
		b += -col;
		l -= -col;
		col = 0;
	}
	if (col + l > c.query_w()) l = c.query_w() - col;
	poke_quick(row, col, attr, colour, b, l);
}

void
TUIVIO::WriteCharStrAttr (
	long row,
	long col,
	const CharacterCell::attribute_type & attr,
	const ColourPair & colour,
	const char * b,
	std::size_t l
) {
	if (row < 0 || c.query_h() <= row || c.query_w() <= col || (col < 0 && static_cast<std::size_t>(-col) >= l))
		return;
	if (col < 0) {
		b += -col;
		l -= -col;
		col = 0;
	}
	if (col + l > c.query_w()) l = c.query_w() - col;
	poke_quick(row, col, attr, colour, b, l);
}

void
TUIVIO::WriteCharStr (
	long row,
	long col,
	const CharacterCell::character_type * b,
	std::size_t l
) {
	if (row < 0 || c.query_h() <= row || c.query_w() <= col || (col < 0 && static_cast<std::size_t>(-col) >= l))
		return;
	if (col < 0) {
		l -= -col;
		col = 0;
	}
	if (col + l > c.query_w()) l = c.query_w() - col;
	poke_quick(row, col, b, l);
}

void
TUIVIO::WriteCharStr (
	long row,
	long col,
	const char * b,
	std::size_t l
) {
	if (row < 0 || c.query_h() <= row || c.query_w() <= col || (col < 0 && static_cast<std::size_t>(-col) >= l))
		return;
	if (col < 0) {
		l -= -col;
		col = 0;
	}
	if (col + l > c.query_w()) l = c.query_w() - col;
	poke_quick(row, col, b, l);
}

void
TUIVIO::Print (
	long row,
	long & col,
	const CharacterCell::attribute_type & attr,
	const ColourPair & colour,
	CharacterCell::character_type character
) {
	if (0 <= row && row < c.query_h() && 0 <= col && col < c.query_w())
		poke_quick(row, col, attr, colour, character);
	++col;
}

void
TUIVIO::Print (
	long row,
	long & col,
	const CharacterCell::attribute_type & attr,
	const ColourPair & colour,
	const CharacterCell::character_type * b,
	std::size_t l
) {
	WriteCharStrAttr(row, col, attr, colour, b, l);
	col += l;
}

void
TUIVIO::Print (
	long row,
	long & col,
	const CharacterCell::attribute_type & attr,
	const ColourPair & colour,
	const char * b,
	std::size_t l
) {
	WriteCharStrAttr(row, col, attr, colour, b, l);
	col += l;
}

void
TUIVIO::Print (
	long row,
	long & col,
	const CharacterCell::character_type * b,
	std::size_t l
) {
	WriteCharStr(row, col, b, l);
	col += l;
}

void
TUIVIO::Print (
	long row,
	long & col,
	const char * b,
	std::size_t l
) {
	WriteCharStr(row, col, b, l);
	col += l;
}

void
TUIVIO::PrintFormatted (
	long row,
	long & col,
	const CharacterCell::attribute_type & attr,
	const ColourPair & colour,
	const char * format,
	...
) {
	char *buf(0);
	va_list a;
	va_start(a, format);
	const int n(vasprintf(&buf, format, a));
	va_end(a);
	if (buf && 0 <= n)
		Print(row, col, attr, colour, buf, n);
	free(buf);
}

void
TUIVIO::PrintFormatted (
	long row,
	long & col,
	const char * format,
	...
) {
	char *buf(0);
	va_list a;
	va_start(a, format);
	const int n(vasprintf(&buf, format, a));
	va_end(a);
	if (buf && 0 <= n)
		Print(row, col, buf, n);
	free(buf);
}
