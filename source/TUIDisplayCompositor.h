/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_TUIDISPLAYCOMPOSITOR_H)
#define INCLUDE_TUIDISPLAYCOMPOSITOR_H

#include <vector>
#include "CharacterCell.h"

/// \brief Output composition and change buffering for a TUI
///
/// This implements a "new" array and a "cur" array.
/// The output is composed into the "new" array and then transposed into the "cur" array.
/// Entries in the "cur" array have an additional "touched" flag to indicate that they were changed during transposition.
/// Actually outputting the "cur" array is the job of another class; this class knows nothing about I/O.
/// Layered on top of this are VIO and other access methods.
class TUIDisplayCompositor
{
public:
	typedef unsigned short coordinate;

	TUIDisplayCompositor(bool invalidate_software_cursor, coordinate h, coordinate w);
	~TUIDisplayCompositor();
	coordinate query_h() const { return h; }
	coordinate query_w() const { return w; }
	coordinate query_cursor_row() const { return cursor_row; }
	coordinate query_cursor_col() const { return cursor_col; }
	void touch_all();
	void repaint_new_to_cur();
	void poke(coordinate y, coordinate x, const CharacterCell & c);
	void move_cursor(coordinate y, coordinate x);
	bool change_pointer_row(coordinate row);
	bool change_pointer_col(coordinate col);
	bool is_marked(bool inclusive, coordinate y, coordinate x);
	bool is_pointer(coordinate y, coordinate x);
	void set_cursor_state(CursorSprite::attribute_type a, CursorSprite::glyph_type g);
	void set_pointer_attributes(PointerSprite::attribute_type a);
	CursorSprite::glyph_type query_cursor_glyph() const { return cursor_glyph; }
	CursorSprite::attribute_type query_cursor_attributes() const { return cursor_attributes; }
	PointerSprite::attribute_type query_pointer_attributes() const { return pointer_attributes; }
	void resize(coordinate h, coordinate w);

	class DirtiableCell : public CharacterCell {
	public:
		DirtiableCell() : CharacterCell(), t(true) {}
		bool touched() const { return t; }
		void untouch() { t = false; }
		void touch() { t = true; }
		DirtiableCell & operator = ( const CharacterCell & c );
	protected:
		bool t;
	};

	DirtiableCell & cur_at(coordinate y, coordinate x) { return cur_cells[static_cast<std::size_t>(y) * w + x]; }
	CharacterCell & new_at(coordinate y, coordinate x) { return new_cells[static_cast<std::size_t>(y) * w + x]; }
protected:
	bool invalidate_software_cursor;
	coordinate cursor_row, cursor_col;
	coordinate pointer_row, pointer_col;
	CursorSprite::glyph_type cursor_glyph;
	CursorSprite::attribute_type cursor_attributes;
	PointerSprite::attribute_type pointer_attributes;
	coordinate h, w;
	std::vector<DirtiableCell> cur_cells;
	std::vector<CharacterCell> new_cells;
};

#endif
