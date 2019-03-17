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

#include <iostream>	// for debugging

/* Constructor and destructor ***********************************************
// **************************************************************************
*/

static const CharacterCell::colour_type erased_foreground(ALPHA_FOR_ERASED,0xC0,0xC0,0xC0), erased_background(ALPHA_FOR_ERASED,0U,0U,0U);
static const CharacterCell::colour_type default_foreground(ALPHA_FOR_DEFAULT,0xC0,0xC0,0xC0), default_background(ALPHA_FOR_DEFAULT,0U,0U,0U);

SoftTerm::SoftTerm(
	SoftTerm::ScreenBuffer & s,
	SoftTerm::KeyboardBuffer & k,
	SoftTerm::MouseBuffer & m,
	SoftTerm::coordinate w,
	SoftTerm::coordinate h
) :
	utf8_decoder(*this),
	ecma48_decoder(*this, false /* no control strings */, true /* permit cancel */, true /* permit 7-bit extensions*/, false /* no Interix shift state */, false /* no RXVT final $ in CSI bodge */),
	screen(s),
	keyboard(k),
	mouse(m),
	scroll_margin(w,h),
	display_margin(w,h),
	scrolling(true),
	overstrike(true),
	square(true),
	no_clear_screen_on_column_change(false),
	advance_pending(false),
	attributes(0),
	foreground(default_foreground),
	background(default_background),
	cursor_type(CursorSprite::BLOCK),
	cursor_attributes(CursorSprite::VISIBLE|CursorSprite::BLINK),
	send_DECLocator(false),
	send_XTermMouse(false)
{
	Resize(display_origin.x + display_margin.w, display_origin.y + display_margin.h);
	SetRegularHorizontalTabstops(8U);
	ClearAllVerticalTabstops();
	UpdateCursorType();
	Home();
	ClearDisplay();
	keyboard.SetBackspaceIsBS(false);
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

	if (active_cursor.x >= columns) active_cursor.x = columns - 1U;
	if (active_cursor.y >= rows) active_cursor.y = rows - 1U;
	UpdateCursorPos();
}

/* Control sequence arguments ***********************************************
// **************************************************************************
*/

static inline
SoftTerm::coordinate
OneIfZero (
	const SoftTerm::coordinate & v
) {
	return 0U == v ? 1U : v;
}

SoftTerm::coordinate 
SoftTerm::SumArgs()
{
	coordinate s(0U);
	for (std::size_t i(0U); i < argc; ++i)
		s += args[i];
	return s;
}

/* Editing ******************************************************************
// **************************************************************************
*/

CharacterCell 
SoftTerm::ErasureCell(uint32_t c)
{
	return CharacterCell(c, attributes, active_modes.background_colour_erase ? foreground : erased_foreground, active_modes.background_colour_erase ? background : erased_background);
}

void 
SoftTerm::ClearDisplay(uint32_t c)
{
	// Erasure ignores margins.
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const ScreenBuffer::coordinate s(stride * display_origin.y);
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
	const ScreenBuffer::coordinate s(stride * display_origin.y);
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
SoftTerm::EraseCharacters(coordinate n) 
{
	// Erasure ignores margins.
	const ScreenBuffer::coordinate columns(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const ScreenBuffer::coordinate s(columns * active_cursor.y + active_cursor.x);
	if (active_cursor.x + n >= columns) n = columns - 1U - active_cursor.x;
	screen.WriteNCells(s, n, ErasureCell());
}

void 
SoftTerm::DeleteCharacters(coordinate n) 
{
	// Deletion always operates only inside the margins.
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	const ScreenBuffer::coordinate s(stride * active_cursor.y + active_cursor.x);
	const ScreenBuffer::coordinate e(stride * active_cursor.y + right_margin + 1U);
	screen.ScrollUp(s, e, n, ErasureCell());
}

void 
SoftTerm::InsertCharacters(coordinate n) 
{
	// Insertion always operates only inside the margins.
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	const ScreenBuffer::coordinate s(stride * active_cursor.y + active_cursor.x);
	const ScreenBuffer::coordinate e(stride * active_cursor.y + right_margin + 1U);
	screen.ScrollDown(s, e, n, ErasureCell());
}

void 
SoftTerm::DeleteLinesInScrollAreaAt(coordinate top, coordinate n) 
{
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const coordinate bottom_margin(scroll_origin.y + scroll_margin.h - 1U);
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	const coordinate left_margin(scroll_origin.x);
	if (left_margin != 0U || right_margin != stride - 1U) {
		const coordinate w(right_margin - left_margin + 1U);
		for (ScreenBuffer::coordinate r(top); r <= bottom_margin - n; ++r) {
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
SoftTerm::InsertLinesInScrollAreaAt(coordinate top, coordinate n) 
{
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	const coordinate left_margin(scroll_origin.x);
	const coordinate bottom_margin(scroll_origin.y + scroll_margin.h - 1U);
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
SoftTerm::DeleteColumnsInScrollAreaAt(coordinate left, coordinate n) 
{
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const coordinate bottom_margin(scroll_origin.y + scroll_margin.h - 1U);
	const coordinate top_margin(scroll_origin.y);
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	if (left >= right_margin) return;
	if (n > right_margin - left) n = right_margin - left;
	const coordinate w(right_margin - left - n);
	for (ScreenBuffer::coordinate r(top_margin); r <= bottom_margin; ++r) {
		const ScreenBuffer::coordinate d(stride * r + left);
		const ScreenBuffer::coordinate s(stride * r + left + n);
		screen.CopyNCells(d, s, w);
		screen.WriteNCells(d + w, n, ErasureCell());
	}
}

void 
SoftTerm::InsertColumnsInScrollAreaAt(coordinate left, coordinate n) 
{
	const ScreenBuffer::coordinate stride(ScreenBuffer::coordinate(display_margin.w) + display_origin.x);
	const coordinate bottom_margin(scroll_origin.y + scroll_margin.h - 1U);
	const coordinate top_margin(scroll_origin.y);
	const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);
	if (left >= right_margin) return;
	if (n > right_margin - left) n = right_margin - left;
	const coordinate w(scroll_margin.w - left - n);
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
	for (std::size_t i(0U); i < argc; ++i) {
		switch (args[i]) {
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
	for (std::size_t i(0U); i < argc; ++i) {
		switch (args[i]) {
			case 0:	ClearToEOL(); break;
			case 1:	ClearFromBOL(); break;
			case 2:	ClearLine(); break;
		}
	}
}

void 
SoftTerm::InsertLines(coordinate n)
{
	// Insertion always operates only inside the margins.
	InsertLinesInScrollAreaAt(active_cursor.y, n);
}

void 
SoftTerm::DeleteLines(coordinate n)
{
	// Deletion always operates only inside the margins.
	DeleteLinesInScrollAreaAt(active_cursor.y, n);
}

void 
SoftTerm::ScrollDown(coordinate n)
{
	// Scrolling always operates only inside the margins.
	InsertLinesInScrollAreaAt(scroll_origin.y, n);
}

void 
SoftTerm::ScrollUp(coordinate n)
{
	// Scrolling always operates only inside the margins.
	DeleteLinesInScrollAreaAt(scroll_origin.y, n);
}

void 
SoftTerm::ScrollLeft(coordinate n)
{
	// Scrolling always operates only inside the margins.
	DeleteColumnsInScrollAreaAt(scroll_origin.x, n);
}

void 
SoftTerm::ScrollRight(coordinate n)
{
	// Scrolling always operates only inside the margins.
	InsertColumnsInScrollAreaAt(scroll_origin.x, n);
}

/* Tabulation ***************************************************************
// **************************************************************************
*/

void 
SoftTerm::CursorTabulationControl()
{
	for (std::size_t i(0U); i < argc; ++i) {
		switch (args[i]) {
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
	for (std::size_t i(0U); i < argc; ++i) {
		switch (args[i]) {
/* DECST8C */		case 5: SetRegularHorizontalTabstops(8U); break;
		}
	}
}

void 
SoftTerm::TabClear()
{
	for (std::size_t i(0U); i < argc; ++i) {
		switch (args[i]) {
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
	coordinate n,
	bool apply_margins
) {
	advance_pending = false;
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
	coordinate n,
	bool apply_margins
) {
	advance_pending = false;
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
	coordinate n,
	bool apply_margins
) {
	advance_pending = false;
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
	coordinate n
) {
	for (unsigned short p(0U); p < 256U; ++p)
		SetHorizontalTabstopAt(p, 0U == (p % n));
}

void 
SoftTerm::ClearAllHorizontalTabstops()
{
	for (unsigned short p(0U); p < 256U; ++p)
		SetHorizontalTabstopAt(p, false);
}

void 
SoftTerm::ClearAllVerticalTabstops()
{
	for (unsigned short p(0U); p < 256U; ++p)
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
SoftTerm::SaveCursor()
{
	saved_cursor = active_cursor;
}

void 
SoftTerm::RestoreCursor()
{
	advance_pending = false;
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
	advance_pending = false;
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
SoftTerm::CarriageReturnNoUpdate()
{
	advance_pending = false;
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
	advance_pending = false;
	if (active_modes.origin)
		active_cursor = scroll_origin;
	else
		active_cursor = display_origin;
	UpdateCursorPos();
}

void 
SoftTerm::GotoX(coordinate n)
{
	advance_pending = false;
	const coordinate columns(active_modes.origin ? scroll_margin.w : display_margin.w);
	if (n > columns) n = columns;
	n += (active_modes.origin ? scroll_origin.x : display_origin.x);
	active_cursor.x = n - 1U;
	UpdateCursorPos();
}

void 
SoftTerm::GotoY(coordinate n)
{
	advance_pending = false;
	const coordinate rows(active_modes.origin ? scroll_margin.h : display_margin.h);
	if (n > rows) n = rows;
	n += (active_modes.origin ? scroll_origin.y : display_origin.y);
	active_cursor.y = n - 1U;
	UpdateCursorPos();
}

void 
SoftTerm::GotoYX()
{
	advance_pending = false;
	coordinate n(argc > 0U ? OneIfZero(args[0U]) : 1U);
	coordinate m(argc > 1U ? OneIfZero(args[1U]) : 1U);
	const coordinate columns(active_modes.origin ? scroll_margin.w : display_margin.w);
	const coordinate rows(active_modes.origin ? scroll_margin.h : display_margin.h);
	if (n > rows) n = rows;
	if (m > columns) m = columns;
	n += (active_modes.origin ? scroll_origin.y : display_origin.y);
	m += (active_modes.origin ? scroll_origin.x : display_origin.x);
	active_cursor.y = n - 1U;
	active_cursor.x = m - 1U;
	UpdateCursorPos();
}

void 
SoftTerm::SetTopBottomMargins()
{
	coordinate new_top_margin(argc > 0U ? OneIfZero(args[0U]) - 1U : display_origin.y);
	coordinate new_bottom_margin(argc > 1U && args[1U] > 0U ? args[1U] - 1U : display_origin.y + display_margin.h - 1U);
	if (new_top_margin < new_bottom_margin) {
		scroll_origin.y = new_top_margin;
		scroll_margin.h = new_bottom_margin - new_top_margin + 1U;
		Home();
	}
}

void 
SoftTerm::SetLeftRightMargins()
{
	if (!active_modes.left_right_margins) return;
	coordinate new_left_margin(argc > 0U ? OneIfZero(args[0U]) - 1U : display_origin.y);
	coordinate new_right_margin(argc > 1U && args[1U] > 0U ? args[1U] - 1U : display_origin.y + display_margin.w - 1U);
	if (new_left_margin < new_right_margin) {
		scroll_origin.x = new_left_margin;
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
	coordinate n,
	bool apply_margins,
	bool scroll_at_top
) {
	advance_pending = false;
	coordinate top_margin(scroll_origin.y), top(top_margin);
	if (active_cursor.y < top)
		top = display_origin.y;
	if (active_cursor.y > top) {
		const coordinate l(active_cursor.y - top);
		if (l >= n) {
			active_cursor.y -= n;
			n = 0U;
		} else {
			n -= l;
			active_cursor.y = top;
		}
		UpdateCursorPos();
	}
	if (active_cursor.y < top_margin) {
		if (apply_margins) {
			active_cursor.y = top_margin;
			UpdateCursorPos();
		}
	} else if (n > 0U && scroll_at_top)
		ScrollDown(n);
}

void 
SoftTerm::CursorDown(
	coordinate n,
	bool apply_margins,
	bool scroll_at_bottom
) {
	advance_pending = false;
	coordinate bottom_margin(scroll_origin.y + scroll_margin.h - 1U), bot(bottom_margin);
	if (active_cursor.y > bot)
		bot = display_origin.y + display_margin.h - 1U;
	if (active_cursor.y < bot) {
		const coordinate l(bot - active_cursor.y);
		if (l >= n) {
			active_cursor.y += n;
			n = 0U;
		} else {
			n -= l;
			active_cursor.y = bot;
		}
		UpdateCursorPos();
	}
	if (active_cursor.y > bottom_margin) {
		if (apply_margins) {
			active_cursor.y = bottom_margin;
			UpdateCursorPos();
		}
	} else if (n > 0U && scroll_at_bottom)
		ScrollUp(n);
}

void 
SoftTerm::CursorLeft(
	coordinate n,
	bool apply_margins,
	bool scroll_at_left
) {
	advance_pending = false;
	coordinate left_margin(scroll_origin.x), lmm(left_margin);
	if (active_cursor.x > lmm)
		lmm = display_origin.x;
	if (active_cursor.x > lmm) {
		const coordinate l(active_cursor.x - lmm);
		if (l >= n) {
			active_cursor.x -= n;
			n = 0U;
		} else {
			n -= l;
			active_cursor.x = lmm;
		}
		UpdateCursorPos();
	}
	if (active_cursor.x < left_margin) {
		if (apply_margins) {
			active_cursor.x = left_margin;
			UpdateCursorPos();
		}
	} else if (n > 0U && scroll_at_left)
		ScrollRight(n);
}

void 
SoftTerm::CursorRight(
	coordinate n,
	bool apply_margins,
	bool scroll_at_right
) {
	advance_pending = false;
	coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U), rmm(right_margin);
	if (active_cursor.x > rmm)
		rmm = display_origin.x + display_margin.w - 1U;
	if (active_cursor.x < rmm) {
		const coordinate l(rmm - active_cursor.x);
		if (l >= n) {
			active_cursor.x += n;
			n = 0U;
		} else {
			n -= l;
			active_cursor.x = rmm;
		}
		UpdateCursorPos();
	}
	if (active_cursor.x > right_margin) {
		if (apply_margins) {
			active_cursor.x = right_margin;
			UpdateCursorPos();
		}
	} else if (n > 0U && scroll_at_right)
		ScrollLeft(n);
}

/* Colours, modes, and attributes *******************************************
// **************************************************************************
*/

void 
SoftTerm::SetAttributes()
{
	for (std::size_t i(0U); i < argc; ) {
		if (i + 3U <= argc && 38U == args[i + 0U] && 5U == args[i + 1U]) {
			foreground = Map256Colour(args[i + 2U] % 256U);
			i += 3U;
		} else
		if (i + 3U <= argc && 48U == args[i + 0U] && 5U == args[i + 1U]) {
			background = Map256Colour(args[i + 2U] % 256U);
			i += 3U;
		} else
		if (i + 5U <= argc && 38U == args[i + 0U] && 2U == args[i + 1U]) {
			foreground = MapTrueColour(args[i + 2U] % 256U,args[i + 3U] % 256U,args[i + 4U] % 256U);
			i += 5U;
		} else
		if (i + 5U <= argc && 48U == args[i + 0U] && 2U == args[i + 1U]) {
			background = MapTrueColour(args[i + 2U] % 256U,args[i + 3U] % 256U,args[i + 4U] % 256U);
			i += 5U;
		} else
			SetAttribute (args[i++]);
	}
}

void 
SoftTerm::SetModes(bool flag)
{
	for (std::size_t i(0U); i < argc; ++i)
		SetMode (args[i], flag);
}

void 
SoftTerm::SetPrivateModes(bool flag)
{
	for (std::size_t i(0U); i < argc; ++i)
		SetPrivateMode (args[i], flag);
}

void
SoftTerm::SGR0()
{
	attributes = 0U; 
	foreground = default_foreground; 
	background = default_background; 
}

void 
SoftTerm::SetAttribute(unsigned int a)
{
	switch (a) {
		default:
			std::clog << "Unknown attribute : " << a << "\n";
			break;
		case 0U:	SGR0(); break;
		case 1U:	attributes |= CharacterCell::BOLD; break;
		case 2U:	attributes |= CharacterCell::FAINT; break;
		case 3U:	attributes |= CharacterCell::ITALIC; break;
		case 4U:	attributes = (attributes & ~CharacterCell::UNDERLINES) | CharacterCell::SIMPLE_UNDERLINE; break;
		case 5U:	attributes |= CharacterCell::BLINK; break;
		case 7U:	attributes |= CharacterCell::INVERSE; break;
		case 8U:	attributes |= CharacterCell::INVISIBLE; break;
		case 9U:	attributes |= CharacterCell::STRIKETHROUGH; break;
		case 22U:	attributes &= ~(CharacterCell::BOLD|CharacterCell::FAINT); break;
		case 23U:	attributes &= ~CharacterCell::ITALIC; break;
		case 24U:	attributes &= ~CharacterCell::UNDERLINES; break;
		case 25U:	attributes &= ~CharacterCell::BLINK; break;
		case 27U:	attributes &= ~CharacterCell::INVERSE; break;
		case 28U:	attributes &= ~CharacterCell::INVISIBLE; break;
		case 29U:	attributes &= ~CharacterCell::STRIKETHROUGH; break;
		case 30U: case 31U: case 32U: case 33U:
		case 34U: case 35U: case 36U: case 37U:	
				foreground = Map16Colour(a -  30U); break;
		case 39U:	foreground = default_foreground; break;
		case 40U: case 41U: case 42U: case 43U:
		case 44U: case 45U: case 46U: case 47U:	
				background = Map16Colour(a -  40U); break;
		case 49U:	background = default_background; break;
		case 90U: case 91U: case 92U: case 93U:
		case 94U: case 95U: case 96U: case 97U:	
				foreground = Map16Colour(a -  90U + 8U); break;
		case 100U: case 101U: case 102U: case 103U:
		case 104U: case 105U: case 106U: case 107U:	
				background = Map16Colour(a - 100U + 8U); break;
		// ECMA-48 defines these as font changes.  We don't provide that.  
		// The Linux console defines them as something else.  We don't provide that, either.
		case 10U:	break;
		case 11U:	break;
	}
}

void 
SoftTerm::SaveAttributes()
{
	saved_attributes = attributes;
	saved_foreground = foreground;
	saved_background = background;
}

void 
SoftTerm::RestoreAttributes()
{
	attributes = saved_attributes;
	foreground = saved_foreground;
	background = saved_background;
}

void 
SoftTerm::SetLinesPerPageOrDTTerm()
{
	// This is a bodge to accommodate progreams such as NeoVIM that hardwire this xterm control sequence.
	// xterm is not strictly compatible here, as it gives quite different meanings to values of n less than 24.
	// This is an extension that began with dtterm.
	// A true DEC VT520 rounds up to the lowest possible size, which is 24.
	if (3 == argc && 8U == args[0]) {
		const coordinate rows(args[1]);
		const coordinate columns(args[2]);
		if (columns >= 2U && rows >= 2U)
			Resize(columns, rows);
	} else
		SetLinesPerPage();
}

void 
SoftTerm::SetLinesPerPage()
{
	if (argc) {
		const coordinate n(args[argc - 1U]);
		// The DEC VT minimum is 24 rows; we are more liberal since we are not constrained by CRT hardware.
		if (n >= 2U)
			Resize(0U, n);
	}
}

void 
SoftTerm::SetColumnsPerPage()
{
	if (argc) {
		const coordinate n(args[argc - 1U]);
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
SoftTerm::SetMode(unsigned int a, bool f)
{
	switch (a) {
/* IRM */	case 4U:	overstrike = !f; break;

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
			std::clog << "Unknown mode : " << a << "\n";
			break;
	}
}

void 
SoftTerm::SetPrivateMode(unsigned int a, bool f)
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
/* DECOM */	case 6U:	active_modes.origin = f; break;
/* DECAWM */	case 7U:	active_modes.automatic_right_margin = f; break;
		case 12U:
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
		case 1037U:	keyboard.SetDeleteIsDEL(f); break	;
		case 2004U:	keyboard.SetSendPasteEvent(f); break	;
		case 1369U:	square = f; break;

		// ############## Intentionally unimplemented private modes
		case 8U:	// DECARM (autorepeat)
		case 68U:	// DECKBUM (main keypad data processing, i.e. group 2 lock)
			// The terminal emulator is entirely decoupled from the physical keyboard; making these meaningless.
			break;
		case 1004U:	// xterm GUI focus event reports
			// The terminal emulator is entirely decoupled from the realizer; making these meaningless.
			break;
		case 2U:	break;	// DECANM (ANSI) has no meaning as we are never in VT52 mode.
		case 4U:	break;	// DECSCLM (slow a.k.a. smooth scroll) is not useful.
		case 9U:	break;	// This is an old ambiguous to decode mouse protocol, since superseded.
		case 11U:	break;	// DECLTM (line transmission) has no meaning.
		case 18U:	break;	// DECPFF (print Form Feed) has no meaning as we have no printer.
		case 19U:	break;	// DECPEXT (print extent) has no meaning as we have no printer.
		case 1001U:	break;	// This is a mouse grabber, tricky and largely unused in the wild.
		case 1005U:	break;	// This is an old ambiguous to decode mouse protocol, since superseded.
		case 1015U:	break;	// This is an old mouse protocol, since superseded.

		// ############## Only partly implemented private modes
		case 1049U:	/// \bug FIXME This should combine 1047/47 and 1048 in one.
		case 1048U:	// Save/restore cursor position, equivalent to DECSC/DECRC
				f ? SaveCursor() : RestoreCursor();
				f ? SaveAttributes() : RestoreAttributes();
				f ? SaveModes() : RestoreModes();
				break;

		// ############## As yet unimplemented or simply unknown private modes
		case 5U:	// DECSCNM (light background/whole screen reverse video) is not implemented.
		case 10U:	// DECEDM (edit)
		case 13U:	// DECSCFDN (space compression/field delimiting)
		case 16U:	// DECEKEM (edit key execution)
#if 0 /// TODO:
		case 1007U:	/// \todo Wheel mouse events when the alternate screen buffer is on
		case 47U:	/// \todo Switch alternate screen buffer
		case 1047U:	/// \todo Switch alternate screen buffer
#endif
		default:
			std::clog << "Unknown private mode : " << a << "\n";
			break;
	}
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
SoftTerm::SCOSCorDESCSLRM()
{
	// SCOSC (SCO console save cursor) is the same control sequence as DECSLRM (DEC VT set left and right margins).
	// SCOSC is what the Linux and FreeBSD consoles implement and it is listed in the termcap/terminfo entries, so we cannot simply omit it.
	// This is pretty much the same bodge to solve this as used by xterm.
	if (active_modes.left_right_margins || argc > 0U)
		SetLeftRightMargins();
	else {
		SaveCursor(); 
		SaveAttributes(); 
		SaveModes(); 
	}
}

void 
SoftTerm::SetSCOAttributes()
{
	if (argc < 1U) return;	// SCO SGR must always have an initial subcommand parameter.
	switch (args[0]) {
		case 0U:
			foreground = default_foreground;
			background = default_background;
			break;
		case 1U:
			if (argc > 1U)
				background = Map256Colour(args[1] % 256U);
			break;
		case 2U:
			if (argc > 1U)
				foreground = Map256Colour(args[1] % 256U);
			break;
		default:
			std::clog << "Unknown SCO attribute : " << args[0] << "\n";
			break;
	}
}

void 
SoftTerm::SetSCOCursorType()
{
	if (argc != 1U) return;	// We don't do custom cursor shapes.
	switch (args[0]) {
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
	if (argc < 1U) return;
	switch (args[0] & 0x0F) {
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
	for (std::size_t i(0U); i < argc; ++i)
		switch (args[i]) {
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
		}
	UpdateCursorType();
}

void 
SoftTerm::SendPrimaryDeviceAttributes()
{
	for (std::size_t i(0U); i < argc; ++i)
		SendPrimaryDeviceAttribute (args[i]);
}

void 
SoftTerm::SendSecondaryDeviceAttributes()
{
	for (std::size_t i(0U); i < argc; ++i)
		SendSecondaryDeviceAttribute (args[i]);
}

void 
SoftTerm::SendTertiaryDeviceAttributes()
{
	for (std::size_t i(0U); i < argc; ++i)
		SendTertiaryDeviceAttribute (args[i]);
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
SoftTerm::SendPrimaryDeviceAttribute(unsigned int a)
{
	switch (a) {
		case 0U:	
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(sizeof vt220_device_attribute1 - 1, vt220_device_attribute1); 
			break;
		default:
			std::clog << "Unknown primary device attribute request : " << a << "\n";
			break;
	}
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
SoftTerm::SendSecondaryDeviceAttribute(unsigned int a)
{
	switch (a) {
		case 0U:	
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(sizeof vt220_device_attribute2 - 1, vt220_device_attribute2); 
			break;
		default:
			std::clog << "Unknown secondary device attribute request : " << a << "\n";
			break;
	}
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
SoftTerm::SendTertiaryDeviceAttribute(unsigned int a)
{
	switch (a) {
		case 0U:	
			keyboard.WriteControl1Character(DCS);
			keyboard.WriteLatin1Characters(sizeof vt220_device_attribute3 - 1, vt220_device_attribute3); 
			keyboard.WriteControl1Character(ST);
			break;
		default:
			std::clog << "Unknown tertiary device attribute request : " << a << "\n";
			break;
	}
}

void 
SoftTerm::SendDeviceStatusReports()
{
	for (std::size_t i(0U); i < argc; ++i)
		SendDeviceStatusReport (args[i]);
}

void 
SoftTerm::SendPrivateDeviceStatusReports()
{
	for (std::size_t i(0U); i < argc; ++i)
		SendPrivateDeviceStatusReport (args[i]);
}

static 
const char 
operating_status_report[] = 
	"0" 		// operating OK
	"n";

void 
SoftTerm::SendDeviceStatusReport(unsigned int a)
{
	switch (a) {
		case 5U:	
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(sizeof operating_status_report - 1, operating_status_report); 
			break;
		case 6U:
		{
			char b[32];
			const int n(snprintf(b, sizeof b, "%u;%uR", active_cursor.y + 1U, active_cursor.x + 1U));
			keyboard.WriteControl1Character(CSI);
			keyboard.WriteLatin1Characters(n, b); 
			break;
		}
		default:
			std::clog << "Unknown device attribute request : " << a << "\n";
			break;
	}
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
SoftTerm::SendPrivateDeviceStatusReport(unsigned int a)
{
	switch (a) {
		case 6U:
		{
			char b[32];
			const int n(snprintf(b, sizeof b, "%u;%u;1R", active_cursor.y + 1U, active_cursor.x + 1U));
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
			std::clog << "Unknown device attribute request : " << a << "\n";
			break;
	}
}

/* Locator control **********************************************************
// **************************************************************************
*/

void 
SoftTerm::RequestLocatorReport()
{
	if (0 == argc)
		mouse.RequestDECLocatorReport();
	else
	for (std::size_t i(0U); i < argc; ++i)
		mouse.RequestDECLocatorReport();
}

void 
SoftTerm::EnableLocatorReports()
{
	send_DECLocator = argc > 0U ? args[0U] : false;
	mouse.SetSendDECLocator(send_DECLocator);
	UpdatePointerType();
	if (2 <= argc && 1U == args[1U])
		std::clog << "Pixel coordinate locator report request denied.\n";
}

void 
SoftTerm::SelectLocatorEvents()
{
	if (0 == argc)
		SelectLocatorEvent(0U);
	else
	for (std::size_t i(0U); i < argc; ++i)
		SelectLocatorEvent(args[i]);
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
			std::clog << "Unknown locator event selected : " << a << "\n";
			break;
	}
}

/* Top-level output functions ***********************************************
// **************************************************************************
*/

void 
SoftTerm::ProcessDecodedUTF8(
	uint32_t character,
	bool decoder_error,
	bool overlong
) {
	ecma48_decoder.Process(character, decoder_error, overlong);
}

void
SoftTerm::SoftReset()
{
	advance_pending = false;
	ResetMargins(); 
	SetRegularHorizontalTabstops(8U);
	cursor_attributes = CursorSprite::VISIBLE|CursorSprite::BLINK;
	cursor_type = CursorSprite::BLOCK; 
	UpdateCursorType();
	SGR0();
	keyboard.SetCursorApplicationMode(false); 	
	keyboard.SetCalculatorApplicationMode(false); 	
	keyboard.SetBackspaceIsBS(false);
	saved_modes = active_modes = mode();
	saved_cursor = display_origin;
	scrolling = true;
	overstrike = true;
}

void
SoftTerm::ResetToInitialState()
{
	Resize(80U, 25U);
	Home(); 
	ClearDisplay(); 
	no_clear_screen_on_column_change = false;
	// Per the VT420 programmers' reference, if one ignores serial comms RIS does only a few things more than DECSTR.
	ResetMargins(); 
	SetRegularHorizontalTabstops(8U);
	cursor_attributes = CursorSprite::VISIBLE|CursorSprite::BLINK;
	cursor_type = CursorSprite::BLOCK; 
	UpdateCursorType();
	SGR0();
	keyboard.SetCursorApplicationMode(false); 	
	keyboard.SetCalculatorApplicationMode(false); 	
	keyboard.SetBackspaceIsBS(false);
	saved_modes = active_modes = mode();
	saved_cursor = active_cursor;
	scrolling = true;
	overstrike = true;
}

void 
SoftTerm::ControlCharacter(uint_fast32_t character)
{
	switch (character) {
		case NUL:	break;
		case BEL:	/* TODO: bell */ break;
		case CR:	CarriageReturn(); break;
		case NEL:	CarriageReturnNoUpdate(); CursorDown(1U, true, scrolling); break;
		case IND: case LF: case VT: case FF:
				CursorDown(1U, true, scrolling); break;
		case RI:	CursorUp(1U, true, scrolling); break;
		case TAB:	HorizontalTab(1U, true); break;
		case BS:	CursorLeft(1U, true, false); break;
		case DEL:	DeleteCharacters(1U); break;
		case HTS:	SetHorizontalTabstop(); break;
		// These are wholly dealt with by the ECMA-48 decoder.
		case ESC: case CSI: case SS2: case SS3: case CAN: case DCS: case OSC: case PM: case APC: case SOS: case ST:
				break;
		default:	break;
	}
}

void 
SoftTerm::EscapeSequence(uint_fast32_t character, char last_intermediate)
{
	switch (last_intermediate) {
		default:	break;
		case NUL:
			switch (character) {
				default:	break;
/* DECGON */			case '1':	break;	// We are never in VT105 mode.
/* DECGOFF */			case '2':	break;	// We are never in VT105 mode.
/* DECVTS */			case '3':	break;	// We are never in LA120 mode.
/* DECCAVT */			case '4':	break;	// We are never in LA120 mode.
/* DECXMT */			case '5':	break;	// Unimplemented.
/* DECBI */			case '6':	CursorLeft(1U, true, true); break;
/* DECSC */			case '7':	SaveCursor(); SaveAttributes(); SaveModes(); break;
/* DECRC */			case '8':	RestoreCursor(); RestoreAttributes(); RestoreModes(); break;
/* DECFI */			case '9':	CursorRight(1U, true, true); break;
/* DECANSI */			case '<':	break;	// We are never in VT52 mode.
/* DECKPAM */			case '=':	keyboard.SetCalculatorApplicationMode(true); break;
/* DECKPNM */			case '>':	keyboard.SetCalculatorApplicationMode(false); break;
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
		case ' ':
			switch (character) {
				default:	break;
/* S7C1T */			case 'F':	keyboard.Set8BitControl1(false); break;
/* S8C1T */			case 'G':	keyboard.Set8BitControl1(true); break;
			}
			break;
		case '#':
			switch (character) {
				default:	break;
/* DECALN */			case '8':	ResetMargins(); Home(); ClearDisplay('E'); break;
			}
			break;
	}
}

void 
SoftTerm::ControlSequence(
	uint_fast32_t character,
	char last_intermediate,
	char first_private_parameter
) {
	// Finish the final argument, using the relevant defaults.
	if (NUL == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
			case 'c':
			case 'g':
			case 'm':
			case 'J':
			case 'K':
			case 'x':
			case 'r':
				FinishArg(0U); 
				break;
			case 'H':
			case 'f':
				FinishArg(1U); 
				if (argc < 2U) FinishArg(1U); 
				break;
			default:	
				FinishArg(1U); 
				break;
		} else
		if ('?' == first_private_parameter) {
			FinishArg(1U); 
		} else
		if ('>' == first_private_parameter) {
			FinishArg(0U); 
		} else
		if ('=' == first_private_parameter) {
			FinishArg(0U); 
		} else
			FinishArg(1U); 
	} else
	if ('$' == last_intermediate) {
		FinishArg(1U); 
	} else
	if ('*' == last_intermediate) {
		FinishArg(1U); 
	} else
	if (' ' == last_intermediate) {
		FinishArg(1U); 
	} else
	if ('!' == last_intermediate) {
		FinishArg(1U); 
	} else
	if ('\'' == last_intermediate) switch (character) {
/* DECSLE */	case '{':	
/* DECRQLP */	case '|':	
/* DECELR */	case 'z':	
			FinishArg(0U); 
			break;
		default:	
			FinishArg(1U); 
			break;
	} else
		FinishArg(1U); 

	// Enact the action.
	if (NUL == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
// ---- ECMA-defined final characters ----
/* ICH */		case '@':	InsertCharacters(OneIfZero(SumArgs())); break;
/* CUU */		case 'A':	CursorUp(OneIfZero(SumArgs()), false, false); break;
/* CUD */		case 'B':	CursorDown(OneIfZero(SumArgs()), false, false); break;
/* CUF */		case 'C':	CursorRight(OneIfZero(SumArgs()), false, false); break;
/* CUB */		case 'D':	CursorLeft(OneIfZero(SumArgs()), false, false); break;
/* CNL */		case 'E':	CarriageReturnNoUpdate(); CursorDown(OneIfZero(SumArgs()), true, false); break;
/* CPL */		case 'F':	CarriageReturnNoUpdate(); CursorUp(OneIfZero(SumArgs()), true, false); break;
/* CHA */		case 'G':	GotoX(OneIfZero(SumArgs())); break;
/* CUP */		case 'H':	GotoYX(); break;
/* CHT */		case 'I':	HorizontalTab(OneIfZero(SumArgs()), true); break;
/* ED */		case 'J':	EraseInDisplay(); break;
/* EL */		case 'K':	EraseInLine(); break;
/* IL */		case 'L':	InsertLines(OneIfZero(SumArgs())); break;
/* DL */		case 'M':	DeleteLines(OneIfZero(SumArgs())); break;
/* EF */		case 'N':	break; // Erase Field has no applicability as there are no fields.
/* EA */		case 'O':	break; // Erase Area has no applicability as there are no areas.
/* DCH */		case 'P':	DeleteCharacters(OneIfZero(SumArgs())); break;
/* SEE */		case 'Q':	break; // Set Editing Extent has no applicability as this is not a block mode terminal.
/* CPR */		case 'R':	break; // Cursor Position Report is meaningless if received rather than sent.
			case 'S':	ScrollUp(OneIfZero(SumArgs())); break;	/// FIXME \bug SU is a window pan, not a buffer scroll.
			case 'T':	ScrollDown(OneIfZero(SumArgs())); break;	/// FIXME \bug SD is a window pan, not a buffer scroll.
/* NP */		case 'U':	break; // Next Page has no applicability as there are no pages.
/* PP */		case 'V':	break; // Previous Page has no applicability as there are no pages.
/* CTC */		case 'W':	CursorTabulationControl(); break;
/* ECH */		case 'X':	EraseCharacters(OneIfZero(SumArgs())); break;
/* CVT */		case 'Y':	VerticalTab(OneIfZero(SumArgs()), true); break;
/* CBT */		case 'Z':	BackwardsHorizontalTab(OneIfZero(SumArgs()), true); break;
/* SRS */		case '[':	break; // No-one needs reversed strings from a virtual terminal.
/* PTX */		case '\\':	break; // No-one needs parallel texts from a virtual terminal.
/* SDS */		case ']':	break; // No-one needs directed strings from a virtual terminal.
/* SIMD */		case '^':	break; // No-one needs implicit movement direction from a virtual terminal.
/* HPA */		case '`':	GotoX(OneIfZero(SumArgs())); break;
/* HPR */		case 'a':	CursorRight(OneIfZero(SumArgs()), true, false); break;
/* REP */		case 'b':	break; // No-one needs repeat from a virtual terminal.
/* DA */		case 'c':	SendPrimaryDeviceAttributes(); break;
/* VPA */		case 'd':	GotoY(OneIfZero(SumArgs())); break;
/* VPR */		case 'e':	CursorDown(OneIfZero(SumArgs()), true, false); break;
/* HVP */		case 'f':	GotoYX(); break;
/* TBC */		case 'g':	TabClear(); break;
/* SM */		case 'h':	SetModes(true); break;
/* MC */		case 'i':	break; // Media Copy has no applicability as there are no auxiliary devices.
/* HPB */		case 'j':	CursorLeft(OneIfZero(SumArgs()), true, false); break;
/* VPB */		case 'k':	CursorUp(OneIfZero(SumArgs()), true, false); break;
/* RM */		case 'l':	SetModes(false); break;
/* SGR */		case 'm':	SetAttributes(); break;
/* DSR */		case 'n':	SendDeviceStatusReports(); break;
/* DAQ */		case 'o':	break; // Define Area Qualification has no applicability as this is not a block mode terminal.
// ---- ECMA private-use final characters begin here. ----
/* DECSTR */		case 'p':	break; // Soft Terminal Reset is not implemented.
/* DECLL */		case 'q':	break; // Load LEDs has no applicability as there are no LEDs.
/* DECSTBM */		case 'r':	SetTopBottomMargins(); break;
			case 's':	SCOSCorDESCSLRM(); break;
/* DECSLPP */		case 't':	SetLinesPerPageOrDTTerm(); break;
/* SCORC */		case 'u':	RestoreCursor(); RestoreAttributes(); RestoreModes(); break;	// Not DECSHST as on LA100.
/* DECSVST */		case 'v':	break; // Set multiple vertical tab stops at once is not implemented.
/* DECSHORP */		case 'w':	break; // Set Horizontal Pitch has no applicability as this is not a LA100.
/* SCOSGR */		case 'x':	SetSCOAttributes(); break;	// not DECREQTPARM
/* DECTST */		case 'y':	break; // Confidence Test has no applicability as this is not a hardware terminal.
/* DECSVERP */		case 'z':	break; // Set Vertical Pitch has no applicability as this is not a LA100.
/* DECTTC */		case '|':	break; // Transmit Termination Character is not implemented
/* DECPRO */		case '}':	break; // Define Protected Field is not implemented
			default:	
				std::clog << "Unknown CSI terminator " << character << "\n";
				break;
		} else
		if ('?' == first_private_parameter) switch (character) {
			case 'c':	SetLinuxCursorType(); break;
/* DECSM */		case 'h':	SetPrivateModes(true); break;
/* DECRM */		case 'l':	SetPrivateModes(false); break;
/* DECDSR */		case 'n':	SendPrivateDeviceStatusReports(); break;
/* DECCTC */		case 'W':	DECCursorTabulationControl(); break;
			default:	
				std::clog << "Unknown DEC Private CSI " << first_private_parameter << ' ' << character << "\n";
				break;
		} else
		if ('>' == first_private_parameter) switch (character) {
/* DECDA2 */		case 'c':	SendSecondaryDeviceAttributes(); break;
			default:	
				std::clog << "Unknown DEC Private CSI " << first_private_parameter << ' ' << character << "\n";
				break;
		} else
		if ('=' == first_private_parameter) switch (character) {
			case 'C':	SetSCOCursorType(); break;
			case 'S':	SetSCOCursorType(); break;
/* DECDA3 */		case 'c':	SendTertiaryDeviceAttributes(); break;
			default:	
				std::clog << "Unknown DEC Private CSI " << first_private_parameter << ' ' << character << "\n";
				break;
		} else
			std::clog << "Unknown DEC Private CSI " << first_private_parameter << ' ' << character << "\n";
	} else
	if ('$' == last_intermediate) switch (character) {
/* DECSCPP */	case '|':	SetColumnsPerPage(); break;
		default:	
			std::clog << "Unknown CSI " << last_intermediate << ' ' << character << "\n";
			break;
	} else
	if ('*' == last_intermediate) switch (character) {
/* DECSNLS */	case '|':	SetLinesPerPage(); break;
		default:	
			std::clog << "Unknown CSI " << last_intermediate << ' ' << character << "\n";
			break;
	} else
	if (' ' == last_intermediate) switch (character) {
/* DECSCUSR */	case 'q':	SetCursorStyle(); break;
/* SL */	case '@':	ScrollLeft(OneIfZero(SumArgs())); break;
/* SR */	case 'A':	ScrollRight(OneIfZero(SumArgs())); break;
/* GSM */	case 'B':	break;	// Graphic Size Modification has no meaning.
/* GSS */	case 'C':	break;	// Graphic Size Selection has no meaning.
/* FNT */	case 'D':	break;	// Font Selection has no meaning.
/* TSS */	case 'E':	break;	// Thin Space Selection has no meaning.
/* JFY */	case 'F':	break;	// Justify has no meaning.
/* SPI */	case 'G':	break;	// Spacing Increment has no meaning.
/* QUAD */	case 'H':	break;	// Quadding is not implemented.
		default:	
			std::clog << "Unknown CSI " << last_intermediate << ' ' << character << "\n";
			break;
	} else
	if ('!' == last_intermediate) switch (character) {
/* DECSTR */	case 'p':	SoftReset(); break;
		default:	
			std::clog << "Unknown CSI " << last_intermediate << ' ' << character << "\n";
			break;
	} else
	if ('\'' == last_intermediate) switch (character) {
/* DECEFR */	case 'w':	break; // Enable Filter Rectangle implies a complex multi-window model that is beyond the scope of this emulation.
/* DECSLE */	case '{':	SelectLocatorEvents(); break;
/* DECRQLP */	case '|':	RequestLocatorReport(); break;
/* DECELR */	case 'z':	EnableLocatorReports(); break;
		default:	
			std::clog << "Unknown CSI " << last_intermediate << ' ' << character << "\n";
			break;
	} else
		std::clog << "Unknown CSI " << last_intermediate << ' ' << character << "\n";
}

void 
SoftTerm::ControlString(
	uint_fast32_t /*character*/
) {
	std::clog << "Unexpected control string\n";
}

void 
SoftTerm::PrintableCharacter(
	bool error,
	unsigned short shift_level,
	uint_fast32_t character
) {
	/// FIXME: \bug
	/// This needs a lot more attention.
	/// Combining characters don't combine and we don't handle bidirectional printing.
	if (UnicodeCategorization::IsOtherFormat(character)
	||  UnicodeCategorization::IsMarkNonSpacing(character)
	) {
		// Do nothing.
	} else
	{
		const coordinate columns(display_origin.x + display_margin.w);
		const CharacterCell::attribute_type a(attributes ^ (error || 1U != shift_level ? CharacterCell::INVERSE : 0));

		if (advance_pending) {
			if (WillWrap()) Advance();
			advance_pending = false;
		}
		const ScreenBuffer::coordinate s(columns * active_cursor.y + active_cursor.x);

		if (UnicodeCategorization::IsMarkEnclosing(character)) {
			screen.WriteNCells(s, 1U, CharacterCell(character, a, foreground, background));
		} else
		{
			if (!overstrike) InsertCharacters(1U);
			screen.WriteNCells(s, 1U, CharacterCell(character, a, foreground, background));
			advance_pending = WillWrap();
			if (!advance_pending) Advance();
			if (!square && UnicodeCategorization::IsWideOrFull(character)) {
				const coordinate right_margin(scroll_origin.x + scroll_margin.w - 1U);

				if (active_cursor.x < right_margin) {
					if (advance_pending) {
						if (WillWrap()) Advance();
						advance_pending = false;
					}
					const ScreenBuffer::coordinate s1(columns * active_cursor.y + active_cursor.x);

					if (!overstrike) InsertCharacters(1U);
					screen.WriteNCells(s1, 1U, CharacterCell(SPC, a, foreground, background));
					advance_pending = WillWrap();
					if (!advance_pending) Advance();
				}
			}
		}
	}
}
