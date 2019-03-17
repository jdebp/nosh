/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#define _XOPEN_SOURCE_EXTENDED
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/ioctl.h>
#include <termios.h>
#if defined(__LINUX__) || defined(__linux__)
#include "ttyutils.h"
#elif defined(__OpenBSD__)
#include "ttyutils.h"
#else
#include "ttyutils.h"
#endif
#include <unistd.h>
#include "utils.h"
#include "UnicodeClassification.h"
#include "CharacterCell.h"
#include "ECMA48Output.h"
#include "TerminalCapabilities.h"
#include "TUIDisplayCompositor.h"
#include "TUIOutputBase.h"

/* Colour manipulation ******************************************************
// **************************************************************************
*/

namespace {

inline
void
invert (
	CharacterCell::colour_type & c
) {
	c.red = ~c.red;
	c.green = ~c.green;
	c.blue = ~c.blue;
}

inline
void
dim (
	uint8_t & c
) {
	c = c > 0x40 ? c - 0x40 : 0x00;
}

inline
void
dim (
	CharacterCell::colour_type & c
) {
	dim(c.red);
	dim(c.green);
	dim(c.blue);
}

inline
void
bright (
	uint8_t & c
) {
	c = c < 0xC0 ? c + 0x40 : 0xff;
}

inline
void
bright (
	CharacterCell::colour_type & c
) {
	bright(c.red);
	bright(c.green);
	bright(c.blue);
}

static const CharacterCell::colour_type overscan_fg(ALPHA_FOR_ERASED,255,255,255), overscan_bg(ALPHA_FOR_ERASED,0,0,0);
static const CharacterCell overscan_blank(' ', 0U, overscan_fg, overscan_bg);

}

/* The base class ***********************************************************
// **************************************************************************
*/

inline
bool
TUIOutputBase::is_cheap_to_print(
	unsigned short row,
	unsigned short col, 
	unsigned cols
) const {
	for (unsigned i(0U); i < cols; ++i) {
		const TUIDisplayCompositor::DirtiableCell & cell(c.cur_at(row, col + i));
		if (cell.attributes != current_attr
		||  cell.foreground != current_fg
		||  cell.background != current_bg
		||  UnicodeCategorization::IsWideOrFull(cell.character)
		||  c.is_marked(false, row, col + i)
		||  c.is_pointer(row, col + i)
		)
			return false;
	}
	return true;
}

inline
bool
TUIOutputBase::is_blank(
	uint32_t ch
) const {
	return ch <= SPC || (DEL <= ch && ch <= 0x0000009F);
}

inline
bool
TUIOutputBase::is_all_blank(
	unsigned short row,
	unsigned short col, 
	unsigned cols
) const {
	for (unsigned i(0U); i < cols; ++i) {
		const TUIDisplayCompositor::DirtiableCell & cell(c.cur_at(row, col + i));
		if (cell.attributes != current_attr
		||  cell.foreground != current_fg
		||  cell.background != current_bg
		||  !is_blank(cell.character)
		||  c.is_marked(false, row, col + i)
		||  c.is_pointer(row, col + i)
		)
			return false;
	}
	return true;
}

inline
void
TUIOutputBase::GotoYX(
	const unsigned short row,
	const unsigned short col
) {
	if (row == cursor_y && col == cursor_x) 
		return;
	if (0 == col && 0 == row) {
		out.CUP();
		cursor_y = cursor_x = 0;
		return;
	}
	if (0 == col ? col != cursor_x :
	    1 == col ? 2 < cursor_x :
	    2 == col ? 5 < cursor_x :
	    false
	) {
		if (row > cursor_y) {
			out.newline();
			++cursor_y;
			if (row > cursor_y) {
				const unsigned short n(row - cursor_y);
				if (out.caps.use_IND && n <= 3U)
					out.print_control_characters(IND, n);
				else
				if (n <= 6U)
					out.print_control_characters(LF, n);
				else
					out.CUD(n);
				cursor_y = row;
			}
		} else
			out.print_control_character(CR);
		cursor_x = 0;
	}
	if (col == cursor_x) {
		if (row < cursor_y) {
			const unsigned short n(cursor_y - row);
			if (out.caps.use_RI && n <= 3U)
				out.print_control_characters(RI, n);
			else
				out.CUU(n);
			cursor_y = row;
		} else
		if (row > cursor_y) {
			const unsigned short n(row - cursor_y);
			if (out.caps.use_IND && n <= 3U)
				out.print_control_characters(IND, n);
			else
			if (n <= 6U)
				out.print_control_characters(LF, n);
			else
				out.CUD(n);
			cursor_y = row;
		}
	} else
	if (row == cursor_y && cursor_x < c.query_w()) {
		if (col < cursor_x) {
			const unsigned short n(cursor_x - col);
			if (n <= 6U)
				out.print_control_characters(BS, n);
			else
				out.CUL(n);
			cursor_x = col;
		} else
		if (col > cursor_x) {
			const unsigned short n(col - cursor_x);
			if (n <= 6U && is_cheap_to_print(cursor_y, cursor_x, n))
				for (unsigned i(cursor_x); i < col; ++i) {
					TUIDisplayCompositor::DirtiableCell & cell(c.cur_at(cursor_y, i));
					print(cell, false /* We checked for no pointer or mark. */);
					cell.untouch();
				}
			else
				out.CUR(n);
			cursor_x = col;
		}
	} else
	{
		out.CUP(row + 1U, col + 1U);
		cursor_y = row;
		cursor_x = col;
	}
}

inline
void
TUIOutputBase::SGRAttr1(
	const CharacterCell::attribute_type & attr,
	const CharacterCell::attribute_type & mask,
	char m,
	char & semi
) const {
	if ((attr & mask) != (current_attr & mask)) {
		if (semi) out.print_graphic_character(semi);
		if (!(attr & mask)) out.print_graphic_character('2');
		out.print_graphic_character(m);
		semi = ';';
	}
}

inline
void
TUIOutputBase::SGRAttr1(
	const CharacterCell::attribute_type & attr,
	const CharacterCell::attribute_type & mask,
	const CharacterCell::attribute_type & unit,
	char m,
	char & semi
) const {
	const CharacterCell::attribute_type bits(attr & mask);
	if (bits != (current_attr & mask)) {
		if (semi) out.print_graphic_character(semi);
		if (!bits) out.print_graphic_character('2');
		out.print_graphic_character(m);
		if (bits && (bits != unit))
			out.print_subparameter(bits / unit);
		semi = ';';
	}
}

inline
void
TUIOutputBase::SGRAttr (
	const CharacterCell::attribute_type & attr
) {
	if (attr == current_attr) return;
	out.csi();
	char semi(0);
	if (!out.caps.has_reverse_off && (current_attr & CharacterCell::INVERSE)) {
		if (semi) out.print_graphic_character(semi);
		out.print_graphic_character('0');
		semi = ';';
		current_attr = 0;
	}
	enum { BF = CharacterCell::BOLD|CharacterCell::FAINT };
	if ((attr & BF) != (current_attr & BF)) {
		if (current_attr & BF) {
			if (semi) out.print_graphic_character(semi);
			out.print_graphic_character('2');
			out.print_graphic_character('2');
			semi = ';';
		}
		if (CharacterCell::BOLD & attr) {
			if (semi) out.print_graphic_character(semi);
			out.print_graphic_character('1');
			semi = ';';
		}
		if (CharacterCell::FAINT & attr) {
			if (semi) out.print_graphic_character(semi);
			out.print_graphic_character('2');
			semi = ';';
		}
	}
	SGRAttr1(attr, CharacterCell::ITALIC, '3', semi);
	SGRAttr1(attr, CharacterCell::UNDERLINES, CharacterCell::SIMPLE_UNDERLINE, '4', semi);
	SGRAttr1(attr, CharacterCell::BLINK, '5', semi);
	SGRAttr1(attr, CharacterCell::INVERSE, '7', semi);
	SGRAttr1(attr, CharacterCell::INVISIBLE, '8', semi);
	SGRAttr1(attr, CharacterCell::STRIKETHROUGH, '9', semi);
	out.print_graphic_character('m');
	current_attr = attr;
}

// This has to match the way that most realizing terminals will advance the cursor.
inline
unsigned
TUIOutputBase::width (
	uint32_t ch
) const {

	if ((ch < SPC)
	||  (DEL <= ch && ch <= 0x0000009F)
	||  (0x00001160 <= ch && ch <= 0x000011FF)
	||  (0x0000200B == ch)
	||  UnicodeCategorization::IsMarkEnclosing(ch)
	||  UnicodeCategorization::IsMarkNonSpacing(ch)
	||  UnicodeCategorization::IsOtherFormat(ch)
	)
		return 0U;
	if (!out.caps.has_square_mode) {
		if (0x000000AD == ch) return 1U;
		if (UnicodeCategorization::IsWideOrFull(ch))
			return 2U;
	}
	return 1U;
}

void
TUIOutputBase::print(
	const TUIDisplayCompositor::DirtiableCell & cell,
	bool inverted
) {
	CharacterCell::attribute_type font_attributes(cell.attributes);
	CharacterCell::colour_type fg(cell.foreground), bg(cell.background);

	if (faint_as_colour) {
		if (CharacterCell::FAINT & font_attributes) {
			dim(fg);
//			dim(bg);
			font_attributes &= ~CharacterCell::FAINT;
		}
	}
	if (bold_as_colour) {
		if (CharacterCell::BOLD & font_attributes) {
			bright(fg);
//			bright(bg);
			font_attributes &= ~CharacterCell::BOLD;
		}
	}
	if (inverted) {
		invert(fg);
		invert(bg);
	}

	const unsigned w(width(cell.character));
	bool replace_with_spaces(false);

	if (!out.caps.has_invisible && (CharacterCell::INVISIBLE & font_attributes)) {
		font_attributes &= ~CharacterCell::INVISIBLE;
		replace_with_spaces = true;
	} else
	if (is_blank(cell.character)) {
		replace_with_spaces = true;
	}

	SGRFGColour(fg);
	SGRBGColour(bg);
	SGRAttr(font_attributes);
	if (replace_with_spaces) {
		for (unsigned n(w); n > 0U; --n)
			out.UTF8(SPC);
	} else
		out.UTF8(cell.character);
	for (unsigned n(w); n > 0U; --n) {
		++cursor_x;
		if (!out.caps.pending_wrap && cursor_x >= c.query_w()) {
			cursor_x = 0;
			if (cursor_y < c.query_h())
				++cursor_y;
		}
	}
}

void
TUIOutputBase::enter_full_screen_mode(
) {
	if (0 <= tcgetattr_nointr(out.fd(), original_attr))
		tcsetattr_nointr(out.fd(), TCSADRAIN, make_raw(original_attr));
	if (out.caps.use_DECPrivateMode) {
		out.XTermSaveRestore(true);
		out.XTermAlternateScreenBuffer(alternate_screen_buffer);
	}
	out.CUP();
	SGRAttr(0U);
	SGRFGColour(overscan_fg);
	SGRBGColour(overscan_bg);
	// DEC Locator is the less preferable protocol since it does not carry modifier information.
	if (out.caps.use_DECLocator && !out.caps.has_XTerm1006Mouse) {
		out.DECELR(true);
		out.DECSLE(true /*press*/, true);
		out.DECSLE(false /*release*/, true);
	}
	if (out.caps.use_DECPrivateMode) {
		if (out.caps.has_square_mode)
			out.SquareMode(true);
		out.DECAWM(false);
		out.DECECM(false);	// We rely upon erasure to the background colour, not to the screen/default colour.
		out.DECBKM(true);	// We want to be able to distinguish Backspace from Control+Backspace.
		if (out.caps.has_XTerm1006Mouse)
			out.XTermSendAnyMouseEvents();
		else
			out.XTermSendNoMouseEvents();
		out.XTermDeleteIsDEL(false);	// We want to be able to distinguish Delete from Control+Backspace.
		out.DECCKM(cursor_application_mode);
		out.DECNKM(calculator_application_mode);
	}
	out.change_cursor_visibility(false);
	out.SCUSR(cursor_attributes, cursor_glyph);
	out.flush();
}

void
TUIOutputBase::exit_full_screen_mode(
) {
	out.SCUSR();
	out.change_cursor_visibility(true);
	if (out.caps.use_DECLocator) {
		out.DECELR(false);
		out.DECSLE();
	}
	if (out.caps.use_DECPrivateMode) {
		out.DECNKM(false);
		out.DECCKM(false);
		out.XTermSendNoMouseEvents();
		out.DECBKM(false);			// Restore the more common Unix convention.
		out.DECECM(false);
		out.DECAWM(true);
		if (out.caps.has_square_mode)
			out.SquareMode(true);
	}
	SGRBGColour(overscan_bg);
	SGRFGColour(overscan_fg);
	SGRAttr(0U);
	GotoYX(0U, 0U);
	if (out.caps.use_DECPrivateMode) {
		out.XTermAlternateScreenBuffer(false);
		out.XTermSaveRestore(false);
	}
	out.flush();
	tcsetattr_nointr(out.fd(), TCSADRAIN, original_attr);
}

TUIOutputBase::TUIOutputBase(
	const TerminalCapabilities & t,
	FILE * f,
	bool bc,
	bool fc,
	bool cursa,
	bool calca,
	bool ab,
	TUIDisplayCompositor & comp
) :
	c(comp),
	out(t, f, true /* C1 is 7-bit aliased */),
	bold_as_colour(bc),
	faint_as_colour(fc),
	cursor_application_mode(cursa),
	calculator_application_mode(calca),
	alternate_screen_buffer(ab),
	window_resized(true),
	refresh_needed(true),
	update_needed(true),
	cursor_y(0),
	cursor_x(0),
	current_fg(-1U, 0U, 0U, 0U),	// Set an impossible colour, forcing a change.
	current_bg(-1U, 0U, 0U, 0U),	// Set an impossible colour, forcing a change.
	current_attr(-1U),	// Assume all attributes are on and need to be turned off.
	cursor_glyph(CursorSprite::BOX),
	cursor_attributes(CursorSprite::VISIBLE)
{
	out.flush();
	std::setvbuf(out.file(), out_buffer, _IOFBF, sizeof out_buffer);
	enter_full_screen_mode();
}

TUIOutputBase::~TUIOutputBase()
{
	exit_full_screen_mode();
	std::setvbuf(out.file(), 0, _IOFBF, 1024);
}

void
TUIOutputBase::handle_resize_event (
) {
	if (window_resized) {
		window_resized = false;
		struct winsize size;
		if (0 <= tcgetwinsz_nointr(out.fd(), size))
			c.resize(size.ws_row, size.ws_col);
		refresh_needed = true;
	}
}

void
TUIOutputBase::handle_refresh_event (
) {
	if (refresh_needed) {
		refresh_needed = false;
		redraw_new();
		update_needed = true;
	}
}

void
TUIOutputBase::handle_update_event (
) {
	if (update_needed) {
		update_needed = false;
		c.repaint_new_to_cur();
		write_changed_cells_to_output();
	}
}

void
TUIOutputBase::write_changed_cells_to_output()
{
	const CursorSprite::attribute_type a(c.query_cursor_attributes());
	if (CursorSprite::VISIBLE & a)
		out.change_cursor_visibility(false);
	for (unsigned row(0); row < c.query_h(); ++row) {
		for (unsigned col(0); col < c.query_w(); ++col) {
			TUIDisplayCompositor::DirtiableCell & cell(c.cur_at(row, col));
			if (!cell.touched()) continue;
			GotoYX(row, col);
			if (is_blank(cell.character)) {
				const unsigned n(c.query_w() - col);
				if (3U < n && is_cheap_to_print(row, col, n) && is_all_blank(row, col, n)) {
					out.EL(0U);
					while (col < c.query_w())
						c.cur_at(row, col++).untouch();
					continue;
				}
			}
			const bool inverted(
				((c.query_cursor_attributes() & CursorSprite::VISIBLE) && c.is_marked(false, row, col))
			||
				((c.query_pointer_attributes() & PointerSprite::VISIBLE) && c.is_pointer(row, col))
			);
			print(cell, inverted);
			cell.untouch();
			unsigned n(width(cell.character));
			if (1U < n && is_cheap_to_print(row, col + 1U, n - 1U) && is_all_blank(row, col + 1U, n - 1U)) {
				while (1U < n && col + 1U < c.query_w()) {
					c.cur_at(row, ++col).untouch();
					--n;
				}
			}
		}
	}
	GotoYX(c.query_cursor_row(), c.query_cursor_col());
	const CursorSprite::glyph_type g(c.query_cursor_glyph());
	if (a != cursor_attributes || g != cursor_glyph) {
		cursor_attributes = a;
		cursor_glyph = g;
		out.SCUSR(cursor_attributes, cursor_glyph);
	}
	if (CursorSprite::VISIBLE & a)
		out.change_cursor_visibility(true);
	out.flush();
}

void
TUIOutputBase::erase_new_to_backdrop () 
{
	for (unsigned short row(0U); row < c.query_h(); ++row)
		for (unsigned short col(0U); col < c.query_w(); ++col)
			c.poke(row, col, overscan_blank);
}
