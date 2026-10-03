/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <vector>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <termios.h>
#if defined(__linux__) || defined(__LINUX__)
#include <sys/ioctl.h>	// For struct winsize on Linux
#endif
#include "utils.h"
#include "ttyutils.h"
#include "UnicodeClassification.h"
#include "CharacterCell.h"
#include "ECMA48Output.h"
#include "TerminalCapabilities.h"
#include "TUIDisplayCompositor.h"
#include "TUIOutputBase.h"
#include "SignalManagement.h"

/* The base class ***********************************************************
// **************************************************************************
*/

// This has to match the way that most realizing terminals will advance the cursor.
inline
unsigned
TUIOutputBase::width (
	char32_t ch
) const {

	if (0x000000AD == ch) return 1U;
	if ((0x00001160 <= ch && ch <= 0x000011FF)
	||  UnicodeCategorization::IsMarkEnclosing(ch)
	||  UnicodeCategorization::IsMarkNonSpacing(ch)
	||  UnicodeCategorization::IsOtherFormat(ch)
	||  UnicodeCategorization::IsOtherSurrogate(ch)
	||  UnicodeCategorization::IsOtherControl(ch)
	)
		return 0U;
	if (!out.caps.has_square_mode) {
		if (UnicodeCategorization::IsWideOrFull(ch))
			return 2U;
	}
	return 1U;
}

void
TUIOutputBase::fixup(
	CharacterCell & cell,
	bool marked,
	bool is_pointer
) const {
	if (invert_screen)
		cell.attributes ^= CharacterCell::INVERSE;
	if (options.faint_as_colour || options.bold_as_colour || out.caps.faulty_reverse_video) {
		// If we are adjusting the colours, we need to do reverse video first so that we adjust the right colours.
		// If the terminal's reverse video is faulty, we need to do it ourselves without the ECMA-48 SGR sequences.
		if (CharacterCell::INVERSE & cell.attributes) {
			std::swap(cell.foreground,cell.background);
			cell.attributes &= ~CharacterCell::INVERSE;
		}
		if (options.faint_as_colour) {
			if (CharacterCell::FAINT & cell.attributes) {
				if (cell.foreground.is_black())
					cell.background.dim();
				else
					cell.foreground.dim();
				cell.attributes &= ~CharacterCell::FAINT;
			}
		}
		if (options.bold_as_colour) {
			if (CharacterCell::BOLD & cell.attributes) {
				if (cell.foreground.is_black())
					cell.background.bright();
				else
					cell.foreground.bright();
				cell.attributes &= ~CharacterCell::BOLD;
			}
		}
	}
	if (is_pointer) {
		if (options.tui_level > 0U)
			marked = !marked;
		else
			cell.character = 0x01FBB0;
	}
	if (marked) {
		cell.foreground.complement();
		cell.background.complement();
		cell.foreground.alpha = cell.background.alpha = ALPHA_FOR_MOUSE_SPRITE;
	}
	if (options.no_default_colour) {
		if (cell.foreground.is_default_or_erased())
			cell.foreground.alpha = ALPHA_FOR_256_COLOURED;
		if (cell.background.is_default_or_erased())
			cell.background.alpha = ALPHA_FOR_256_COLOURED;
	}
	if (out.caps.lacks_invisible && (CharacterCell::INVISIBLE & cell.attributes)) {
		cell.attributes &= ~CharacterCell::INVISIBLE;
		cell.foreground = cell.background;
	}
}

inline
unsigned
TUIOutputBase::count_cheap_narrow(
	unsigned short row,
	unsigned short col,
	unsigned cols
) const {
	for (unsigned i(0U); i < cols; ++i) {
		if (false
		||  c.is_marked(false /* does not include cursor */, row, col + i)
		||  c.is_pointer(row, col + i)
		)
			return i;
		CharacterCell cell(c.cur_at(row, col + i));
		fixup(cell, false, false /* We checked for no pointer or mark. */);
		if (cell.attributes != current_attr
		||  cell.foreground != current.foreground
		||  cell.background != current.background
		||  (1U != width(cell.character))
		)
			return i;
	}
	return cols;
}

inline
unsigned
TUIOutputBase::count_cheap(
	unsigned short row,
	unsigned short col,
	unsigned cols,
	CharacterCell::attribute_type attr,
	uint32_t ch
) const {
	for (unsigned i(0U); i < cols; ++i) {
		if (false
		||  c.is_marked(false /* does not include cursor */, row, col + i)
		||  c.is_pointer(row, col + i)
		)
			return i;
		CharacterCell cell(c.cur_at(row, col + i));
		fixup(cell, false, false /* We checked for no pointer or mark. */);
		if (cell.attributes != attr
		||  cell.foreground != current.foreground
		||  cell.background != current.background
		||  cell.character != ch
		)
			return i;
	}
	return cols;
}

inline
unsigned
TUIOutputBase::count_cheap_eraseable(
	unsigned short row,
	unsigned short col,
	unsigned cols,
	CharacterCell::attribute_type attr
) const {
	return count_cheap(row, col, cols, attr, SPC);
}

inline
unsigned
TUIOutputBase::count_cheap_spaces(
	unsigned short row,
	unsigned short col,
	unsigned cols
) const {
	return count_cheap(row, col, cols, current_attr, SPC);
}

inline
unsigned
TUIOutputBase::count_cheap_repeatable(
	unsigned short row,
	unsigned short col,
	unsigned cols,
	uint32_t ch
) const {
	return count_cheap(row, col, cols, current_attr, ch);
}

inline
void
TUIOutputBase::GotoYX(
	const unsigned short row,
	const unsigned short col
) {
	if (row == cursor_y && col == cursor_x)
		return;
	// Going to the home position is easy.
	if (0 == col && 0 == row) {
		out.CUP();
		cursor_y = cursor_x = 0;
		return;
	}
	// If we are going to the first 3 columns in a row, use newlines and carriage returns to get roughly there.
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
				if (!out.caps.lacks_IND && n <= 3U)
					out.forward_index(n);
				else
				if (n <= 6U)
					out.linefeed(n);
				else
					out.CUD(n);
				cursor_y = row;
			}
		} else
			out.carriage_return();
		cursor_x = 0;
	}
	// If we are (now) in the right column, use index and reverse index to get to the right row.
	if (col == cursor_x) {
		if (row < cursor_y) {
			const unsigned short n(cursor_y - row);
			if (!out.caps.lacks_RI && n <= 3U)
				out.reverse_index(n);
			else
				out.CUU(n);
			cursor_y = row;
		} else
		if (row > cursor_y) {
			const unsigned short n(row - cursor_y);
			if (!out.caps.lacks_IND && n <= 3U)
				out.forward_index(n);
			else
			if (n <= 6U)
				out.linefeed(n);
			else
				out.CUD(n);
			cursor_y = row;
		}
	} else
	// If we are (now) in the right row, use left and right to get to the right column.
	if (row == cursor_y && cursor_x < c.query_w()) {
		if (col < cursor_x) {
			const unsigned short n(cursor_x - col);
			// Optimize going left if the control sequence is longer than just printing enough BS characters.
			if (n <= 6U)
				out.backspace(n);
			else
				out.CUL(n);
			cursor_x = col;
		} else
		if (col > cursor_x) {
			const unsigned short n(col - cursor_x);
			// Optimize going right if the control sequence is longer than just re-printing the actual characters.
			if (n <= 6U && n == count_cheap_narrow(cursor_y, cursor_x, n))
				for (unsigned i(cursor_x); i < col; ++i) {
					TUIDisplayCompositor::DirtiableCell & cell(c.cur_at(cursor_y, i));
					print(cell, false, false /* We checked for no pointer or mark. */);
					cell.untouch();
				}
			else
				out.CUR(n);
			cursor_x = col;
		}
	} else
	// Non-optimized positioning with the ordinary control sequence.
	{
		out.CUP(row + 1U, col + 1U);
		cursor_y = row;
		cursor_x = col;
	}
}

namespace {

inline
void
SGRAttr1(
	const CharacterCell::attribute_type & current_attr,
	const CharacterCell::attribute_type & attr,
	const CharacterCell::attribute_type & mask,
	std::vector<unsigned> & n,
	unsigned n0
) {
	if ((attr & mask) != (current_attr & mask)) {
		if (!(attr & mask))
			n.push_back(20U + n0);
		else
			n.push_back(n0);
	}
}

inline
void
SGRAttr1(
	const CharacterCell::attribute_type & current_attr,
	const CharacterCell::attribute_type & attr,
	const CharacterCell::attribute_type & mask,
	const CharacterCell::attribute_type & unit,
	std::vector<unsigned> & n,
	std::vector<unsigned> & u
) {
	const CharacterCell::attribute_type bits(attr & mask);
	if (bits != (current_attr & mask)) {
		if (!bits)
			n.push_back(24U);
		else
			u.push_back(bits / unit);
	}
}

}

inline
void
TUIOutputBase::SGRAttr (
	const CharacterCell::attribute_type & attr
) {
	if (attr == current_attr) return;
	std::vector<unsigned> n, u;
	if (out.caps.lacks_reverse_off && (current_attr & CharacterCell::INVERSE)) {
		n.push_back(0U);
		current_attr = 0;
	}
	enum {
		BF = CharacterCell::BOLD|CharacterCell::FAINT,
		FE = CharacterCell::FRAME|CharacterCell::ENCIRCLE,
	};
	if ((attr & BF) != (current_attr & BF)) {
		if (current_attr & BF)
			n.push_back(22U);
		if (CharacterCell::BOLD & attr)
			n.push_back(1U);
		if (CharacterCell::FAINT & attr)
			n.push_back(2U);
	}
	if ((attr & FE) != (current_attr & FE)) {
		if (current_attr & FE)
			n.push_back(54U);
		if (CharacterCell::FRAME & attr)
			n.push_back(51U);
		if (CharacterCell::ENCIRCLE & attr)
			n.push_back(52U);
	}
	SGRAttr1(current_attr, attr, CharacterCell::ITALIC, n, 3U);
	if (out.caps.has_extended_underline) {
		SGRAttr1(current_attr, attr, CharacterCell::UNDERLINES, CharacterCell::SIMPLE_UNDERLINE, n, u);
	} else {
		SGRAttr1(current_attr, attr, CharacterCell::UNDERLINES, n, 4U);
	}
	SGRAttr1(current_attr, attr, CharacterCell::BLINK, n, 5U);
	SGRAttr1(current_attr, attr, CharacterCell::INVERSE, n, 7U);
	if (!out.caps.lacks_invisible)
		SGRAttr1(current_attr, attr, CharacterCell::INVISIBLE, n, 8U);
	if (!out.caps.lacks_strikethrough)
		SGRAttr1(current_attr, attr, CharacterCell::STRIKETHROUGH, n, 9U);
	if ((attr & CharacterCell::OVERLINE) != (current_attr & CharacterCell::OVERLINE)) {
		if (attr & CharacterCell::OVERLINE)
			n.push_back(53U);
		else
			n.push_back(55U);
	}
	out.SGRAttribute(n);
	if (!u.empty()) out.SGRAttribute(4U, u);
	current_attr = attr;
}

void
TUIOutputBase::print(
	CharacterCell cell,	///< a copy of the character cell, so that we can alter it
	bool marked,
	bool is_pointer
) {
	fixup(cell, marked, is_pointer);

	unsigned w(width(cell.character));
	if (w < 1U) {
		cell.character = SPC;
		w = 1U;
	}

	SGRFGColour(cell.foreground);
	SGRBGColour(cell.background);
	SGRAttr(cell.attributes);
	out.UTF8(cell.character);
	for (unsigned n(w); n > 0U; --n) {
		++cursor_x;
		if (out.caps.lacks_pending_wrap && cursor_x >= c.query_w()) {
			cursor_x = 0;
			if (cursor_y < c.query_h())
				++cursor_y;
		}
	}
}

void
TUIOutputBase::enter_full_screen_mode(
) {
	if (0 <= tcgetattr_nointr(fd, original_attr)) {
		out.stream().flush();
		tcsetattr_nointr(fd, TCSADRAIN, disable_canonical_software_processing(original_attr));
	}
	if (out.caps.use_DECPrivateMode) {
		out.XTermSaveRestore(true);
		out.XTermAlternateScreenBuffer(!options.no_alternate_screen_buffer);
	}
	out.CUP();
	cursor_y = cursor_x = 0U;
	SGRAttr(0U);
	SGRFGColour(ColourPair::colour_type::default_foreground);
	SGRBGColour(ColourPair::colour_type::default_background);
	// DEC Locator is the less preferable protocol since it does not carry modifier information.
	if (out.caps.use_DECLocator && !out.caps.has_XTerm1006Mouse) {
		out.DECELR(true);
		out.DECSLE(true /*press*/, true);
		out.DECSLE(false /*release*/, true);
	}
	if (out.caps.use_SCOPrivateMode) {
		if (out.caps.has_square_mode)
			out.SquareMode(true);
	}
	if (out.caps.use_DECPrivateMode) {
		out.DECAWM(false);
		// We rely upon erasure to the background colour, not to the screen/default colour.
		if (out.caps.has_DECECM)
			out.DECECM(false);
		out.DECBKM(true);	// We want to be able to distinguish Backspace from Control+Backspace.
		if (out.caps.has_XTerm1006Mouse)
			out.XTermSendAnyMouseEvents();
		else
			out.XTermSendNoMouseEvents();
		out.TeraTermEscapeIsFS(false);	// We want Escape to have its normal function.
		out.XTermDeleteIsDEL(false);	// We want to be able to distinguish Delete from Control+Backspace.
		out.DECCKM(options.cursor_application_mode);
		if (out.caps.use_DECNKM)
			out.DECNKM(options.calculator_application_mode);
		else
			out.DECKPxM(options.calculator_application_mode);
	}
	out.change_cursor_visibility(false);
	out.SCUSR(cursor_attributes, cursor_glyph);
	out.stream().flush();
}

void
TUIOutputBase::exit_full_screen_mode(
) {
	out.SCUSR();
	out.change_cursor_visibility(true);
	if (out.caps.use_DECPrivateMode) {
		if (out.caps.use_DECNKM)
			out.DECNKM(false);
		else
			out.DECKPxM(false);
		out.DECCKM(false);
		out.XTermSendNoMouseEvents();
		out.DECBKM(false);			// Restore the more common Unix convention.
		if (out.caps.has_DECECM)
			out.DECECM(out.caps.initial_DECECM);
		out.DECAWM(true);
	}
	if (out.caps.use_SCOPrivateMode) {
		if (out.caps.has_square_mode)
			out.SquareMode(true);
	}
	if (out.caps.use_DECLocator) {
		out.DECSLE();
		out.DECELR(false);
	}
	SGRBGColour(ColourPair::colour_type::default_background);
	SGRFGColour(ColourPair::colour_type::default_foreground);
	SGRAttr(0U);
	out.CUP();
	cursor_y = cursor_x = 0U;
	if (out.caps.use_DECPrivateMode) {
		out.XTermAlternateScreenBuffer(false);
		out.XTermSaveRestore(false);
	}
	out.stream().flush();
	tcsetattr_nointr(fd, TCSADRAIN, original_attr);
}

TUIOutputBase::TUIOutputBase(
	const TerminalCapabilities & t,
	std::ostream & os,
	int d,
	const TUIOutputBase::Options & o,
	TUIDisplayCompositor & comp
) :
	c(comp),
	options(o),
	out(t, os, true /* C1 is 7-bit aliased */, false /* C1 is not raw 8-bit */),
	fd(d),
	window_resized(true),
	refresh_needed(true),
	update_needed(true),
	cursor_y(0),
	cursor_x(0),
	current(ColourPair::impossible),	// Set an impossible colour, forcing a change.
	current_attr(0U),
	cursor_glyph(CursorSprite::BOX),
	cursor_attributes(CursorSprite::VISIBLE),
	invert_screen(-1),	// Use an impossible value to force an initial update.
	current_attr_unknown(true)
{
	out.stream().flush();
	out.stream().rdbuf()->pubsetbuf(out_buffer, sizeof out_buffer);
	enter_full_screen_mode();
}

TUIOutputBase::~TUIOutputBase()
{
	exit_full_screen_mode();
	out.stream().flush();
	out.stream().rdbuf()->pubsetbuf(nullptr, 0);
}

void
TUIOutputBase::handle_resize_event (
) {
	if (window_resized) {
		window_resized = false;
		struct winsize size;
		if (0 <= tcgetwinsz_nointr(fd, size)) {
			sane(size);
			c.resize(size.ws_row, size.ws_col);
		}
		refresh_needed = true;
		// Some terminals reset attributes after a video mode change (and hence a resize event).
		// So we need to force explicit changes to be sent by setting impossible current values.
		current = ColourPair::impossible;
		current_attr_unknown = true;
		invert_screen = -1;
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
		if (!out.caps.has_square_mode)
			c.touch_width_change_shadows();
		c.repaint_new_to_cur();
		write_changed_cells_to_output();
	}
}

void
TUIOutputBase::suspend_self(
) {
	suspended();
	TemporarilyUnblockSignals u(SIGTSTP, 0);
	killpg(0, SIGTSTP);
}

void
TUIOutputBase::tstp_signal(
) {
// On true kqueue() systems it is too late to do anything here, as the suspension has already happened.
#if defined(__LINUX__) || defined(__linux__)
	suspend_self();
#endif
}

void
TUIOutputBase::suspended(
) {
	exit_full_screen_mode();
}

void
TUIOutputBase::continued(
) {
	enter_full_screen_mode();
	// We don't know what might have happened whilst we were suspended.
	current = ColourPair::impossible;
	current_attr_unknown = true;
	invert_screen = -1;
	invalidate_cur();
	set_update_needed();
}

void
TUIOutputBase::write_changed_cells_to_output()
{
	const ScreenFlags::flag_type f(c.query_screen_flags());
	if ((f & ScreenFlags::INVERTED) != invert_screen) {
		invert_screen = (f & ScreenFlags::INVERTED);
		c.touch_all();
	}
	// Do this once, instead of inside of every call to SGRAttr().
	if (current_attr_unknown) {
		out.SGRAttribute(0);
		current_attr = 0U;
		current_attr_unknown = false;
	}
	const CursorSprite::attribute_type a(c.query_cursor_attributes());
	if (CursorSprite::VISIBLE & a)
		out.change_cursor_visibility(false);
	for (unsigned row(0U); row < c.query_h(); ++row) {
		for (unsigned col(0U); col < c.query_w(); ++col) {
			TUIDisplayCompositor::DirtiableCell & cell(c.cur_at(row, col));
			if (!cell.touched()) continue;
			GotoYX(row, col);
			const unsigned toeol(c.query_w() - col);
			if (3U < toeol
			&&  (out.caps.has_DECECM || !out.caps.initial_DECECM)	// i.e. does not always erase to default colour
			&&  !out.caps.faulty_inverse_erase
			// EL sets erased cells to 0 attributes, by widespread tacit agreement.
			&&  toeol == count_cheap_eraseable(row, col, toeol, 0)
			) {
				out.EL(0U);
				while (col < c.query_w())
					c.cur_at(row, col++).untouch();
				continue;
			}
			const bool marked((c.query_cursor_attributes() & CursorSprite::VISIBLE) && c.is_marked(false /* does not include cursor */, row, col));
			const bool is_pointer((c.query_pointer_attributes() & PointerSprite::VISIBLE) && c.is_pointer(row, col));
			print(cell, marked, is_pointer);
			cell.untouch();
			unsigned n(width(cell.character));
			if (1U < n) {
				--n;
				if (n == count_cheap_spaces(row, col + 1U, n)) {
					while (0U < n && col + 1U < c.query_w()) {
						c.cur_at(row, ++col).untouch();
						--n;
					}
				}
			} else
			if (1U == n) {
				if (3U < toeol
				&&  !out.caps.lacks_REP
				&&  !marked
				&&  !is_pointer
				&&  (!out.caps.faulty_SP_REP || UnicodeCategorization::IsBMP(cell.character))
				) {
					const unsigned r(count_cheap_repeatable(row, col + 1U, toeol - 1U, cell.character));
					if (3U < r) {
						out.REP(r);
						for (unsigned i(0U); i < r; ++i)
							c.cur_at(row, ++col).untouch();
						cursor_x += r;	// We are guaranteed no line wraps by toeol .
					}
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
	out.stream().flush();
}

void
TUIOutputBase::optimize_scroll_up(
	unsigned short rows
) {
	const CursorSprite::attribute_type a(c.query_cursor_attributes());
	if (CursorSprite::VISIBLE & a)
		out.change_cursor_visibility(false);
	GotoYX(0U, 0U);
	const unsigned short capped_rows(rows < c.query_h() ? rows : c.query_h());
	out.reverse_index(capped_rows);
	GotoYX(c.query_cursor_row(), c.query_cursor_col());
	if (CursorSprite::VISIBLE & a)
		out.change_cursor_visibility(true);
	c.scroll_up(capped_rows);
	if ((out.caps.has_DECECM || !out.caps.initial_DECECM)	// i.e. does not always erase to default colour
	&&  !out.caps.faulty_inverse_erase
	) {
		// RI scrolling sets erased cells to 0 attributes, by widespread tacit agreement.
		TUIDisplayCompositor::DirtiableCell spc(SPC, 0, current);
		for (unsigned row(0); row < capped_rows; ++row)
			for (unsigned col(0U), w(c.query_w()); col < w; ++col)
				c.cur_at(row, col) = spc;
	}
}

void
TUIOutputBase::optimize_scroll_down(
	unsigned short rows
) {
	const CursorSprite::attribute_type a(c.query_cursor_attributes());
	if (CursorSprite::VISIBLE & a)
		out.change_cursor_visibility(false);
	GotoYX(c.query_h() - 1U, 0U);
	const unsigned short capped_rows(rows < c.query_h() ? rows : c.query_h());
	out.forward_index(capped_rows);
	GotoYX(c.query_cursor_row(), c.query_cursor_col());
	if (CursorSprite::VISIBLE & a)
		out.change_cursor_visibility(true);
	c.scroll_down(capped_rows);
	if ((out.caps.has_DECECM || !out.caps.initial_DECECM)	// i.e. does not always erase to default colour
	&&  !out.caps.faulty_inverse_erase
	) {
		// IND scrolling sets erased cells to 0 attributes, by widespread tacit agreement.
		TUIDisplayCompositor::DirtiableCell spc(SPC, 0, current);
		for (unsigned row(0); row < capped_rows; ++row)
			for (unsigned col(0U), w(c.query_w()); col < w; ++col)
				c.cur_at(c.query_h() - 1U - row, col) = spc;
	}
}
