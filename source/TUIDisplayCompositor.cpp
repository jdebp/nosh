/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstddef>
#include "CharacterCell.h"
#include "TUIDisplayCompositor.h"

/* Character cell comparison ************************************************
// **************************************************************************
*/

static inline
bool
operator != (
	const CharacterCell & a,
	const CharacterCell & b
) {
	return a.character != b.character || a.attributes != b.attributes || a.foreground != b.foreground || a.background != b.background;
}

/* The TUIDisplayCompositor class *******************************************
// **************************************************************************
*/

TUIDisplayCompositor::TUIDisplayCompositor(
	bool i,
	coordinate init_h,
	coordinate init_w
) :
	invalidate_software_cursor(i),
	cursor_row(0U),
	cursor_col(0U),
	pointer_row(0U),
	pointer_col(0U),
	cursor_glyph(CursorSprite::BLOCK),
	cursor_attributes(0),	// Initialize to invisible so that making it visible causes a repaint.
	pointer_attributes(0),	// Initialize to invisible so that making it visible causes a repaint.
	h(init_h),
	w(init_w),
	cur_cells(static_cast<std::size_t>(h) * w),
	new_cells(static_cast<std::size_t>(h) * w)
{
}

TUIDisplayCompositor::DirtiableCell & 
TUIDisplayCompositor::DirtiableCell::operator = ( const CharacterCell & c ) 
{ 
	if (c != *this) {
		CharacterCell::operator =(c); 
		t = true; 
	}
	return *this; 
}

TUIDisplayCompositor::~TUIDisplayCompositor()
{
}

void 
TUIDisplayCompositor::resize(
	coordinate new_h, 
	coordinate new_w
) {
	if (h == new_h && w == new_w) return;
	touch_all();
	h = new_h;
	w = new_w;
	const std::size_t s(static_cast<std::size_t>(h) * w);
	if (cur_cells.size() != s) cur_cells.resize(s);
	if (new_cells.size() != s) new_cells.resize(s);
}

void
TUIDisplayCompositor::touch_all()
{
	for (unsigned row(0); row < h; ++row)
		for (unsigned col(0); col < w; ++col)
			cur_at(row, col).touch();
}

void
TUIDisplayCompositor::repaint_new_to_cur()
{
	for (unsigned row(0); row < h; ++row)
		for (unsigned col(0); col < w; ++col)
			cur_at(row, col) = new_at(row, col);
}

void
TUIDisplayCompositor::poke(coordinate y, coordinate x, const CharacterCell & c)
{
	if (y < h && x < w) new_at(y, x) = c;
}

void 
TUIDisplayCompositor::move_cursor(coordinate row, coordinate col) 
{
	if (cursor_row != row || cursor_col != col) {
		if (invalidate_software_cursor) cur_at(cursor_row, cursor_col).touch();
		cursor_row = row;
		cursor_col = col;
		if (invalidate_software_cursor) cur_at(cursor_row, cursor_col).touch();
	}
}

bool 
TUIDisplayCompositor::change_pointer_row(coordinate row) 
{
	if (row < h && pointer_row != row) {
		cur_at(pointer_row, pointer_col).touch();
		pointer_row = row;
		cur_at(pointer_row, pointer_col).touch();
		return true;
	}
	return false;
}

bool 
TUIDisplayCompositor::change_pointer_col(coordinate col) 
{
	if (col < w && pointer_col != col) {
		cur_at(pointer_row, pointer_col).touch();
		pointer_col = col;
		cur_at(pointer_row, pointer_col).touch();
		return true;
	}
	return false;
}

void 
TUIDisplayCompositor::set_cursor_state(CursorSprite::attribute_type a, CursorSprite::glyph_type g) 
{ 
	if (cursor_attributes != a || cursor_glyph != g) {
		cursor_attributes = a; 
		cursor_glyph = g; 
		if (invalidate_software_cursor) cur_at(cursor_row, cursor_col).touch();
	}
}

void 
TUIDisplayCompositor::set_pointer_attributes(PointerSprite::attribute_type a) 
{ 
	if (pointer_attributes != a) {
		pointer_attributes = a; 
		cur_at(pointer_row, pointer_col).touch();
	}
}

/// This is a fairly minimal function for testing whether a particular cell position is within the current cursor, so that it can be displayed marked.
/// \todo TODO: When we gain mark/copy functionality, the cursor will be the entire marked region rather than just one cell.
bool
TUIDisplayCompositor::is_marked(bool inclusive, coordinate row, coordinate col)
{
	return inclusive && cursor_row == row && cursor_col == col;
}

bool
TUIDisplayCompositor::is_pointer(coordinate row, coordinate col)
{
	return pointer_row == row && pointer_col == col;
}
