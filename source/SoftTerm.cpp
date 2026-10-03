/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <algorithm>
#include <cstdio>
#include <stdint.h>
#include "CharacterCell.h"
#include "SoftTerm.h"
#include "UnicodeClassification.h"
#include "UnicodeKeyboard.h"

#include <iostream>	// for debugging

/* Constructor and destructor ***********************************************
// **************************************************************************
*/

SoftTerm::SoftTerm(
	SoftTerm::ScreenBuffer & s,
	SoftTerm::KeyboardBuffer & k,
	SoftTerm::MouseBuffer & m,
	const SoftTerm::Setup & i
) :
	ECMA48Decoder::ECMA48ControlSequenceSink(),
	utf8_decoder(*this),
	ecma48_decoder(*this, ECMA48Decoder::Options(false /* no control strings */, true /* permit cancel */, true /* permit 7-bit extensions*/, false /* no Interix shift state */, false /* no RXVT final $ in CSI bodge */, false /* no Linux function keys */)),
	screen(s),
	keyboard(k),
	mouse(m),
	scroll_margin(i.w,i.h),
	display_margin(i.w,i.h),
	scrolling(true),
	overstrike(true),
	square(true),
	altbuffer(false),
	no_clear_screen_on_column_change(false),
	pan_is_scroll(i.pan_is_scroll),
	attributes(0),
	colour(ColourPair::def),
	saved_colour(ColourPair::def),
	cursor_type(CursorSprite::BLOCK),
	cursor_attributes(CursorSprite::VISIBLE|CursorSprite::BLINK),
	send_DECLocator(false),
	send_XTermMouse(false),
	invert_screen(i.inverted),
	last_printable_character(NUL)
{
	Resize(display_origin.x + display_margin.w, display_origin.y + display_margin.h);
	SetRegularHorizontalTabstops(8U);
	ClearAllVerticalTabstops();
	UpdateCursorType();
	UpdatePointerType();
	UpdateScreenFlags();
	Home();
	ClearDisplay();
	keyboard.SetBackspaceIsBS(false);
	keyboard.SetEscapeIsFS(false);
	keyboard.SetSCOFunctionKeys(true);
	keyboard.SetDECFunctionKeys(true);
	keyboard.SetTekenFunctionKeys(true);
}

SoftTerm::~SoftTerm()
{
	// For security, we erase several sources of information about old terminal sessions.
	Resize(80U, 25U);
	Home();
	ClearDisplay();
}

SoftTerm::mode::mode() :
	automatic_right_margin(true),
	background_colour_erase(true),
	origin(false),
	left_right_margins(false)
{
}

/* Top-level control functions **********************************************
// **************************************************************************
*/

void
SoftTerm::Resize(
	coordinate columns,
	coordinate rows
) {
	if (columns)  {
		if (display_origin.x >= columns)
			display_origin.x = 0U;
		display_margin.w = columns - display_origin.x;
	} else
		columns = display_origin.x + display_margin.w;
	if (rows) {
		if (display_origin.y >= rows)
			display_origin.y = 0U;
		display_margin.h = rows - display_origin.y;
	} else
		rows = display_origin.y + display_margin.h;

	screen.SetSize(columns, rows);
	keyboard.ReportSize(columns, rows);

	if (scroll_origin.y >= rows)
		scroll_origin.y = display_origin.y;
	if (scroll_origin.y + scroll_margin.h > rows)
		scroll_margin.h = rows - scroll_origin.y;
	if (scroll_origin.x >= columns)
		scroll_origin.x = display_origin.x;
	if (scroll_origin.x + scroll_margin.w > columns)
		scroll_margin.w = columns - scroll_origin.x;

	active_cursor.advance_pending = false;	// per DEC EL-00070-D section D.4
	if (active_cursor.x >= columns) active_cursor.x = columns - 1U;
	if (active_cursor.y >= rows) active_cursor.y = rows - 1U;
	UpdateCursorPos();
}

/* Control sequence arguments ***********************************************
// **************************************************************************
*/

namespace {

inline SoftTerm::coordinate TranslateFromDECCoordinates(SoftTerm::coordinate n) { return n - 1U; }
inline SoftTerm::coordinate TranslateToDECCoordinates(SoftTerm::coordinate n) { return n + 1U; }

}

/* Editing ******************************************************************
// **************************************************************************
*/

CharacterCell
SoftTerm::ErasureCell(uint32_t c)
{
	// Erased cells have no attributes set on.
	return CharacterCell(c, 0U, active_modes.background_colour_erase ? colour : ColourPair::erased);
}

void
SoftTerm::ClearDisplay(uint32_t c)
{
	// Erasure ignores margins.
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const ScreenBuffer::coordinate s(stride * display_origin.y + display_origin.x);
	const ScreenBuffer::coordinate l(stride * display_margin.h);
	screen.WriteNCells(s, l, ErasureCell(c));
}

void
SoftTerm::ClearLine()
{
	// Erasure ignores margins.
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const ScreenBuffer::coordinate s(stride * active_cursor.y + display_origin.x);
	screen.WriteNCells(s, display_margin.w, ErasureCell());
}

void
SoftTerm::ClearToEOD()
{
	// Erasure ignores margins.
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const ScreenBuffer::coordinate rows(ScreenBuffer::coordinate(display_margin.h) + display_origin.y);
	const ScreenBuffer::coordinate s(stride * active_cursor.y + active_cursor.x);
	const ScreenBuffer::coordinate e(stride * rows);
	if (s < e) {
		const ScreenBuffer::coordinate l(e - s);
		screen.WriteNCells(s, l, ErasureCell());
	}
}

void
SoftTerm::ClearToEOL()
{
	// Erasure ignores margins.
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const ScreenBuffer::coordinate s(stride * active_cursor.y + active_cursor.x);
	if (active_cursor.x < stride)
		screen.WriteNCells(s, stride - active_cursor.x, ErasureCell());
}

void
SoftTerm::ClearFromBOD()
{
	// Erasure ignores margins.
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const ScreenBuffer::coordinate s(stride * display_origin.y + display_origin.x);
	if (display_origin.y <= active_cursor.y) {
		const ScreenBuffer::coordinate l(stride * (active_cursor.y - display_origin.y) + active_cursor.x + 1U);
		screen.WriteNCells(s, l, ErasureCell());
	}
}

void
SoftTerm::ClearFromBOL()
{
	// Erasure ignores margins.
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const ScreenBuffer::coordinate s(stride * active_cursor.y + display_origin.x);
	if (display_origin.x <= active_cursor.x)
		screen.WriteNCells(s, active_cursor.x - display_origin.x + 1U, ErasureCell());
}

void
SoftTerm::EraseCharacters(argument_type n)
{
	// Erasure ignores margins.
	// DEC EL-00070-D section D.4 says that this moves the cursor and thus affects pending advance.
	// It does not.
	const coordinate right_margin(display_origin.x + display_margin.w - 1U);
	if (active_cursor.x > right_margin) return;
	if (n > right_margin - active_cursor.x + 1U) n = right_margin - active_cursor.x + 1U;
	if (0U >= n) return;
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const ScreenBuffer::coordinate s(stride * active_cursor.y + active_cursor.x);
	screen.WriteNCells(s, n, ErasureCell());
}

void
SoftTerm::DeleteCharacters(argument_type n)
{
	// Deletion always operates only inside the margins.
	// DEC EL-00070-D section D.4 says that this moves the cursor and thus affects pending advance.
	// It does not.
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	if (active_cursor.x > right_margin) return;
	if (n > right_margin - active_cursor.x + 1U) n = right_margin - active_cursor.x + 1U;
	if (0U >= n) return;
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const ScreenBuffer::coordinate s(stride * active_cursor.y + active_cursor.x);
	const ScreenBuffer::coordinate e(stride * active_cursor.y + right_margin + 1U);
	screen.ScrollUp(s, e, n, ErasureCell());
}

void
SoftTerm::InsertCharacters(argument_type n)
{
	// Insertion always operates only inside the margins.
	// DEC EL-00070-D section D.4 says that this moves the cursor and thus affects pending advance.
	// It does not.
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	if (active_cursor.x > right_margin) return;
	if (n > right_margin - active_cursor.x + 1U) n = right_margin - active_cursor.x + 1U;
	if (0U >= n) return;
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const ScreenBuffer::coordinate s(stride * active_cursor.y + active_cursor.x);
	const ScreenBuffer::coordinate e(stride * active_cursor.y + right_margin + 1U);
	screen.ScrollDown(s, e, n, ErasureCell());
}

void
SoftTerm::DeleteLinesInScrollAreaAt(coordinate top, argument_type n)
{
	// DEC EL-00070-D section D.4 says that this moves the cursor and thus affects pending advance.
	// It does not.
	const coordinate bottom_margin(scroll_origin.y + scroll_margin.h - 1U);
	if (top > bottom_margin) return;
	if (n > bottom_margin - top + 1U) n = bottom_margin - top + 1U;
	if (0U >= n) return;
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	const coordinate left_margin(scroll_origin.x);
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	if (left_margin != 0U || right_margin != stride - 1U) {
		const coordinate w(right_margin - left_margin + 1U);
		for (ScreenBuffer::coordinate r(top); r + n <= bottom_margin; ++r) {
			const ScreenBuffer::coordinate d(stride * r + left_margin);
			const ScreenBuffer::coordinate s(stride * (r + n) + left_margin);
			screen.CopyNCells(d, s, w);
		}
		for (ScreenBuffer::coordinate r(bottom_margin - n + 1U); r <= bottom_margin; ++r) {
			const ScreenBuffer::coordinate d(stride * r + left_margin);
			screen.WriteNCells(d, w, ErasureCell());
		}
	} else {
		const ScreenBuffer::coordinate s(stride * top);
		const ScreenBuffer::coordinate e(stride * (bottom_margin + 1U));
		const ScreenBuffer::coordinate l(stride * n);
		screen.ScrollUp(s, e, l, ErasureCell());
	}
}

void
SoftTerm::InsertLinesInScrollAreaAt(coordinate top, argument_type n)
{
	// DEC EL-00070-D section D.4 says that this moves the cursor and thus affects pending advance.
	// It does not.
	const coordinate bottom_margin(scroll_origin.y + scroll_margin.h - 1U);
	if (top > bottom_margin) return;
	if (n > bottom_margin - top + 1U) n = bottom_margin - top + 1U;
	if (0U >= n) return;
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	const coordinate left_margin(scroll_origin.x);
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	if (left_margin != 0U || right_margin != stride - 1U) {
		const coordinate w(right_margin - left_margin + 1U);
		for (ScreenBuffer::coordinate r(bottom_margin); r >= top + n; --r) {
			const ScreenBuffer::coordinate d(stride * r + left_margin);
			const ScreenBuffer::coordinate s(stride * (r - n) + left_margin);
			screen.CopyNCells(d, s, w);
		}
		for (ScreenBuffer::coordinate r(top + n); r-- > top; ) {
			const ScreenBuffer::coordinate d(stride * r + left_margin);
			screen.WriteNCells(d, w, ErasureCell());
		}
	} else {
		const ScreenBuffer::coordinate s(stride * top);
		const ScreenBuffer::coordinate e(stride * (bottom_margin + 1U));
		const ScreenBuffer::coordinate l(stride * n);
		screen.ScrollDown(s, e, l, ErasureCell());
	}
}

void
SoftTerm::DeleteColumnsInScrollAreaAt(coordinate left, argument_type n)
{
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	if (left > right_margin) return;
	if (n > right_margin - left + 1U) n = right_margin - left + 1U;
	if (0U >= n) return;
	const coordinate bottom_margin(scroll_origin.y + scroll_margin.h - 1U);
	const coordinate top_margin(scroll_origin.y);
	const coordinate w(right_margin - left - n);
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	for (ScreenBuffer::coordinate r(top_margin); r <= bottom_margin; ++r) {
		const ScreenBuffer::coordinate d(stride * r + left);
		const ScreenBuffer::coordinate s(stride * r + left + n);
		screen.CopyNCells(d, s, w);
		screen.WriteNCells(d + w, n, ErasureCell());
	}
}

void
SoftTerm::InsertColumnsInScrollAreaAt(coordinate left, argument_type n)
{
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	if (left > right_margin) return;
	if (n > right_margin - left + 1U) n = right_margin - left + 1U;
	if (0U >= n) return;
	const coordinate bottom_margin(scroll_origin.y + scroll_margin.h - 1U);
	const coordinate top_margin(scroll_origin.y);
	const coordinate w(scroll_margin.w - left - n);
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	for (ScreenBuffer::coordinate r(top_margin); r <= bottom_margin; ++r) {
		const ScreenBuffer::coordinate d(stride * r + left + n);
		const ScreenBuffer::coordinate s(stride * r + left);
		screen.CopyNCells(d, s, w);
		screen.WriteNCells(s, n, ErasureCell());
	}
}

void
SoftTerm::EraseInDisplay()
{
	// DEC EL-00070-D section D.4 says that this moves the cursor and thus affects pending advance.
	// It does not.
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		switch (GetArgZeroIfEmpty(i)) {
			case 0:	ClearToEOD(); break;
			case 1:	ClearFromBOD(); break;
			case 2:	ClearDisplay(); break;
			// 3 is a Linux kernel terminal emulator extension introduced in 2011.
			// It clears the display and also any off-screen buffers.
			// The original xterm extension by Stephen P. Wall from 1999-06-12, and the PuTTY extension by Jacob Nevins in 2006, both clear only the off-screen buffers.
			// We follow the originals.
			case 3:	break;
		}
	}
}

void
SoftTerm::EraseInLine()
{
	// DEC EL-00070-D section D.4 says that this moves the cursor and thus affects pending advance.
	// It does not.
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		switch (GetArgZeroIfEmpty(i)) {
			case 0:	ClearToEOL(); break;
			case 1:	ClearFromBOL(); break;
			case 2:	ClearLine(); break;
		}
	}
}

void
SoftTerm::InsertLines(argument_type n)
{
	// Insertion always operates only inside the margins.
	InsertLinesInScrollAreaAt(active_cursor.y, n);
}

void
SoftTerm::DeleteLines(argument_type n)
{
	// Deletion always operates only inside the margins.
	DeleteLinesInScrollAreaAt(active_cursor.y, n);
}

void
SoftTerm::ScrollDown(argument_type n)
{
	// Scrolling always operates only inside the margins.
	InsertLinesInScrollAreaAt(scroll_origin.y, n);
}

void
SoftTerm::ScrollUp(argument_type n)
{
	// Scrolling always operates only inside the margins.
	DeleteLinesInScrollAreaAt(scroll_origin.y, n);
}

void
SoftTerm::ScrollLeft(argument_type n)
{
	// Scrolling always operates only inside the margins.
	DeleteColumnsInScrollAreaAt(scroll_origin.x, n);
}

void
SoftTerm::ScrollRight(argument_type n)
{
	// Scrolling always operates only inside the margins.
	InsertColumnsInScrollAreaAt(scroll_origin.x, n);
}

void
SoftTerm::PanUp(argument_type n)
{
	if (pan_is_scroll) ScrollUp(n);
}

void
SoftTerm::PanDown(argument_type n)
{
	if (pan_is_scroll) ScrollDown(n);
}

/* Tabulation ***************************************************************
// **************************************************************************
*/

void
SoftTerm::CursorTabulationControl()
{
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		switch (GetArgZeroIfEmpty(i)) {
			case 0:	SetHorizontalTabstopAt(active_cursor.x, true); break;
			case 1:	SetVerticalTabstopAt(active_cursor.y, true); break;
			case 2:	SetHorizontalTabstopAt(active_cursor.x, false); break;
			case 3:	SetVerticalTabstopAt(active_cursor.y, false); break;
			case 4: // Effectively the same as ...
			case 5:	ClearAllHorizontalTabstops(); break;
			case 6:	ClearAllVerticalTabstops(); break;
		}
	}
}

void
SoftTerm::DECCursorTabulationControl()
{
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		switch (GetArgZeroIfEmpty(i)) {
/* DECST8C */		case 5: SetRegularHorizontalTabstops(8U); break;
		}
	}
}

void
SoftTerm::TabClear()
{
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		switch (GetArgZeroIfEmpty(i)) {
			case 0:	SetHorizontalTabstopAt(active_cursor.x, false); break;
			case 1:	SetHorizontalTabstopAt(active_cursor.x, false); break;
			case 2: // Effectively the same as ...
			case 3:	ClearAllHorizontalTabstops(); break;
			case 4:	ClearAllVerticalTabstops(); break;
			case 5:	ClearAllHorizontalTabstops(); ClearAllVerticalTabstops(); break;
		}
	}
}

void
SoftTerm::HorizontalTab(
	argument_type n,
	bool apply_margins
) {
	// DEC VTs are supposed to cancel pending wrap on TAB.
	// It is reported that the 420 and 510 actually do not, in practice.
	active_cursor.advance_pending = false;
	const coordinate right_margin(apply_margins ? scroll_origin.x + scroll_margin.w - 1U : display_origin.x + display_margin.w - 1U);
	if (active_cursor.x < right_margin && n) {
		do {
			if (IsHorizontalTabstopAt(++active_cursor.x)) {
				if (!n) break;
				--n;
			}
		} while (active_cursor.x < right_margin && n);
		UpdateCursorPos();
	}
}

void
SoftTerm::BackwardsHorizontalTab(
	argument_type n,
	bool apply_margins
) {
	active_cursor.advance_pending = false;
	const coordinate left_margin(apply_margins ? scroll_origin.x : display_origin.x);
	if (active_cursor.x > left_margin && n) {
		do {
			if (IsHorizontalTabstopAt(--active_cursor.x)) {
				if (!n) break;
				--n;
			}
		} while (active_cursor.x > left_margin && n);
		UpdateCursorPos();
	}
}

void
SoftTerm::VerticalTab(
	argument_type n,
	bool apply_margins
) {
	active_cursor.advance_pending = false;	// per DEC EL-00070-D section D.4
	const coordinate bottom_margin(apply_margins ? scroll_origin.y + scroll_margin.h - 1U : display_origin.y + display_margin.h - 1U);
	if (active_cursor.y < bottom_margin && n) {
		do {
			if (IsVerticalTabstopAt(++active_cursor.y)) {
				if (!n) break;
				--n;
			}
		} while (active_cursor.y < bottom_margin && n);
		UpdateCursorPos();
	}
}

void
SoftTerm::SetHorizontalTabstop()
{
	SetHorizontalTabstopAt(active_cursor.x, true);
}

void
SoftTerm::SetRegularHorizontalTabstops(
	argument_type n
) {
	for (argument_type p(0U); p < 256U; ++p)
		SetHorizontalTabstopAt(p, 0U == (p % n));
}

void
SoftTerm::ClearAllHorizontalTabstops()
{
	for (argument_type p(0U); p < 256U; ++p)
		SetHorizontalTabstopAt(p, false);
}

void
SoftTerm::ClearAllVerticalTabstops()
{
	for (argument_type p(0U); p < 256U; ++p)
		SetVerticalTabstopAt(p, false);
}

/* Cursor motion ************************************************************
// **************************************************************************
*/

void
SoftTerm::UpdateCursorPos()
{
	screen.SetCursorPos(active_cursor.x, active_cursor.y);
}

void
SoftTerm::UpdateCursorType()
{
	screen.SetCursorType(cursor_type, cursor_attributes);
}

void
SoftTerm::UpdatePointerType()
{
	const PointerSprite::attribute_type pointer_attributes(send_DECLocator || send_XTermMouse ? PointerSprite::VISIBLE : 0);
	screen.SetPointerType(pointer_attributes);
}

void
SoftTerm::UpdateScreenFlags()
{
	const ScreenFlags::flag_type screen_flags(invert_screen ? ScreenFlags::INVERTED : 0);
	screen.SetScreenFlags(screen_flags);
}

void
SoftTerm::SaveCursor()
{
	saved_cursor = active_cursor;
}

void
SoftTerm::RestoreCursor()
{
	active_cursor = saved_cursor;
	UpdateCursorPos();
}

bool
SoftTerm::WillWrap()
{
	// Normal advance always operates only inside the margins.
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	return active_cursor.x >= right_margin && active_modes.automatic_right_margin;
}

void
SoftTerm::Advance()
{
	active_cursor.advance_pending = false;
	// Normal advance always operates only inside the margins.
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	if (active_cursor.x < right_margin) {
		++active_cursor.x;
		UpdateCursorPos();
	} else if (active_modes.automatic_right_margin) {
		const coordinate left_margin(scroll_origin.x);
		const coordinate bottom_margin(scroll_origin.y + scroll_margin.h - 1U);
		active_cursor.x = left_margin;
		if (active_cursor.y < bottom_margin)
			++active_cursor.y;
		else if (scrolling)
			ScrollUp(1U);
		UpdateCursorPos();
	}
}

void
SoftTerm::ClearPendingAdvance()
{
	if (active_cursor.advance_pending) {
		if (WillWrap()) Advance();
		active_cursor.advance_pending = false;
	}
}

void
SoftTerm::AdvanceOrPend()
{
	active_cursor.advance_pending = WillWrap();
	if (!active_cursor.advance_pending) Advance();
}

void
SoftTerm::CarriageReturnNoUpdate()
{
	active_cursor.advance_pending = false;	// per DEC EL-00070-D section D.4
	// Normal return always operates only inside the margins.
	const coordinate left_margin(scroll_origin.x);
	active_cursor.x = left_margin;
}

void
SoftTerm::CarriageReturn()
{
	CarriageReturnNoUpdate();
	UpdateCursorPos();
}

void
SoftTerm::Home()
{
	active_cursor.advance_pending = false;	// per DEC EL-00070-D section D.4
	if (active_modes.origin)
		active_cursor = scroll_origin;
	else
		active_cursor = display_origin;
	UpdateCursorPos();
}

void
SoftTerm::GotoX(argument_type n)
{
	active_cursor.advance_pending = false;	// per DEC EL-00070-D section D.4
	const coordinate columns(active_modes.origin ? scroll_margin.w : display_margin.w);
	if (n > columns) n = columns;
	n += (active_modes.origin ? scroll_origin.x : display_origin.x);
	active_cursor.x = TranslateFromDECCoordinates(n);
	UpdateCursorPos();
}

void
SoftTerm::GotoY(argument_type n)
{
	active_cursor.advance_pending = false;	// per DEC EL-00070-D section D.4
	const coordinate rows(active_modes.origin ? scroll_margin.h : display_margin.h);
	if (n > rows) n = rows;
	n += (active_modes.origin ? scroll_origin.y : display_origin.y);
	active_cursor.y = TranslateFromDECCoordinates(n);
	UpdateCursorPos();
}

void
SoftTerm::GotoYX(argument_type n, argument_type m)
{
	active_cursor.advance_pending = false;	// per DEC EL-00070-D section D.4
	const coordinate columns(active_modes.origin ? scroll_margin.w : display_margin.w);
	const coordinate rows(active_modes.origin ? scroll_margin.h : display_margin.h);
	if (n > rows) n = rows;
	if (m > columns) m = columns;
	n += (active_modes.origin ? scroll_origin.y : display_origin.y);
	m += (active_modes.origin ? scroll_origin.x : display_origin.x);
	active_cursor.y = TranslateFromDECCoordinates(n);
	active_cursor.x = TranslateFromDECCoordinates(m);
	UpdateCursorPos();
}

void
SoftTerm::SetTopBottomMargins()
{
	coordinate new_top_margin(TranslateFromDECCoordinates(GetArgOneIfZeroThisIfEmpty(0U, TranslateToDECCoordinates(0U))));
	coordinate new_bottom_margin(TranslateFromDECCoordinates(GetArgThisIfZeroOrEmpty(1U, TranslateToDECCoordinates(display_margin.h - 1U))));
	if (new_top_margin < new_bottom_margin) {
		scroll_origin.y = display_origin.y + new_top_margin;
		scroll_margin.h = new_bottom_margin - new_top_margin + 1U;
		Home();
	}
}

void
SoftTerm::SetLeftRightMargins()
{
	if (!active_modes.left_right_margins) return;
	coordinate new_left_margin(TranslateFromDECCoordinates(GetArgOneIfZeroThisIfEmpty(0U, TranslateToDECCoordinates(0U))));
	coordinate new_right_margin(TranslateFromDECCoordinates(GetArgThisIfZeroOrEmpty(1U, TranslateToDECCoordinates(display_margin.w - 1U))));
	if (new_left_margin < new_right_margin) {
		scroll_origin.x = display_origin.x + new_left_margin;
		scroll_margin.w = new_right_margin - new_left_margin + 1U;
		Home();
	}
}

void
SoftTerm::ResetMargins()
{
	scroll_origin = display_origin;
	scroll_margin = display_margin;
}

void
SoftTerm::CursorUp(
	argument_type n,
	bool scroll_at_top
) {
	active_cursor.advance_pending = false;	// per DEC EL-00070-D section D.4
	const coordinate top_margin(scroll_origin.y);
	if (active_cursor.y < top_margin) {
		const coordinate display_top(display_origin.y);
		if (active_cursor.y >= display_top) {
			const coordinate l(active_cursor.y - display_top);
			if (l > 0U) {
				if (l >= n) {
					active_cursor.y -= n;
					n = 0U;
				} else {
					n -= l;
					active_cursor.y = display_top;
				}
				UpdateCursorPos();
			}
		} else {
			active_cursor.y = display_top;
			UpdateCursorPos();
		}
	} else {
		const coordinate l(active_cursor.y - top_margin);
		if (l > 0U) {
			if (l >= n) {
				active_cursor.y -= n;
				n = 0U;
			} else {
				n -= l;
				active_cursor.y = top_margin;
			}
			UpdateCursorPos();
		}
		if (n > 0U && scroll_at_top)
			ScrollDown(n);
	}
}

void
SoftTerm::CursorDown(
	argument_type n,
	bool scroll_at_bottom
) {
	active_cursor.advance_pending = false;	// per DEC EL-00070-D section D.4
	const coordinate bottom_margin(scroll_origin.y + scroll_margin.h - 1U);
	if (active_cursor.y > bottom_margin) {
		const coordinate display_bottom(display_origin.y + display_margin.h - 1U);
		if (active_cursor.y <= display_bottom) {
			const coordinate l(display_bottom - active_cursor.y);
			if (l > 0U) {
				if (l >= n) {
					active_cursor.y += n;
					n = 0U;
				} else {
					n -= l;
					active_cursor.y = display_bottom;
				}
				UpdateCursorPos();
			}
		} else {
			active_cursor.y = display_bottom;
			UpdateCursorPos();
		}
	} else {
		const coordinate l(bottom_margin - active_cursor.y);
		if (l > 0U) {
			if (l >= n) {
				active_cursor.y += n;
				n = 0U;
			} else {
				n -= l;
				active_cursor.y = bottom_margin;
			}
			UpdateCursorPos();
		}
		if (n > 0U && scroll_at_bottom)
			ScrollUp(n);
	}
}

void
SoftTerm::CursorLeft(
	argument_type n,
	bool scroll_at_left
) {
	active_cursor.advance_pending = false;	// per DEC EL-00070-D section D.4
	const coordinate left_margin(scroll_origin.x);
	if (active_cursor.x < left_margin) {
		const coordinate display_left(display_origin.x);
		if (active_cursor.x >= display_left) {
			const coordinate l(active_cursor.x - display_left);
			if (l > 0U) {
				if (l >= n) {
					active_cursor.x -= n;
					n = 0U;
				} else {
					n -= l;
					active_cursor.x = display_left;
				}
				UpdateCursorPos();
			}
		} else {
			active_cursor.x = display_left;
			UpdateCursorPos();
		}
	} else {
		const coordinate l(active_cursor.x - left_margin);
		if (l > 0U) {
			if (l >= n) {
				active_cursor.x -= n;
				n = 0U;
			} else {
				n -= l;
				active_cursor.x = left_margin;
			}
			UpdateCursorPos();
		}
		if (n > 0U && scroll_at_left)
			ScrollRight(n);
	}
}

void
SoftTerm::CursorRight(
	argument_type n,
	bool scroll_at_right
) {
	active_cursor.advance_pending = false;	// per DEC EL-00070-D section D.4
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	if (active_cursor.x > right_margin) {
		const coordinate display_right(display_origin.x + display_margin.w - 1U);
		if (active_cursor.x <= display_right) {
			const coordinate l(display_right - active_cursor.x);
			if (l > 0U) {
				if (l >= n) {
					active_cursor.x += n;
					n = 0U;
				} else {
					n -= l;
					active_cursor.x = display_right;
				}
				UpdateCursorPos();
			}
		} else {
			active_cursor.x = display_right;
			UpdateCursorPos();
		}
	} else {
		const coordinate l(right_margin - active_cursor.x);
		if (l > 0U) {
			if (l >= n) {
				active_cursor.x += n;
				n = 0U;
			} else {
				n -= l;
				active_cursor.x = right_margin;
			}
			UpdateCursorPos();
		}
		if (n > 0U && scroll_at_right)
			ScrollLeft(n);
	}
}

/* Colours, modes, and attributes *******************************************
// **************************************************************************
*/

void
SoftTerm::SetAttributes()
{
	MinimumOneArg();
	CharacterCell::attribute_type turnoff(0U), flipon(0U);
	bool fg_touched(false), bg_touched(false);
	CharacterCell::colour_type fg, bg;
	SetAttributes(0U, turnoff, flipon, fg_touched, fg, bg_touched, bg);
	attributes &= ~turnoff;
	attributes |= flipon;
	if (fg_touched) colour.foreground = fg;
	if (bg_touched) colour.background = bg;
}

namespace {

inline
void
on (
	CharacterCell::attribute_type & turnoff,
	CharacterCell::attribute_type & flipon,
	CharacterCell::attribute_type bits
) {
	turnoff &= ~bits;
	flipon |= bits;
}

inline
void
off (
	CharacterCell::attribute_type & turnoff,
	CharacterCell::attribute_type & flipon,
	CharacterCell::attribute_type bits
) {
	turnoff |= bits;
	flipon &= ~bits;
}

}

void
SoftTerm::SetAttributes(
	std::size_t start,
	CharacterCell::attribute_type & turnoff,
	CharacterCell::attribute_type & flipon,
	bool & fg_touched,
	CharacterCell::colour_type & fg,
	bool & bg_touched,
	CharacterCell::colour_type & bg
) {
	for (std::size_t i(start); i < QueryArgCount(); ++i) {
		const unsigned attr(GetArgZeroIfEmpty(i, 0U));
		switch (attr) {
			default:
#if defined(DEBUG)
				std::clog << "Unknown attribute in SGR sequence : " << attr << "\n";
#endif
				break;
			case 0U:
				off(turnoff, flipon, -static_cast<CharacterCell::attribute_type>(1));
				fg = CharacterCell::colour_type::default_foreground;
				fg_touched = true;
				bg = CharacterCell::colour_type::default_background;
				bg_touched = true;
				break;
			case 1U:	on(turnoff, flipon, CharacterCell::BOLD); break;
			case 2U:	on(turnoff, flipon, CharacterCell::FAINT); break;
			case 3U:	on(turnoff, flipon, CharacterCell::ITALIC); break;
			case 4U:
			{
				const unsigned style(GetArgZeroIfEmpty(i, 1U));
				off(turnoff, flipon, CharacterCell::UNDERLINES);
				switch (style) {
					case 0U:
					case 1U:	on(turnoff, flipon, CharacterCell::SIMPLE_UNDERLINE); break;
					case 2U:	on(turnoff, flipon, CharacterCell::DOUBLE_UNDERLINE); break;
					default:
					case 3U:	on(turnoff, flipon, CharacterCell::CURLY_UNDERLINE); break;
					case 4U:	on(turnoff, flipon, CharacterCell::DOTTED_UNDERLINE); break;
					case 5U:	on(turnoff, flipon, CharacterCell::DASHED_UNDERLINE); break;
					case 6U:	on(turnoff, flipon, CharacterCell::LDASHED_UNDERLINE); break;
					case 7U:	on(turnoff, flipon, CharacterCell::LLDASHED_UNDERLINE); break;
					case 8U:	on(turnoff, flipon, CharacterCell::LDOTTED_UNDERLINE); break;
					case 9U:	on(turnoff, flipon, CharacterCell::LLDOTTED_UNDERLINE); break;
					case 10U:	on(turnoff, flipon, CharacterCell::LCURLY_UNDERLINE); break;
				}
				break;
			}
			case 5U:	on(turnoff, flipon, CharacterCell::BLINK); break;
			case 7U:	on(turnoff, flipon, CharacterCell::INVERSE); break;
			case 8U:	on(turnoff, flipon, CharacterCell::INVISIBLE); break;
			case 9U:	on(turnoff, flipon, CharacterCell::STRIKETHROUGH); break;
			case 21U:	off(turnoff, flipon, CharacterCell::UNDERLINES); on(turnoff, flipon, CharacterCell::DOUBLE_UNDERLINE); break;
			case 22U:	off(turnoff, flipon, CharacterCell::BOLD|CharacterCell::FAINT); break;
			case 23U:	off(turnoff, flipon, CharacterCell::ITALIC); break;
			case 24U:	off(turnoff, flipon, CharacterCell::UNDERLINES); break;
			case 25U:	off(turnoff, flipon, CharacterCell::BLINK); break;
			case 27U:	off(turnoff, flipon, CharacterCell::INVERSE); break;
			case 28U:	off(turnoff, flipon, CharacterCell::INVISIBLE); break;
			case 29U:	off(turnoff, flipon, CharacterCell::STRIKETHROUGH); break;
			case 30U: case 31U: case 32U: case 33U:
			case 34U: case 35U: case 36U: case 37U:
					fg = Map16Colour(attr -  30U); fg_touched = true; break;
			case 39U:	fg = ColourPair::colour_type::default_foreground; fg_touched = true; break;
			case 40U: case 41U: case 42U: case 43U:
			case 44U: case 45U: case 46U: case 47U:
					bg = Map16Colour(attr -  40U); bg_touched = true; break;
			case 49U:	bg = ColourPair::colour_type::default_background; bg_touched = true; break;
			case 51U:	on(turnoff, flipon, CharacterCell::FRAME); break;
			case 52U:	on(turnoff, flipon, CharacterCell::ENCIRCLE); break;
			case 53U:	on(turnoff, flipon, CharacterCell::OVERLINE); break;
			case 54U:	off(turnoff, flipon, CharacterCell::FRAME|CharacterCell::ENCIRCLE); break;
			case 55U:	off(turnoff, flipon, CharacterCell::OVERLINE); break;
			case 90U: case 91U: case 92U: case 93U:
			case 94U: case 95U: case 96U: case 97U:
					fg = Map16Colour(attr -  90U + 8U); fg_touched = true; break;
			case 100U: case 101U: case 102U: case 103U:
			case 104U: case 105U: case 106U: case 107U:
					bg = Map16Colour(attr - 100U + 8U); bg_touched = true; break;
			case 38U: case 48U:
			{
				if (HasNoSubArgsFrom(i))
					CollapseArgsToSubArgs(i);
				ColourPair::colour_type & ground(38U == attr ? fg : bg);
				bool & touched(38U == attr ? fg_touched : bg_touched);
				if (5U == GetArgZeroIfEmpty(i, 1U)) {
					ground = Map256Colour(GetArgZeroIfEmpty(i, 2U) % 256U);
					touched = true;
				} else
				if (2U == GetArgZeroIfEmpty(i, 1U)) {
					// ISO 8613-6/ITU T.416 section 13.1.8 has a colour space in sub-parameter 2, which is not implemented.
					// Parameter 6 has no meaning per the standard, and parameters 7 and 8 (tolerance value and space) are not implemented.
					if (5U != QuerySubArgCount(i))
						ground = MapTrueColour(GetArgZeroIfEmpty(i, 3U) % 256U,GetArgZeroIfEmpty(i, 4U) % 256U,GetArgZeroIfEmpty(i, 5U) % 256U);
					else
						// A common error is to omit the colour space, which we detect heuristically by a too-short-by-one sequence length.
						ground = MapTrueColour(GetArgZeroIfEmpty(i, 2U) % 256U,GetArgZeroIfEmpty(i, 3U) % 256U,GetArgZeroIfEmpty(i, 4U) % 256U);
					touched = true;
				}
				break;
			}
			// ECMA-48 defines these as font changes.  We don't provide that.
			// The Linux console defines them as something else.  We don't provide that, either.
			case 10U:	break;
			case 11U:	break;
		}
	}
}

void
SoftTerm::ChangeAreaAttributes()
{
	const struct xy & origin(active_modes.origin ? scroll_origin : display_origin);
	const struct wh & margin(active_modes.origin ? scroll_margin : display_margin);
	coordinate top(TranslateFromDECCoordinates(GetArgOneIfZeroThisIfEmpty(0U, TranslateToDECCoordinates(0U))));
	coordinate left(TranslateFromDECCoordinates(GetArgOneIfZeroThisIfEmpty(0U, TranslateToDECCoordinates(0U))));
	coordinate bottom(TranslateFromDECCoordinates(GetArgThisIfZeroOrEmpty(1U, TranslateToDECCoordinates(margin.h - 1U))));
	coordinate right(TranslateFromDECCoordinates(GetArgThisIfZeroOrEmpty(1U, TranslateToDECCoordinates(margin.w - 1U))));
	if (top > bottom) return;
	if (left > right) return;
	top += origin.y;
	bottom += origin.y;
	left += origin.x;
	right += origin.x;

	CharacterCell::attribute_type turnoff(0U), flipon(0U);
	bool fg_touched(false), bg_touched(false);
	CharacterCell::colour_type fg, bg;
	SetAttributes(4U, turnoff, flipon, fg_touched, fg, bg_touched, bg);

	const coordinate bottom_margin(display_origin.y + display_margin.h - 1U);
	if (top > bottom_margin) return;
	const coordinate right_margin(display_origin.x + display_margin.w - 1U);
	if (left > right_margin) return;
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	if (left != 0U || right != stride - 1U) {
		const coordinate w(right - left + 1U);
		for (ScreenBuffer::coordinate r(top); r < bottom; ++r) {
			const ScreenBuffer::coordinate s(stride * r + left);
			screen.ModifyNCells(s, w, turnoff, flipon, fg_touched, fg, bg_touched, bg);
		}
	} else {
		const ScreenBuffer::coordinate s(stride * top);
		const ScreenBuffer::coordinate e(stride * (bottom + 1U));
		screen.ModifyNCells(s, e - s, turnoff, flipon, fg_touched, fg, bg_touched, bg);
	}
}

void
SoftTerm::SetModes(bool flag)
{
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i)
		SetMode (GetArgZeroIfEmpty(i), flag);
}

void
SoftTerm::SetPrivateModes(bool flag)
{
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i)
		SetPrivateMode (GetArgZeroIfEmpty(i), flag);
}

void
SoftTerm::SetSCOModes(bool flag)
{
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i)
		SetSCOMode (GetArgZeroIfEmpty(i), flag);
}

void
SoftTerm::SaveModes()
{
	saved_modes = active_modes;
}

void
SoftTerm::RestoreModes()
{
	active_modes = saved_modes;
}

void
SoftTerm::SGR0()
{
	attributes = 0U;
	colour = ColourPair::def;
}

void
SoftTerm::SaveAttributes()
{
	saved_attributes = attributes;
	saved_colour = colour;
}

void
SoftTerm::RestoreAttributes()
{
	attributes = saved_attributes;
	colour = saved_colour;
}

void
SoftTerm::SetLinesPerPageOrDTTerm()
{
	// This is a bodge to accommodate progreams such as NeoVIM that hardwire this xterm control sequence.
	// xterm is not strictly compatible here, as it gives quite different meanings to values of n less than 24.
	// This is an extension that began with dtterm.
	// A true DEC VT520 rounds up to the lowest possible size, which is 24.
	if (8U == GetArgOneIfZeroOrEmpty(0U)) {
		if (HasNoSubArgsFrom(0U))
			CollapseArgsToSubArgs(0U);
		unsigned rows(GetArgZeroIfEmpty(0U,1U));
		unsigned columns(GetArgZeroIfEmpty(0U,2U));
		if (rows > std::numeric_limits<coordinate>().max())
			rows = std::numeric_limits<coordinate>().max();
		if (columns > std::numeric_limits<coordinate>().max())
			columns = std::numeric_limits<coordinate>().max();
		// Remember that zero is allowed here, so 1U is not a minimum.
		if (columns != 1U && rows != 1U)
			Resize(columns, rows);
		// Allow mixing the xterm extension with DECSLPP, if ISO 8613-3/ITU T.416 form is used.
		if (1U >= QueryArgCount())
			return;
	}
	SetLinesPerPage();
}

void
SoftTerm::SetLinesPerPage()
{
	if (QueryArgCount()) {
		unsigned n(GetArgOneIfZeroOrEmpty(QueryArgCount() - 1U));
		if (n > std::numeric_limits<coordinate>().max())
			n = std::numeric_limits<coordinate>().max();
		// The DEC VT minimum is 24 rows; we are more liberal since we are not constrained by CRT hardware.
		if (n >= 2U)
			Resize(0U, n);
	}
}

void
SoftTerm::SetColumnsPerPage()
{
	if (QueryArgCount()) {
		unsigned n(GetArgOneIfZeroOrEmpty(QueryArgCount() - 1U));
		if (n > std::numeric_limits<coordinate>().max())
			n = std::numeric_limits<coordinate>().max();
		// The DEC VT minimum is 80 columns; we are more liberal since we are not constrained by CRT hardware.
		if (n >= 2U)
			Resize(n, 0U);
	}
}

void
SoftTerm::SetScrollbackBuffer(bool f)
{
	/// FIXME \bug This does not really work properly.
	if (f) {
		Resize(0U, display_margin.h + 25U);
		ResetMargins();
		display_origin.y = 25U;
	} else {
		display_origin.y = 0U;
		Resize(0U, display_margin.h);
	}
}

void
SoftTerm::SetMode(argument_type a, bool f)
{
	switch (a) {
/* IRM */	case 4U:	overstrike = !f; break;
		// This was deprecated in ECMA-48:1986, but we implement it anyway.
/* ZDM */	case 22U:	SetZeroDefaultMode(f); break;

		// ############## Intentionally unimplemented standard modes
		case 2U:	// KAM (keyboard action)
			// The terminal emulator is entirely decoupled from the physical keyboard; making these meaningless.
			break;
		case 6U:	// ERM (erasure)
		case 7U:	// VEM (line editing)
		case 10U:	// HEM (character editing)
		case 12U:	// SRM (local echoplex)
		case 18U:	// TSM (tabulation stop)
			// We don't provide this variability.
			break;
		case 19U:	// EBM (editing boundary)
		case 20U:	// LNM (linefeed/newline)
			// These were deprecated in ECMA-48:1991.
			break;

		// ############## As yet unimplemented or simply unknown standard modes
		case 3U:	// CRM (control representation)
		default:
#if defined(DEBUG)
			std::clog << "Unknown mode in SM/RM sequence : " << a << "\n";
#endif
			break;
	}
}

void
SoftTerm::SetPrivateMode(argument_type a, bool f)
{
	switch (a) {
/* DECCKM */	case 1U:	keyboard.SetCursorApplicationMode(f); break;
/* DECCOLM */	case 3U:
		{
			Resize(f ? 132U : 80U, 0U);
			if (!no_clear_screen_on_column_change) {
				Home();
				ClearDisplay();
			}
			ResetMargins();
			break;
		}
/* DECSCNM */	case 5U:	invert_screen = f; UpdateScreenFlags(); break;
		/// FIXME \bug origin mode can move the cursor and change pending wrap?
		/// DEC doco says yes.
/* DECOM */	case 6U:	active_modes.origin = f; break;
/* DECAWM */	case 7U:	active_modes.automatic_right_margin = f; break;
		case 12U:	// AT&T 610 via XTerm
			if (f)
				cursor_attributes |= CursorSprite::BLINK;
			else
				cursor_attributes &= ~CursorSprite::BLINK;
			UpdateCursorType();
			break;
/* DECTCEM */	case 25U:
			if (f)
				cursor_attributes |= CursorSprite::VISIBLE;
			else
				cursor_attributes &= ~CursorSprite::VISIBLE;
			UpdateCursorType();
			break;
/* DECNKM */	case 66U:	keyboard.SetCalculatorApplicationMode(f); break;
/* DECBKM */	case 67U:	keyboard.SetBackspaceIsBS(f); break;
/* DECLRMM */	case 69U:	active_modes.left_right_margins = f; break;
/* DECNCSM */	case 95U:	no_clear_screen_on_column_change = f; break;
/* DECRPL */	case 112U:	SetScrollbackBuffer(f); break;
/* DECECM */	case 117U:	active_modes.background_colour_erase = !f; break;
		case 1000U:	mouse.SetSendXTermMouseClicks(f); mouse.SetSendXTermMouseButtonMotions(false); mouse.SetSendXTermMouseNoButtonMotions(false); break;
		case 1002U:	mouse.SetSendXTermMouseClicks(f); mouse.SetSendXTermMouseButtonMotions(f); mouse.SetSendXTermMouseNoButtonMotions(false); break;
		case 1003U:	mouse.SetSendXTermMouseClicks(f); mouse.SetSendXTermMouseButtonMotions(f); mouse.SetSendXTermMouseNoButtonMotions(f); break;
		case 1006U:
			send_XTermMouse = f;
			mouse.SetSendXTermMouse(f);
			UpdatePointerType();
			break;
		case 1037U:	keyboard.SetDeleteIsDEL(f); break;	// XTerm extension
		case 2004U:	keyboard.SetSendPasteEvent(f); break;	// XTerm and many others
		case 1369U:	square = f; break;			// FIXME: Deprecated, remains for backwards compatibility; delete when/before XTerm gets here.
		case 7727U:	keyboard.SetEscapeIsFS(f); break;	// TeraTerm extension

		// ############## Intentionally unimplemented private modes
		case 8U:	// DECARM (autorepeat)
		case 68U:	// DECKBUM (main keypad data processing, i.e. group 2 lock)
			// The terminal emulator is entirely decoupled from the physical keyboard; making these meaningless.
			break;
		case 1004U:	// xterm GUI focus event reports
			// The terminal emulator is entirely decoupled from the realizer; making these meaningless.
			break;
		case 9U:	// DECINLM (interlace mode) has no meaning.
			// This is also in xterm an old ambiguous to decode mouse protocol, since superseded.
			break;
		case 47U:	// DECGRPM (rotated graphic mode) has no meaning.
			// But this is also an old xterm means of controlling the alternate screen buffer, without automatically clearing it when switched to.
			if (altbuffer != f) {
				screen.SetAltBuffer(f);
				altbuffer = f;
			}
			break;
		case 2U:	break;	// DECANM (ANSI) has no meaning as we are never in VT52 mode.
		case 4U:	break;	// DECSCLM (slow a.k.a. smooth scroll) is not useful.
		case 10U:	break;	// DECEDM (edit) local editing mode is largely unused in the wild.
		case 11U:	break;	// DECLTM (line transmission) has no meaning.
		case 13U:	break;	// DECSCFDN (space compression/field delimiting) field transmission control in local edit mode, unimplemented because we do not implement local edit mode.
		case 14U:	break;	// DECTEM (transmit execution mode) immediate/on-demand field transmit mode (when in local editing mode) largely unused in the wild.
		case 16U:	break;	// DECEKEM (edit key execution) inform host when in local edit mode, unimplemented because we do not implement local edit mode itself.
		case 18U:	break;	// DECPFF (print Form Feed) has no meaning as we have no printer.
		case 19U:	break;	// DECPEXT (print extent) has no meaning as we have no printer.
		case 1001U:	break;	// This is a mouse grabber, tricky and largely unused in the wild.
		case 1005U:	break;	// This is an old ambiguous to decode mouse protocol, since superseded.
		case 1015U:	break;	// This is an old mouse protocol, since superseded.
		case 1047U:	if (altbuffer != f) {
					if (altbuffer) ClearDisplay();
					screen.SetAltBuffer(f);
					altbuffer = f;
				}
				break;
		case 1048U:	f ? DECSC() : DECRC(); break;
		case 1049U:	if (altbuffer != f) {
					if (f) DECSC();
					screen.SetAltBuffer(f);
					if (f) {
						Home();
						ClearDisplay();
					}
					if (!f) DECRC();
					altbuffer = f;
				}
				break;

		// ############## As yet unimplemented or simply unknown private modes
#if 0 /// TODO: alternate screen buffer modes
		case 1007U:	/// \todo Wheel mouse events when the alternate screen buffer is on
#endif
		default:
#if defined(DEBUG)
			std::clog << "Unknown mode in DECSM/DECRM sequence : " << a << "\n";
#endif
			break;
	}
}

void
SoftTerm::SetSCOMode(argument_type a, bool f)
{
	switch (a) {
		case 1U:	square = f; break;
		case 2U:	keyboard.SetDECFunctionKeys(f); break;
		case 3U:	keyboard.SetSCOFunctionKeys(f); break;
		case 4U:	keyboard.SetTekenFunctionKeys(f); break;
		default:
#if defined(DEBUG)
			std::clog << "Unknown mode in SCOSM/SCORM sequence : " << a << "\n";
#endif
			break;
	}
}

void
SoftTerm::DECSC()
{
	SaveCursor();
	SaveAttributes();
	SaveModes();
}

void
SoftTerm::DECRC()
{
	RestoreCursor();
	RestoreAttributes();
	RestoreModes();
}

void
SoftTerm::SCOSCorDESCSLRM()
{
	// SCOSC (SCO console save cursor) is the same control sequence as DECSLRM (DEC VT set left and right margins).
	// SCOSC is what the Linux and FreeBSD consoles implement and it is listed in the termcap/terminfo entries, so we cannot simply omit it.
	// This is pretty much the same bodge to solve this as used by XTerm.
	if (active_modes.left_right_margins || QueryArgCount() > 0U)
		SetLeftRightMargins();
	else
		DECSC();	// It is the same.
}

void
SoftTerm::SCORC()
{
	DECRC();	// It is the same.
}

void
SoftTerm::SetSCOAttributes()
{
	if (QueryArgCount() < 1U) return;	// SCO SGR must always have an initial subcommand parameter.
	switch (GetArgZeroIfEmpty(0U)) {
		case 0U:
			colour = ColourPair::def;
			break;
		case 1U:
			if (QueryArgCount() > 1U)
				colour.background = Map256Colour(GetArgZeroIfEmpty(1U) % 256U);
			break;
		case 2U:
			if (QueryArgCount() > 1U)
				colour.foreground = Map256Colour(GetArgZeroIfEmpty(1U) % 256U);
			break;
		default:
#if defined(DEBUG)
			std::clog << "Unknown SCO attribute : " << GetArgZeroIfEmpty(0U) << "\n";
#endif
			break;
	}
}

void
SoftTerm::SetSCOCursorType()
{
	if (QueryArgCount() != 1U) return;	// We don't do custom cursor shapes as they do not fit the cursor model.
	switch (GetArgZeroIfEmpty(0U)) {
		case 0U:
			cursor_attributes |= CursorSprite::VISIBLE;
			cursor_attributes &= ~CursorSprite::BLINK;
			cursor_type = CursorSprite::BLOCK;
			break;
		case 1U:
			cursor_attributes |= CursorSprite::BLINK|CursorSprite::VISIBLE;
			cursor_type = CursorSprite::BLOCK;
			break;
		case 5U:
			cursor_attributes &= ~(CursorSprite::VISIBLE|CursorSprite::BLINK);
			cursor_type = CursorSprite::UNDERLINE;
			break;
	}
	UpdateCursorType();
}

// This control sequence is implemented by the Linux virtual terminal and used by programs such as vim.
// See Linux/Documentation/admin-guide/VGA-softcursor.txt .
void
SoftTerm::SetLinuxCursorType()
{
	if (QueryArgCount() < 1U) return;
	switch (GetArgZeroIfEmpty(0U) & 0x0F) {
		case 0U:
			cursor_attributes |= CursorSprite::VISIBLE;
			cursor_type = CursorSprite::BLOCK;
			break;
		case 1U:
			cursor_attributes &= ~CursorSprite::VISIBLE;
			cursor_type = CursorSprite::UNDERLINE;
			break;
		case 2U:
			cursor_attributes |= CursorSprite::VISIBLE;
			cursor_type = CursorSprite::UNDERLINE;
			break;
		case 3U:
		case 4U:
		case 5U:
			cursor_attributes |= CursorSprite::VISIBLE;
			cursor_type = CursorSprite::BOX;
			break;
		case 6U:
		default:
			cursor_attributes |= CursorSprite::VISIBLE;
			cursor_type = CursorSprite::BLOCK;
			break;
	}
	UpdateCursorType();
}

void
SoftTerm::SetCursorStyle()
{
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i)
		switch (GetArgZDIfZeroOneIfEmpty(i)) {
			case 0U:
			case 1U:
				cursor_attributes |= CursorSprite::BLINK;
				cursor_type = CursorSprite::BLOCK;
				break;
			case 2U:
				cursor_attributes &= ~CursorSprite::BLINK;
				cursor_type = CursorSprite::BLOCK;
				break;
			case 3U:
				cursor_attributes |= CursorSprite::BLINK;
				cursor_type = CursorSprite::UNDERLINE;
				break;
			case 4U:
				cursor_attributes &= ~CursorSprite::BLINK;
				cursor_type = CursorSprite::UNDERLINE;
				break;
			case 5U:
				cursor_attributes |= CursorSprite::BLINK;
				cursor_type = CursorSprite::BAR;
				break;
			case 6U:
				cursor_attributes &= ~CursorSprite::BLINK;
				cursor_type = CursorSprite::BAR;
				break;
			case 7U:
				cursor_attributes |= CursorSprite::BLINK;
				cursor_type = CursorSprite::BOX;
				break;
			case 8U:
				cursor_attributes &= ~CursorSprite::BLINK;
				cursor_type = CursorSprite::BOX;
				break;
			case 9U:
				cursor_attributes |= CursorSprite::BLINK;
				cursor_type = CursorSprite::STAR;
				break;
			case 10U:
				cursor_attributes &= ~CursorSprite::BLINK;
				cursor_type = CursorSprite::STAR;
				break;
			case 11U:
				cursor_attributes |= CursorSprite::BLINK;
				cursor_type = CursorSprite::UNDEROVER;
				break;
			case 12U:
				cursor_attributes &= ~CursorSprite::BLINK;
				cursor_type = CursorSprite::UNDEROVER;
				break;
			case 13U:
				cursor_attributes |= CursorSprite::BLINK;
				cursor_type = CursorSprite::MIRRORL;
				break;
			case 14U:
				cursor_attributes &= ~CursorSprite::BLINK;
				cursor_type = CursorSprite::MIRRORL;
				break;
		}
	UpdateCursorType();
}

static
const char
vt220_device_attribute1[] =
	"?"		// DEC private
	"62" 		// VT220
	";"
	"1" 		// ... with 132-column support
	";"
	"22" 		// ... with ANSI colour capability
	";"
	"29" 		// ... with a locator device
	"c";

void
SoftTerm::SendPrimaryDeviceAttribute(argument_type a)
{
	switch (a) {
		case 0U:
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(sizeof vt220_device_attribute1 - 1, vt220_device_attribute1);
			break;
		default:
#if defined(DEBUG)
			std::clog << "Unknown primary device attribute request : " << a << "\n";
#endif
			break;
	}
}

void
SoftTerm::SendPrimaryDeviceAttributes()
{
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i)
		SendPrimaryDeviceAttribute (GetArgZeroIfEmpty(i));
}

static
const char
vt220_device_attribute2[] =
	">"		// DEC private
	"1" 		// VT220
	";"
	"0" 		// firmware version number
	";"
	"0" 		// ROM cartridge registration number
	"c";

void
SoftTerm::SendSecondaryDeviceAttribute(argument_type a)
{
	switch (a) {
		case 0U:
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(sizeof vt220_device_attribute2 - 1, vt220_device_attribute2);
			break;
		default:
#if defined(DEBUG)
			std::clog << "Unknown secondary device attribute request : " << a << "\n";
#endif
			break;
	}
}

void
SoftTerm::SendSecondaryDeviceAttributes()
{
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i)
		SendSecondaryDeviceAttribute (GetArgZeroIfEmpty(i));
}

static
const char
vt220_device_attribute3[] =
	"!"
	"|"
	"00" 		// zero factory number
	"000000" 	// zero item number
	;

void
SoftTerm::SendTertiaryDeviceAttribute(argument_type a)
{
	switch (a) {
		case 0U:
			keyboard.WriteControl1Character(DCS);
			keyboard.WriteLatin1Characters(sizeof vt220_device_attribute3 - 1, vt220_device_attribute3);
			keyboard.WriteControl1Character(ST);
			break;
		default:
#if defined(DEBUG)
			std::clog << "Unknown tertiary device attribute request : " << a << "\n";
#endif
			break;
	}
}

void
SoftTerm::SendTertiaryDeviceAttributes()
{
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i)
		SendTertiaryDeviceAttribute (GetArgZeroIfEmpty(i));
}

static
const char
operating_status_report[] =
	"0" 		// operating OK
	"n";

void
SoftTerm::SendDeviceStatusReport(argument_type a)
{
	switch (a) {
		case 5U:
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(sizeof operating_status_report - 1, operating_status_report);
			break;
		case 6U:
		{
			const coordinate x(active_cursor.x - (active_modes.origin ? scroll_origin.x : display_origin.x));
			const coordinate y(active_cursor.y - (active_modes.origin ? scroll_origin.y : display_origin.y));
			char b[32];
			const int n(snprintf(b, sizeof b, "%u;%uR", TranslateToDECCoordinates(y), TranslateToDECCoordinates(x)));
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(n, b);
			break;
		}
		default:
#if defined(DEBUG)
			std::clog << "Unknown device attribute request : " << a << "\n";
#endif
			break;
	}
}

void
SoftTerm::SendDeviceStatusReports()
{
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i)
		SendDeviceStatusReport (GetArgZeroIfEmpty(i));
}

static
const char
printer_status_report[] =
	"?"		// DEC private
	"13" 		// no printer
	"n";

static
const char
udk_status_report[] =
	"?"		// DEC private
	"21" 		// UDKs are locked
	"n";

static
const char
keyboard_status_report[] =
	"?"		// DEC private
	"27" 		// keyboard reply
	";"
	"0"		// unknown language
	";"
	"0"		// keyboard ready
	";"
	"5"		// PCXAL
	"n";

static
const char
locator_status_report[] =
	"?"		// DEC private
	"50" 		// locator is present and enabled
	"n";

static
const char
locator_type_status_report[] =
	"?"		// DEC private
	"57" 		// mouse reply
	";"
	"1"		// the locator is a mouse
	"n";

static
const char
data_integrity_report[] =
	"?"		// DEC private
	"70" 		// serial communications errors are impossible
	"n";

static
const char
session_status_report[] =
	"?"		// DEC private
	"83" 		// sessions are not available.
	"n";

void
SoftTerm::SendPrivateDeviceStatusReport(argument_type a)
{
	switch (a) {
		case 6U:
		{
			const coordinate x(active_cursor.x - (active_modes.origin ? scroll_origin.x : display_origin.x));
			const coordinate y(active_cursor.y - (active_modes.origin ? scroll_origin.y : display_origin.y));
			char b[32];
			const int n(snprintf(b, sizeof b, "%u;%u;%uR", TranslateToDECCoordinates(y), TranslateToDECCoordinates(x), 1U));
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(n, b);
			break;
		}
		case 15U:
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(sizeof printer_status_report - 1, printer_status_report);
			break;
		case 25U:
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(sizeof udk_status_report - 1, udk_status_report);
			break;
		case 26U:
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(sizeof keyboard_status_report - 1, keyboard_status_report);
			break;
		case 53U:	// This is an xterm extension.
		case 55U:	// This is the DEC-specified report number.
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(sizeof locator_status_report - 1, locator_status_report);
			break;
		case 56U:
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(sizeof locator_type_status_report - 1, locator_type_status_report);
			break;
		case 75U:
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(sizeof data_integrity_report - 1, data_integrity_report);
			break;
		case 85U:
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(sizeof session_status_report - 1, session_status_report);
			break;
		default:
#if defined(DEBUG)
			std::clog << "Unknown device attribute request : " << a << "\n";
#endif
			break;
	}
}

void
SoftTerm::SendPrivateDeviceStatusReports()
{
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i)
		SendPrivateDeviceStatusReport (GetArgZeroIfEmpty(i));
}

void
SoftTerm::SendPresentationStateReport(argument_type a)
{
	switch (a) {
		case 1U:
		{
			keyboard.WriteControl1Character(DCS);
			const coordinate x(active_cursor.x - (active_modes.origin ? scroll_origin.x : display_origin.x));
			const coordinate y(active_cursor.y - (active_modes.origin ? scroll_origin.y : display_origin.y));
			char b[48];
			const char Srend(static_cast<char>(
				'\x40' +
				(attributes & CharacterCell::INVERSE	? 0x08 : 0x00) +
				(attributes & CharacterCell::BLINK	? 0x04 : 0x00) +
				(attributes & CharacterCell::UNDERLINES	? 0x02 : 0x00) +
				(attributes & CharacterCell::BOLD	? 0x01 : 0x00)
				)
			);
			const char Satt(static_cast<char>(
				'\x40' +
				0x00 // DECSCA is always off.
				)
			);
			// We don't report the SS2 and SS3 states.
			const char Sflag(static_cast<char>(
				'\x40' +
				(active_cursor.advance_pending ? 0x08 : 0x00) +
				(active_modes.origin ? 0x01 : 0x00)
				)
			);
			const int n(
				// We outright omit Pgl, Pgr, Scss, and Sdesig because we do not employ ISO 2022 graphic sets.
				snprintf(b, sizeof b,
					"1$u%u;%u;%u;%c;%c;%c;;;;",
					TranslateToDECCoordinates(y),
					TranslateToDECCoordinates(x),
					1U,	// There are no pages; so always page #1.
					Srend,
					Satt,
					Sflag
				)
			);
			keyboard.WriteLatin1Characters(n, b);
			keyboard.WriteControl1Character(ST);
			break;
		}
		case 2U:
		{
			const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
			keyboard.WriteControl1Character(DCS);
			keyboard.WriteLatin1Characters(sizeof "2$" - 1, "2$");
			char b[32];
			char c('u');
			for (coordinate col(0); col < right_margin; ++col) {
				if (IsHorizontalTabstopAt(col)) {
					const int n(snprintf(b, sizeof b, "%c%u", c, TranslateToDECCoordinates(col)));
					keyboard.WriteLatin1Characters(n, b);
					c = '/';
				}
			}
			keyboard.WriteControl1Character(ST);
			break;
		}
		case 0U:	break;
		default:
#if defined(DEBUG)
			std::clog << "Unknown presentation state report request : " << a << "\n";
#endif
			break;
	}
}

void
SoftTerm::SendPresentationStateReports()
{
	MinimumOneArg();
	for (std::size_t i(0U); i < QueryArgCount(); ++i)
		SendPresentationStateReport (GetArgZeroIfEmpty(i));
}

/* Locator control **********************************************************
// **************************************************************************
*/

void
SoftTerm::RequestLocatorReport()
{
	if (0 == QueryArgCount())
		mouse.RequestDECLocatorReport();
	else
	for (std::size_t i(0U); i < QueryArgCount(); ++i)
		mouse.RequestDECLocatorReport();
}

void
SoftTerm::EnableLocatorReports()
{
	send_DECLocator = GetArgZeroIfEmpty(0U);
	mouse.SetSendDECLocator(send_DECLocator);
	UpdatePointerType();
	if (2U <= QueryArgCount() && 1U == GetArgZeroIfEmpty(1U)) {
#if defined(DEBUG)
		std::clog << "Pixel coordinate locator report request denied.\n";
#endif
	}
}

void
SoftTerm::SelectLocatorEvents()
{
	if (0 == QueryArgCount())
		SelectLocatorEvent(0U);
	else
	for (std::size_t i(0U); i < QueryArgCount(); ++i)
		SelectLocatorEvent(GetArgZeroIfEmpty(i));
}

void
SoftTerm::SelectLocatorEvent(unsigned int a)
{
	switch (a) {
		case 0U:
			mouse.SetSendDECLocatorPressEvent(false);
			mouse.SetSendDECLocatorReleaseEvent(false);
			break;
		case 1U:
		case 2U:
			mouse.SetSendDECLocatorPressEvent(1U == a);
			break;
		case 3U:
		case 4U:
			mouse.SetSendDECLocatorReleaseEvent(3U == a);
			break;
		default:
#if defined(DEBUG)
			std::clog << "Unknown locator event selected : " << a << "\n";
#endif
			break;
	}
}

/* Miscellany ***************************************************************
// **************************************************************************
*/

void
SoftTerm::SoftReset()
{
	altbuffer = false;
	screen.SetAltBuffer(false);
	ResetMargins();
	SetRegularHorizontalTabstops(8U);
	cursor_attributes = CursorSprite::VISIBLE|CursorSprite::BLINK;
	cursor_type = CursorSprite::BLOCK;
	send_DECLocator = send_XTermMouse = false;
	UpdateCursorType();
	UpdatePointerType();
	UpdateScreenFlags();
	SGR0();
	keyboard.SetCursorApplicationMode(false);
	keyboard.SetCalculatorApplicationMode(false);
	keyboard.SetBackspaceIsBS(false);
	keyboard.SetEscapeIsFS(false);
	keyboard.SetSCOFunctionKeys(true);
	keyboard.SetDECFunctionKeys(true);
	keyboard.SetTekenFunctionKeys(true);
	saved_modes = active_modes = mode();
	saved_cursor = display_origin;
	scrolling = true;
	overstrike = true;
	square = true;
}

void
SoftTerm::ResetToInitialState()
{
	Resize(80U, 25U);
	altbuffer = false;
	screen.SetAltBuffer(false);
	invert_screen = false;
	last_printable_character = NUL;
	Home();
	ClearDisplay();
	no_clear_screen_on_column_change = false;
	// Per the VT420 programmers' reference, if one ignores serial comms RIS does only a few things more than DECSTR.
	ResetMargins();
	SetRegularHorizontalTabstops(8U);
	cursor_attributes = CursorSprite::VISIBLE|CursorSprite::BLINK;
	cursor_type = CursorSprite::BLOCK;
	send_DECLocator = send_XTermMouse = false;
	UpdateCursorType();
	UpdatePointerType();
	UpdateScreenFlags();
	SGR0();
	keyboard.SetCursorApplicationMode(false);
	keyboard.SetCalculatorApplicationMode(false);
	keyboard.SetBackspaceIsBS(false);
	keyboard.SetEscapeIsFS(false);
	keyboard.SetSCOFunctionKeys(true);
	keyboard.SetDECFunctionKeys(true);
	keyboard.SetTekenFunctionKeys(true);
	saved_modes = active_modes = mode();
	saved_cursor = active_cursor;
	scrolling = true;
	overstrike = true;
	square = true;
}

void
SoftTerm::RepeatPrintableCharacter(unsigned int a)
{
	if (NUL == last_printable_character) return;
	if (active_modes.automatic_right_margin) {
		// No need to repeat more than an entire screenful.
		const unsigned m(unsigned(display_margin.w) * display_margin.h);
		if (a > m) a = m;
	} else {
		// No need to repeat more than an entire lineful.
		if (a > display_margin.w) a = display_margin.w;
	}
	while (a) {
		--a;
		PrintableCharacter(false /* no error */, 1U /* unshifted */, last_printable_character);
	}
}

/* Top-level output functions ***********************************************
// **************************************************************************
*/

void
SoftTerm::ProcessDecodedUTF8(
	char32_t character,
	bool decoder_error,
	bool overlong
) {
	ecma48_decoder.Process(character, decoder_error, overlong);
}

void
SoftTerm::ControlCharacter(char32_t character)
{
	switch (character) {
		case NUL:	break;
		// Per DEC EL-000070-D section D.3, DEC VT factory default for the answerback string is an empty buffer.
		case ENQ:	break;
		case BEL:	/* TODO: bell */ break;
		case CR:	CarriageReturn(); break;
		case NEL:	CarriageReturnNoUpdate(); CursorDown(1U, scrolling); break;
		case IND: case LF: case VT: case FF:
				CursorDown(1U, scrolling); break;
		case RI:	CursorUp(1U, scrolling); break;
		case TAB:	HorizontalTab(1U, true); break;
		case BS:	CursorLeft(1U, false); break;
		case DEL:	DeleteCharacters(1U); break;
		case HTS:	SetHorizontalTabstop(); break;
		// These are wholly dealt with by the ECMA-48 decoder.
		case ESC: case CSI: case SS2: case SS3: case CAN: case DCS: case OSC: case PM: case APC: case SOS: case ST:
				break;
		default:	break;
	}
}

void
SoftTerm::EscapeSequence(char32_t final, char32_t first_intermediate)
{
	switch (first_intermediate) {
		default:	break;
		case NUL:
			switch (final) {
				default:	break;
// ---- ECMA-35 "private control functions" range from 0x30 to 0x3F ----
				case '0':	break;
/* DECGON */			case '1':	break;	// We are never in VT105 mode.
/* DECGOFF */			case '2':	break;	// We are never in VT105 mode.
/* DECVTS */			case '3':	break;	// We are never in LA120 mode.
/* DECCAVT */			case '4':	break;	// We are never in LA120 mode.
/* DECXMT */			case '5':	break;	// Unimplemented, because we do not implement local editing mode.
/* DECBI */			case '6':	CursorLeft(1U, true); break;
/* DECSC */			case '7':	DECSC(); break;
/* DECRC */			case '8':	DECRC(); break;
/* DECFI */			case '9':	CursorRight(1U, true); break;
				case ':':	break;
				case ';':	break;
/* DECANSI */			case '<':	break;	// We are never in VT52 mode.
/* DECKPAM */			case '=':	keyboard.SetCalculatorApplicationMode(true); break;
/* DECKPNM */			case '>':	keyboard.SetCalculatorApplicationMode(false); break;
				case '?':	break;
// ---- ECMA-35 "C1 set control functions" range from 0x40 to 0x5F ----
// The ECMA-48 decoder takes care of these.
// ---- ECMA-35 "standardized single control functions" range from 0x60 to 0x7F ----
/* DMI */			case '`':	break;	// Not applicable.
/* INT */			case 'a':	break;	// Not applicable.
/* EMI */			case 'b':	break;	// Not applicable.
/* RIS */			case 'c':	ResetToInitialState(); break;
/* NAPLPS */			case 'k':	break;	// We do not employ ISO 2022 graphic sets.
/* NAPLPS */			case 'l':	break;	// We do not employ ISO 2022 graphic sets.
/* NAPLPS */			case 'm':	break;	// We do not employ ISO 2022 graphic sets.
/* LS1 */			case 'n':	break;	// We do not employ ISO 2022 graphic sets.
/* LS2 */			case 'o':	break;	// We do not employ ISO 2022 graphic sets.
/* LS3R */			case '|':	break;	// We do not employ ISO 2022 graphic sets.
/* LS2R */			case '}':	break;	// We do not employ ISO 2022 graphic sets.
/* LS1R */			case '~':	break;	// We do not employ ISO 2022 graphic sets.
			}
			break;
// ---- ECMA-35 "announce code structure" ----
		case ' ':
			switch (final) {
				// We explicitly do not support most of the character encoding announcements.
				default:	break;
/* S7C1T */			case 'F':	keyboard.Set8BitControl1(false); break;
/* S8C1T */			case 'G':	keyboard.Set8BitControl1(true); break;
			}
			break;
// ---- ECMA-35 "single control functions" ----
		case '#':
			switch (final) {
				default:	break;
// ---- ECMA-35 "private control functions" range from 0x30 to 0x3F ----
/* DECALN */			case '8':	ResetMargins(); Home(); ClearDisplay('E'); break;
// ---- ECMA-35 "registered single control functions" range from 0x40 to 0x7F ----
			}
			break;
// ---- ECMA-35 "C0/C1 designate" ----
		case '!': case '"':
			break;	// We do not employ ISO 2022 graphic sets.
// ---- ECMA-35 "G0/G1/G2/G3 designate multibyte 94/96-set" ----
		case '$':
			break;	// We do not employ ISO 2022 graphic sets.
// ---- ECMA-35 "designate other coding system" ----
		case '%':
			break;	// We do not employ ISO 2022 graphic sets.
// ---- ECMA-35 "G0/G1/G2/G3 designate 94/96-set" ----
		case '(': case ')': case '*': case '+': case '-': case '.': case '/':
			break;	// We do not employ ISO 2022 graphic sets.
	}
}

void
SoftTerm::ControlSequence(
	char32_t character,
	char32_t last_intermediate,
	char32_t first_private_parameter
) {
	if (NUL == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
// ---- ECMA-defined final characters ----
/* ICH */		case '@':	InsertCharacters(GetArgZDIfZeroOneIfEmpty(0U)); break;
/* CUU */		case 'A':	CursorUp(GetArgZDIfZeroOneIfEmpty(0U), false); break;
/* CUD */		case 'B':	CursorDown(GetArgZDIfZeroOneIfEmpty(0U), false); break;
/* CUF */		case 'C':	CursorRight(GetArgZDIfZeroOneIfEmpty(0U), false); break;
/* CUB */		case 'D':	CursorLeft(GetArgZDIfZeroOneIfEmpty(0U), false); break;
/* CNL */		case 'E':	CarriageReturnNoUpdate(); CursorDown(GetArgOneIfZeroOrEmpty(0U), false); break;
/* CPL */		case 'F':	CarriageReturnNoUpdate(); CursorUp(GetArgOneIfZeroOrEmpty(0U), false); break;
/* CHA */		case 'G':	GotoX(GetArgOneIfZeroOrEmpty(0U)); break;
/* CUP */		case 'H':	GotoYX(GetArgOneIfZeroOrEmpty(0U), GetArgOneIfZeroOrEmpty(1U)); break;
/* CHT */		case 'I':	HorizontalTab(GetArgZDIfZeroOneIfEmpty(0U), true); break;
/* ED */		case 'J':	EraseInDisplay(); break;
/* EL */		case 'K':	EraseInLine(); break;
/* IL */		case 'L':	InsertLines(GetArgZDIfZeroOneIfEmpty(0U)); break;
/* DL */		case 'M':	DeleteLines(GetArgZDIfZeroOneIfEmpty(0U)); break;
/* EF */		case 'N':	break; // Erase Field has no applicability as there are no fields.
/* EA */		case 'O':	break; // Erase Area has no applicability as there are no areas.
/* DCH */		case 'P':	DeleteCharacters(GetArgZDIfZeroOneIfEmpty(0U)); break;
/* SEE */		case 'Q':	break; // Set Editing Extent has no applicability as this is not a block mode terminal.
/* CPR */		case 'R':	break; // Cursor Position Report is meaningless if received rather than sent.
/* SU */		case 'S':	PanUp(GetArgZDIfZeroOneIfEmpty(0U)); break;
/* SD */		case 'T':	PanDown(GetArgZDIfZeroOneIfEmpty(0U)); break;
/* NP */		case 'U':	break; // Next Page has no applicability as there are no pages.
/* PP */		case 'V':	break; // Previous Page has no applicability as there are no pages.
/* CTC */		case 'W':	CursorTabulationControl(); break;
/* ECH */		case 'X':	EraseCharacters(GetArgZDIfZeroOneIfEmpty(0U)); break;
/* CVT */		case 'Y':	VerticalTab(GetArgZDIfZeroOneIfEmpty(0U), true); break;
/* CBT */		case 'Z':	BackwardsHorizontalTab(GetArgZDIfZeroOneIfEmpty(0U), true); break;
/* SRS */		case '[':	break; // No-one needs reversed strings from a virtual terminal.
/* PTX */		case '\\':	break; // No-one needs parallel texts from a virtual terminal.
/* SDS */		case ']':	break; // No-one needs directed strings from a virtual terminal.
/* SIMD */		case '^':	break; // No-one needs implicit movement direction from a virtual terminal.
/* HPA */		case '`':	GotoX(GetArgOneIfZeroOrEmpty(0U)); break;
/* HPR */		case 'a':	CursorRight(GetArgZDIfZeroOneIfEmpty(0U), false); break;
/* REP */		case 'b':	RepeatPrintableCharacter(GetArgZDIfZeroOneIfEmpty(0U)); break;
/* DA */		case 'c':	SendPrimaryDeviceAttributes(); break;
/* VPA */		case 'd':	GotoY(GetArgOneIfZeroOrEmpty(0U)); break;
/* VPR */		case 'e':	CursorDown(GetArgZDIfZeroOneIfEmpty(0U), false); break;
/* HVP */		case 'f':	GotoYX(GetArgOneIfZeroOrEmpty(0U), GetArgOneIfZeroOrEmpty(1U)); break;
/* TBC */		case 'g':	TabClear(); break;
/* SM */		case 'h':	SetModes(true); break;
/* MC */		case 'i':	break; // Media Copy has no applicability as there are no auxiliary devices.
/* HPB */		case 'j':	CursorLeft(GetArgZDIfZeroOneIfEmpty(0U), false); break;
/* VPB */		case 'k':	CursorUp(GetArgZDIfZeroOneIfEmpty(0U), false); break;
/* RM */		case 'l':	SetModes(false); break;
/* SGR */		case 'm':	SetAttributes(); break;
/* DSR */		case 'n':	SendDeviceStatusReports(); break;
/* DAQ */		case 'o':	break; // Define Area Qualification has no applicability as this is not a block mode terminal.
// ---- ECMA private-use final characters begin here. ----
/* DECSR */		case 'p':	break; // Secure Reset is not implemented.
/* DECLL */		case 'q':	break; // Load LEDs has no applicability as there are no LEDs.
/* DECSTBM */		case 'r':	SetTopBottomMargins(); break;
/* SCOSC/DECSLRM */	case 's':	SCOSCorDESCSLRM(); break;
/* DECSLPP */		case 't':	SetLinesPerPageOrDTTerm(); break;
/* SCORC */		case 'u':	SCORC(); break;	// per screen(HW) manual; and not DECSHST as on LA100.
/* DECSVST */		case 'v':	break; // Set multiple vertical tab stops at once is not implemented.
/* DECSHORP */		case 'w':	break; // Set Horizontal Pitch has no applicability as this is not a LA100.
/* SCOSGR */		case 'x':	SetSCOAttributes(); break;	// per screen(HW) manual; and not DECREQTPARM
/* DECTST */		case 'y':	break; // Confidence Test has no applicability as this is not a hardware terminal.
/* DECSVERP */		case 'z':	break; // Set Vertical Pitch has no applicability as this is not a LA100.  Also not SCOVTSW.
/* DECTTC */		case '|':	break; // Transmit Termination Character is not implemented
/* DECPRO */		case '}':	break; // Define Protected Field is not implemented
/* DECFNK */		case '~':	break; // Ignore any function keys output by applications.
			default:
#if defined(DEBUG)
				std::clog << "Unknown CSI terminator " << character << "\n";
#endif
				break;
		} else
		if ('?' == first_private_parameter) switch (character) {
/* DECCTC */		case 'W':	DECCursorTabulationControl(); break;
/* LINUXSCUSR */	case 'c':	SetLinuxCursorType(); break;
/* DECSM */		case 'h':	SetPrivateModes(true); break;
/* DECRM */		case 'l':	SetPrivateModes(false); break;
/* XTQMODKEYS */	case 'm':	break; // We do not support this.
/* DECDSR */		case 'n':	SendPrivateDeviceStatusReports(); break;
			default:
#if defined(DEBUG)
				std::clog << "Unknown DEC Private CSI " << first_private_parameter << ' ' << character << "\n";
#endif
				break;
		} else
		if ('>' == first_private_parameter) switch (character) {
/* DECDA2 */		case 'c':	SendSecondaryDeviceAttributes(); break;
/* XTMODKEYS */		case 'm':	break; // We do not support this.
/* XTDMODKEYS */	case 'n':	break; // We do not support this.
			default:
#if defined(DEBUG)
				std::clog << "Unknown DEC Private CSI " << first_private_parameter << ' ' << character << "\n";
#endif
				break;
		} else
		if ('=' == first_private_parameter) switch (character) {
// All of the non-extension SCO sequences here are in the screen(HW) manual.
// The CONS25 sequences other than C25LSCURS may be apocryphal and are not in the screen(4) manual.
/* SCOABG */		case 'A':	break; // SCO set border colour has no applicability as there is no border.
/* SCOBLPD */		case 'B':	break; // SCO set BEL pitch and duration has no applicabililty as there is no beeper.
/* SCOSCUSR */		case 'C':	SetSCOCursorType(); break;
/* SCOVGAI */		case 'D':	break; // SCO set VGA background intensity does not match the colour model.
/* SCOVGAB */		case 'E':	break; // SCO set VGA CRTC blink/bold does not match the colour model.
/* SCOANFG */		case 'F':	break; // SCO set normal foreground colour is not implemented.
/* SCOANBG */		case 'G':	break; // SCO set normal background colour is not implemented.
/* SCOARFG */		case 'H':	break; // SCO set inverse video foreground colour does not match the colour model.
/* SCOARBG */		case 'I':	break; // SCO set inverse video background colour does not match the colour model.
/* SCOAGFG */		case 'J':	break; // SCO set graphics foreground colour does not match the colour model.
/* SCOAGBG */		case 'K':	break; // SCO set graphics background colour does not match the colour model.
/* SCOECM */		case 'L':	break; // SCO erase modes (native/iBCSe2/ECMA) are too complex to be worth it.
/* SCORQC */		case 'M':	break; // SCO send foreground/background colour report is not worth implementing, and doesn't generate a proper control sequence either.
/* C25LSCURS */		case 'S':	SetSCOCursorType(); break; // This is technically a "local" SCOSCURS.
/* C25MODE */		case 'T':	break; // CONS25 set mode is not implemented.
/* DECDA3 */		case 'c':	SendTertiaryDeviceAttributes(); break;
/* SCOAG */		case 'g':	break; // SCO alternative graphics set does not match the character model.
/* SCOSM */		case 'h':	SetSCOModes(true); break; // extension to SCO per DECSM/SM
/* SCORM */		case 'l':	SetSCOModes(false); break; // extension to SCO per DECRM/RM
/* C25SGR */		case 'x':	break; // CONS25 set attributes is not implemented, and may be apocryphal for SCOSGR.
/* C25VTSW */		case 'z':	break; // CONS25 switch virtual terminal has no applicability, and may be apocryphal for SCOVTSW.
			default:
#if defined(DEBUG)
				std::clog << "Unknown DEC Private CSI " << first_private_parameter << ' ' << character << "\n";
#endif
				break;
		} else {
#if defined(DEBUG)
			std::clog << "Unknown Private CSI " << first_private_parameter << ' ' << character << "\n";
#endif
		}
	} else
	if ('$' == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
/* DECSCPP */		case '|':	SetColumnsPerPage(); break;
/* DECCARA */		case 'r':	ChangeAreaAttributes(); break;
/* DECRQPSR */		case 'w':	SendPresentationStateReports(); break;
/* DECRPM */		case 'y':	break; // Report sent as application output is ignored.
/* DECRQM */		case 'p':	[[clang::fallthrough]]; // TODO: Not (yet) implemented.
			default:
#if defined(DEBUG)
				std::clog << "Unknown CSI " << last_intermediate << ' ' << character << "\n";
#endif
				break;
		} else
		if ('?' == first_private_parameter) switch (character) {
/* DECRPM */		case 'y':	break; // Report sent as application output is ignored.
/* DECRQM */		case 'p':	[[clang::fallthrough]]; // TODO: Not (yet) implemented.
			default:
#if defined(DEBUG)
				std::clog << "Unknown DEC Private CSI " << first_private_parameter << ' ' << last_intermediate << ' ' << character << "\n";
#endif
				break;
		} else {
#if defined(DEBUG)
			std::clog << "Unknown Private CSI " << first_private_parameter << ' ' << last_intermediate << ' ' << character << "\n";
#endif
		}
	} else
	if ('*' == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
/* DECSNLS */		case '|':	SetLinesPerPage(); break;
			default:
#if defined(DEBUG)
				std::clog << "Unknown CSI " << last_intermediate << ' ' << character << "\n";
#endif
				break;
		} else {
#if defined(DEBUG)
			std::clog << "Unknown Private CSI " << first_private_parameter << ' ' << last_intermediate << ' ' << character << "\n";
#endif
		}
	} else
	if (' ' == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
/* SL */		case '@':	ScrollLeft(GetArgZDIfZeroOneIfEmpty(0U)); break;
/* SR */		case 'A':	ScrollRight(GetArgZDIfZeroOneIfEmpty(0U)); break;
/* GSM */		case 'B':	break;	// Graphic Size Modification has no meaning.
/* GSS */		case 'C':	break;	// Graphic Size Selection has no meaning.
/* FNT */		case 'D':	break;	// Font Selection has no meaning.
/* TSS */		case 'E':	break;	// Thin Space Selection has no meaning.
/* JFY */		case 'F':	break;	// Justify has no meaning.
/* SPI */		case 'G':	break;	// Spacing Increment has no meaning.
/* QUAD */		case 'H':	break;	// Quadding is not implemented.
/* FNK */		case 'W':	break;	// Ignore function keys output by applications.
/* DECSCUSR */		case 'q':	SetCursorStyle(); break;
			default:
#if defined(DEBUG)
				std::clog << "Unknown CSI " << last_intermediate << ' ' << character << "\n";
#endif
				break;
		} else {
#if defined(DEBUG)
			std::clog << "Unknown Private CSI " << first_private_parameter << ' ' << last_intermediate << ' ' << character << "\n";
#endif
		}
	} else
	if ('!' == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
/* DECSTR */		case 'p':	SoftReset(); break;
			default:
#if defined(DEBUG)
				std::clog << "Unknown CSI " << last_intermediate << ' ' << character << "\n";
#endif
				break;
		} else {
#if defined(DEBUG)
			std::clog << "Unknown Private CSI " << first_private_parameter << ' ' << last_intermediate << ' ' << character << "\n";
#endif
		}
	} else
	if ('\'' == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
/* DECEFR */		case 'w':	break; // Enable Filter Rectangle implies a complex multi-window model that is beyond the scope of this emulation.
/* DECELR */		case 'z':	EnableLocatorReports(); break;
/* DECSLE */		case '{':	SelectLocatorEvents(); break;
/* DECRQLP */		case '|':	RequestLocatorReport(); break;
			default:
#if defined(DEBUG)
				std::clog << "Unknown CSI " << last_intermediate << ' ' << character << "\n";
#endif
				break;
		} else {
#if defined(DEBUG)
			std::clog << "Unknown Private CSI " << first_private_parameter << ' ' << last_intermediate << ' ' << character << "\n";
#endif
		}
	} else {
#if defined(DEBUG)
		std::clog << "Unknown CSI " << last_intermediate << ' ' << character << "\n";
#endif
	}
}

void
SoftTerm::ControlString(
	char32_t /*start_char*/,
	char32_t /*term_char*/
) {
#if defined(DEBUG)
	std::clog << "Unexpected control string\n";
#endif
}

void
SoftTerm::PrintableCharacter(
	bool error,
	unsigned short shift_level,
	char32_t character
) {
	/// FIXME: \bug We don't handle bidirectional printing.
	if (UnicodeCategorization::IsOtherFormat(character)
	||  UnicodeCategorization::IsOtherSurrogate(character)
	) {
		// Do nothing.
	} else
	{
		const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
		const CharacterCell::attribute_type a(attributes ^ (error || 1U != shift_level ? CharacterCell::INVERSE : 0));

		ClearPendingAdvance();
		const ScreenBuffer::coordinate s(stride * active_cursor.y + active_cursor.x);

		CharacterCell cell(character, a, colour), space(SPC, a, colour), read;
		screen.ReadCell(s, read);
		if (UnicodeCategorization::IsMarkNonSpacing(read.character)
		||  UnicodeCategorization::IsMarkEnclosing(read.character)
		) {
			if (!UnicodeKeyboard::combine_unicode(read.character, cell.character)) {
				if (!overstrike) {
					char32_t non_combining(SPC);
					if (UnicodeKeyboard::combine_peculiar_non_combiners(read.character, non_combining)) {
						read.character = non_combining;
						screen.WriteNCells(s, 1U, read);
						InsertCharacters(1U);
					}
				}
			}
		} else
		{
			if (!overstrike) InsertCharacters(1U);
		}
		screen.WriteNCells(s, 1U, cell);

		if (!UnicodeCategorization::IsMarkNonSpacing(cell.character)
		&&  !UnicodeCategorization::IsMarkEnclosing(cell.character)
		) {
			AdvanceOrPend();
			if (!square && UnicodeCategorization::IsWideOrFull(cell.character)) {
				const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);

				if (active_cursor.x < right_margin) {
					ClearPendingAdvance();
					const ScreenBuffer::coordinate s1(stride * active_cursor.y + active_cursor.x);

					if (!overstrike) InsertCharacters(1U);
					screen.WriteNCells(s1, 1U, space);
					AdvanceOrPend();
				}
			}
		}
	}
	last_printable_character = character;	// even if it is a format or surrogate
}
