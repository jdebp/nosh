/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#define _XOPEN_SOURCE_EXTENDED
#include <map>
#include <set>
#include <stack>
#include <deque>
#include <vector>
#include <algorithm>
#include <memory>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <clocale>
#include <cerrno>
#include <stdint.h>
#include <sys/stat.h>
#include "kqueue_common.h"
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#if defined(__LINUX__) || defined(__linux__)
#include <endian.h>
#include "ttyutils.h"
#elif defined(__OpenBSD__)
#include <sys/uio.h>
#include <sys/endian.h>
#include "ttyutils.h"
#else
#include <sys/uio.h>
#include "ttyutils.h"
#endif
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "pack.h"
#include "popt.h"
#include "ProcessEnvironment.h"
#include "CharacterCell.h"
#include "InputMessage.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "UnicodeClassification.h"
#include "UTF8Decoder.h"
#include "ECMA48Decoder.h"
#include "ECMA48Output.h"
#include "TerminalCapabilities.h"
#include "TUIDisplayCompositor.h"
#include "VirtualTerminalBackEnd.h"
#include "SignalManagement.h"

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

}

/* Realizing a virtual terminal onto a set of physical devices **************
// **************************************************************************
*/

namespace {

inline
unsigned int
OneIfZero (
	const unsigned int v
) {
	return 0U == v ? 1U : v;
}

inline uint_fast8_t TranslateDECModifiers(unsigned int n) { return n - 1U; }
inline unsigned int TranslateDECCoordinates(unsigned int n) { return n - 1U; }

/// \brief Realize a TUIDisplayCompositor onto an ECMA48Output with the given capabilities and output stream.
class ECMA48OutputRealizer :
	public ECMA48Output
{
public:
	ECMA48OutputRealizer(const TerminalCapabilities & t, FILE * f, bool bold_as_colour, bool faint_as_colour, bool cursor_application_mode, bool calculator_application_mode, TUIDisplayCompositor & comp);
	~ECMA48OutputRealizer();

protected:
	TUIDisplayCompositor & c;
	const bool bold_as_colour;
	const bool faint_as_colour;
	unsigned short cursor_y, cursor_x;
	unsigned short screen_y, screen_x;
	CharacterCell::colour_type current_fg, current_bg;
	CharacterCell::attribute_type current_attr;
	CursorSprite::glyph_type cursor_glyph;
	CursorSprite::attribute_type cursor_attributes;

	void paint_backdrop ();
	void paint_changed_cells();

	static unsigned width (uint32_t ch);
	void print(uint32_t ch, const CharacterCell::colour_type & fg, const CharacterCell::colour_type & bg, const CharacterCell::attribute_type & attr);
	void print(const TUIDisplayCompositor::DirtiableCell & cell, bool inverted);
	bool is_cheap_to_print(unsigned cols) const;
	bool is_all_blank(unsigned cols) const;
	void GotoYX(unsigned short row, unsigned short col);
	void SGRFGColour(const CharacterCell::colour_type & colour) { if (colour != current_fg) SGRColour(true, current_fg = colour); }
	void SGRBGColour(const CharacterCell::colour_type & colour) { if (colour != current_bg) SGRColour(false, current_bg = colour); }
	void SGRAttr(const CharacterCell::attribute_type & attr);
	void SGRAttr1(const CharacterCell::attribute_type & attr, const CharacterCell::attribute_type & mask, char m, char & semi) const;
	void SGRAttr1(const CharacterCell::attribute_type & attr, const CharacterCell::attribute_type & mask, const CharacterCell::attribute_type & unit, char m, char & semi) const;

private:
	termios original_attr;
};

class Realizer :
	public TerminalCapabilities,
	public ECMA48OutputRealizer,
	public UTF8Decoder::UCS32CharacterSink,
	public ECMA48Decoder::ECMA48ControlSequenceSink
{
public:
	Realizer(const ProcessEnvironment & envs, unsigned int quadrant, bool wrong_way_up, bool bold_as_colour, bool faint_as_colour, bool cursor_application_mode, bool calculator_application_mode, VirtualTerminalBackEnd & vt, TUIDisplayCompositor & c);
	~Realizer();

	void set_display_update_needed() { update_needed = true; }
	bool exit_signalled() const { return terminate_signalled||interrupt_signalled||hangup_signalled; }
	void handle_non_kevents ();
	void handle_signal (int);
	void handle_stdin (int);

protected:
	unsigned int quadrant;	///< 0, 1, 2, or 3
	const bool wrong_way_up;	///< causes coordinate transforms between c and vt
	const bool has_pointer;
	bool update_needed;
	sig_atomic_t window_resized, terminate_signalled, interrupt_signalled, hangup_signalled, usr1_signalled, usr2_signalled;
	VirtualTerminalBackEnd & vt;
	UTF8Decoder utf8_decoder;
	ECMA48Decoder ecma48_decoder;

	void position ();
	void compose ();

	virtual void ProcessDecodedUTF8(uint32_t character, bool decoder_error, bool overlong);
	virtual void PrintableCharacter(bool, unsigned short, uint_fast32_t);
	virtual void ControlCharacter(uint_fast32_t);
	virtual void EscapeSequence(uint_fast32_t);
	virtual void ControlSequence(uint_fast32_t);
	virtual void ControlString(uint_fast32_t);
	void WriteExtendedKeyFromControlSequenceArgs(uint_fast16_t k, uint_fast8_t default_modifier = 0U);
	void WriteKeyFromDECFNKArgs(uint_fast8_t default_modifier);
	void WriteMouseFromXTermSGR(bool);

	void set_pointer_x(unsigned int, uint8_t);
	void set_pointer_y(unsigned int, uint8_t);

private:
	char stdout_buffer[64U * 1024U];
	char stdin_buffer[1U * 1024U];
};

static const CharacterCell::colour_type overscan_fg(ALPHA_FOR_ERASED,255,255,255), overscan_bg(ALPHA_FOR_ERASED,0,0,0);
static const CharacterCell overscan_blank(' ', 0U, overscan_fg, overscan_bg);

}

inline
bool
ECMA48OutputRealizer::is_cheap_to_print(
	unsigned cols
) const {
	for (unsigned i(0U); i < cols; ++i) {
		const TUIDisplayCompositor::DirtiableCell & cell(c.cur_at(cursor_y, cursor_x + i));
		if (cell.attributes != current_attr
		||  cell.foreground != current_fg
		||  cell.background != current_bg
		||  cell.character >= 0x00010000
		||  c.is_marked(false, cursor_y, cursor_x + i)
		||  c.is_pointer(cursor_y, cursor_x + i)
		)
			return false;
	}
	return true;
}

inline
bool
ECMA48OutputRealizer::is_all_blank(
	unsigned cols
) const {
	for (unsigned i(0U); i < cols; ++i) {
		const TUIDisplayCompositor::DirtiableCell & cell(c.cur_at(cursor_y, cursor_x + i));
		if (cell.character != 0x00000020)
			return false;
	}
	return true;
}

inline
void
ECMA48OutputRealizer::GotoYX(
	const unsigned short row,
	const unsigned short col
) {
	if (row == cursor_y && col == cursor_x) 
		return;
	if (0 == col && 0 == row) {
		CUP();
		cursor_y = cursor_x = 0;
		return;
	}
	if (0 == col ? col != cursor_x :
	    1 == col ? 2 < cursor_x :
	    2 == col ? 5 < cursor_x :
	    false
	) {
		if (row > cursor_y) {
			newline();
			if (row > cursor_y + 1U)
				print_control_characters(LF, row - cursor_y - 1U);
			cursor_y = row;
		} else
			print_control_character(CR);
		cursor_x = 0;
	}
	if (col == cursor_x) {
		if (row < cursor_y) {
			if (caps.use_RI && (row + 2U >= cursor_y))
				print_control_characters(RI, cursor_y - row);
			else
				CUU(cursor_y - row);
			cursor_y = row;
		} else
		if (row > cursor_y) {
			if (caps.use_IND && row <= cursor_y + 2U)
				print_control_characters(IND, row - cursor_y);
			else
				CUD(row - cursor_y);
			cursor_y = row;
		}
	} else
	if (row == cursor_y && cursor_x < c.query_w()) {
		if (col < cursor_x) {
			if (col + 4U >= cursor_x)
				print_control_characters(BS, cursor_x - col);
			else
				CUL(cursor_x - col);
			cursor_x = col;
		} else
		if (col > cursor_x) {
			if (col <= cursor_x + 2U || !is_cheap_to_print(col - cursor_x))
				CUR(col - cursor_x);
			else
				for (unsigned i(cursor_x); i < col; ++i) {
					TUIDisplayCompositor::DirtiableCell & cell(c.cur_at(cursor_y, i));
					print(cell, false /* We checked for no pointer or mark. */);
					cell.untouch();
				}
			cursor_x = col;
		}
	} else
	{
		CUP(row + 1U, col + 1U);
		cursor_y = row;
		cursor_x = col;
	}
}

inline
void
ECMA48OutputRealizer::SGRAttr1(
	const CharacterCell::attribute_type & attr,
	const CharacterCell::attribute_type & mask,
	char m,
	char & semi
) const {
	if ((attr & mask) != (current_attr & mask)) {
		if (semi) std::putc(semi, stdout);
		if (!(attr & mask)) std::putc('2', stdout);
		std::putc(m, stdout);
		semi = ';';
	}
}

inline
void
ECMA48OutputRealizer::SGRAttr1(
	const CharacterCell::attribute_type & attr,
	const CharacterCell::attribute_type & mask,
	const CharacterCell::attribute_type & unit,
	char m,
	char & semi
) const {
	const CharacterCell::attribute_type bits(attr & mask);
	if (bits != (current_attr & mask)) {
		if (semi) std::putc(semi, stdout);
		if (!bits) std::putc('2', stdout);
		std::putc(m, stdout);
		if (bits && (bits != unit)) {
			std::putc(':', stdout);
			std::fprintf(stdout, ":%u", bits / unit);
		}
		semi = ';';
	}
}

inline
void
ECMA48OutputRealizer::SGRAttr (
	const CharacterCell::attribute_type & attr
) {
	if (attr == current_attr) return;
	std::fputs("\x1b[", stdout);
	char semi(0);
	if (!caps.has_reverse_off && (current_attr & CharacterCell::INVERSE)) {
		if (semi) std::putc(semi, out);
		std::fputs("0", out);
		semi = ';';
		current_attr = 0;
	}
	enum { BF = CharacterCell::BOLD|CharacterCell::FAINT };
	if ((attr & BF) != (current_attr & BF)) {
		if (current_attr & BF) {
			if (semi) std::putc(semi, stdout);
			std::fputs("22", stdout);
			semi = ';';
		}
		if (CharacterCell::BOLD & attr) {
			if (semi) std::putc(semi, stdout);
			std::putc('1', stdout);
			semi = ';';
		}
		if (CharacterCell::FAINT & attr) {
			if (semi) std::putc(semi, stdout);
			std::putc('2', stdout);
			semi = ';';
		}
	}
	SGRAttr1(attr, CharacterCell::ITALIC, '3', semi);
	SGRAttr1(attr, CharacterCell::UNDERLINES, CharacterCell::SIMPLE_UNDERLINE, '4', semi);
	SGRAttr1(attr, CharacterCell::BLINK, '5', semi);
	SGRAttr1(attr, CharacterCell::INVERSE, '7', semi);
	SGRAttr1(attr, CharacterCell::INVISIBLE, '8', semi);
	SGRAttr1(attr, CharacterCell::STRIKETHROUGH, '9', semi);
	std::putc('m', stdout);
	current_attr = attr;
}

// This has to match the way that most realizing terminals will advance the cursor.
inline
unsigned
ECMA48OutputRealizer::width (
	uint32_t ch
) {

	if ((0x00000000 == ch)
	||  (0x00000080 <= ch && ch <= 0x0000009F)
	||  (0x0000200B == ch)
	||  (0x00001160 <= ch && ch <= 0x000011FF)
	||  UnicodeCategorization::IsMarkEnclosing(ch)
	||  UnicodeCategorization::IsMarkNonSpacing(ch)
	||  UnicodeCategorization::IsOtherFormat(ch)
	)
		return 0U;
	if (0x000000AD == ch) return 1U;
	if (UnicodeCategorization::IsWideOrFull(ch))
		return 2U;
	return 1U;
}
inline
void
ECMA48OutputRealizer::print(
	uint32_t ch,
	const CharacterCell::colour_type & fg,
	const CharacterCell::colour_type & bg,
	const CharacterCell::attribute_type & attr
) {
	SGRAttr(attr);
	SGRFGColour(fg);
	SGRBGColour(bg);
	if (ch < 0x00000020 || (0x00000080 <= ch && ch < 0x000000A0))
		ch = 0x00000020;
	UTF8(ch);
	for (unsigned w(width(ch)); w > 0U; --w) {
		++cursor_x;
		if (!caps.pending_wrap && cursor_x >= c.query_w()) {
			cursor_x = 0;
			if (cursor_y < c.query_h())
				++cursor_y;
		}
	}
}

void
ECMA48OutputRealizer::print(
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
	if (!caps.has_invisible && (CharacterCell::INVISIBLE & font_attributes)) {
		// Being invisible is always the same glyph.
		print(0x20, fg, bg, font_attributes & ~CharacterCell::INVISIBLE);
	} else {
		print(cell.character, fg, bg, font_attributes);
	}
}

ECMA48OutputRealizer::ECMA48OutputRealizer(
	const TerminalCapabilities & t,
	FILE * f,
	bool bc,
	bool fc,
	bool cursor_application_mode,
	bool calculator_application_mode,
	TUIDisplayCompositor & comp
) :
	ECMA48Output(t, f),
	c(comp),
	bold_as_colour(bc),
	faint_as_colour(fc),
	cursor_y(0),
	cursor_x(0),
	screen_y(0),
	screen_x(0),
	current_fg(-1U, 0U, 0U, 0U),	// Set an impossible colour, forcing a change.
	current_bg(-1U, 0U, 0U, 0U),	// Set an impossible colour, forcing a change.
	current_attr(-1U),	// Assume all attributes are on and need to be turned off.
	cursor_glyph(CursorSprite::BOX),
	cursor_attributes(CursorSprite::VISIBLE)
{
	std::fflush(out);
	if (0 <= tcgetattr_nointr(fileno(out), original_attr))
		tcsetattr_nointr(fileno(out), TCSADRAIN, make_raw(original_attr));
	if (caps.use_DECPrivateMode) {
		XTermAlternateScreenBuffer(true);
		XTermSaveRestore(true);
	}
	CUP();
	SGRAttr(0U);
	SGRFGColour(overscan_fg);
	SGRBGColour(overscan_bg);
	if (caps.use_DECPrivateMode) {
		DECAWM(false);
		DECECM(true);
		DECBKM(true);			// We want to be able to distinguish it from Control+Backspace.
		XTermAnyMouseEvents(caps.has_XTermSGRMouse);
		XTermSGRMouseReports(caps.has_XTermSGRMouse);
		XTermDeleteIsDEL(false);	// We want to be able to distinguish it from Control+Backspace.
		DECCKM(cursor_application_mode);
		DECNKM(calculator_application_mode);
	}
	if (caps.use_DECLocator) {
		DECELR(true);
		DECSLE(true, true);
		DECSLE(false, true);
	}
	change_cursor_visibility(false);
	SCUSR(cursor_attributes, cursor_glyph);
	/// \todo TODO DECPM 1002 and 1006 for mouse
	std::fflush(out);
}

ECMA48OutputRealizer::~ECMA48OutputRealizer()
{
	/// \todo TODO DECPM 1002 and 1006 for mouse
	SCUSR();
	change_cursor_visibility(true);
	if (caps.use_DECLocator) {
		DECELR(false);
		DECSLE(true, false);
		DECSLE(false, false);
	}
	if (caps.use_DECPrivateMode) {
		XTermAnyMouseEvents(false);
		XTermSGRMouseReports(false);
		DECBKM(false);			// Restore the more common Unix convention.
		DECECM(false);
		DECAWM(true);
		DECNKM(false);
		DECCKM(false);
	}
	SGRBGColour(overscan_bg);
	SGRFGColour(overscan_fg);
	SGRAttr(0U);
	GotoYX(0U, 0U);
	if (caps.use_DECPrivateMode) {
		XTermSaveRestore(false);
		XTermAlternateScreenBuffer(false);
	}
	std::fflush(out);
	tcsetattr_nointr(fileno(out), TCSADRAIN, original_attr);
}

inline
void
ECMA48OutputRealizer::paint_changed_cells()
{
	const CursorSprite::attribute_type a(c.query_cursor_attributes());
	if (CursorSprite::VISIBLE & a)
		change_cursor_visibility(false);
	for (unsigned row(0); row < c.query_h(); ++row) {
		for (unsigned col(0); col < c.query_w(); ++col) {
			TUIDisplayCompositor::DirtiableCell & cell(c.cur_at(row, col));
			if (!cell.touched()) continue;
			GotoYX(row, col);
			if (0x20 == cell.character) {
				const unsigned n(c.query_w() - col);
				if (3U < n && is_cheap_to_print(n) && is_all_blank(n)) {
					EL(0U);
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
		}
	}
	GotoYX(c.query_cursor_row(), c.query_cursor_col());
	const CursorSprite::glyph_type g(c.query_cursor_glyph());
	if (a != cursor_attributes || g != cursor_glyph) {
		cursor_attributes = a;
		cursor_glyph = g;
		SCUSR(cursor_attributes, cursor_glyph);
	}
	if (CursorSprite::VISIBLE & a)
		change_cursor_visibility(true);
	std::fflush(out);
}

inline
void
ECMA48OutputRealizer::paint_backdrop () 
{
	for (unsigned short row(0U); row < c.query_h(); ++row)
		for (unsigned short col(0U); col < c.query_w(); ++col)
			c.poke(row, col, overscan_blank);
}

Realizer::Realizer(
	const ProcessEnvironment & envs,
	unsigned int q,
	bool wwu,
	bool bc,
	bool fc,
	bool cursor_application_mode,
	bool calculator_application_mode,
	VirtualTerminalBackEnd & t,
	TUIDisplayCompositor & comp
) : 
	TerminalCapabilities(envs),
	ECMA48OutputRealizer(*this, stdout, bc, fc, cursor_application_mode, calculator_application_mode, comp),
	quadrant(q),
	wrong_way_up(wwu),
	has_pointer(has_XTermSGRMouse),
	update_needed(true),
	window_resized(true),
	terminate_signalled(false),
	interrupt_signalled(false),
	hangup_signalled(false),
	usr1_signalled(false),
	usr2_signalled(false),
	vt(t),
	utf8_decoder(*this),
	ecma48_decoder(*this, false /* no control strings */, interix_function_keys, rxvt_function_keys)
{
	std::setvbuf(stdout, stdout_buffer, _IOFBF, sizeof stdout_buffer);
}

Realizer::~Realizer()
{
	std::fflush(stdout);
	std::setvbuf(stdout, 0, _IOFBF, 1024);
}

/// \brief Clip and position the visible portion of the terminal's display buffer.
inline
void
Realizer::position (
) {
	// Glue the terminal window to the edges of the display screen buffer.
	screen_y = !(quadrant & 0x02) && vt.query_h() < c.query_h() ? c.query_h() - vt.query_h() : 0;
	screen_x = (1U == quadrant || 2U == quadrant) && vt.query_w() < c.query_w() ? c.query_w() - vt.query_w() : 0;
	// Tell the VirtualTerminal the size of the visible window; it will position it itself according to cursor placement.
	vt.set_visible_area(c.query_h() - screen_y, c.query_w() - screen_x);
}

/// \brief Render the terminal's display buffer onto the display's screen buffer.
inline
void
Realizer::compose () 
{
	for (unsigned short row(0U); row < vt.query_visible_h(); ++row) {
		const unsigned short source_row(vt.query_visible_y() + row);
		const unsigned short dest_row(screen_y + (wrong_way_up ? vt.query_visible_h() - row - 1U : row));
		for (unsigned short col(0U); col < vt.query_visible_w(); ++col)
			c.poke(dest_row, col + screen_x, vt.at(source_row, vt.query_visible_x() + col));
	}
}

inline
void
Realizer::handle_non_kevents (
) {
	if (window_resized) {
		window_resized = false;
		struct winsize size;
		if (tcgetwinsz_nointr(fileno(out), size) == 0)
			c.resize(size.ws_row, size.ws_col);
		update_needed = true;
	}
	if (update_needed) {
		update_needed = false;
		paint_backdrop();
		position();
		compose();
		const CursorSprite::attribute_type a(vt.query_cursor_attributes());
		// If the cursor is invisible, we are not guaranteed that the VirtualTerminal has kept the visible area around it.
		if (CursorSprite::VISIBLE & a) {
			const unsigned short cursor_y(screen_y + (wrong_way_up ? vt.query_visible_h() - vt.query_cursor_y() - 1U : vt.query_cursor_y()) - vt.query_visible_y());
			c.move_cursor(false, cursor_y, vt.query_cursor_x() - vt.query_visible_x() + screen_x);
		}
		c.set_cursor_state(false, a, vt.query_cursor_glyph());
		if (has_pointer)
			c.set_pointer_attributes(vt.query_pointer_attributes());
		c.repaint_new_to_cur();
		paint_changed_cells();
	}
}

void 
Realizer::ProcessDecodedUTF8(
	uint32_t character,
	bool decoder_error,
	bool overlong
) {
	ecma48_decoder.Process(character, decoder_error, overlong);
}

void
Realizer::WriteExtendedKeyFromControlSequenceArgs(
	uint_fast16_t k,
	uint_fast8_t default_modifier
) {
	const uint_fast8_t m(TranslateDECModifiers(OneIfZero(argc > 1U ? args[1U] : default_modifier + 1U)));
	for (unsigned n(OneIfZero(argc > 0U ? args[0U] : 0U)); n > 0U; --n)
		vt.WriteInputMessage(MessageForExtendedKey(k, m));
}

void
Realizer::WriteKeyFromDECFNKArgs(
	uint_fast8_t default_modifier
) {
	if (argc < 1U) return;
	const unsigned k(OneIfZero(args[0U]));
	const uint_fast8_t m(TranslateDECModifiers(OneIfZero(argc > 1U ? args[1U] : default_modifier + 1U)));
	switch (k) {
		case 1U:	vt.WriteInputMessage(MessageForExtendedKey(linux_editing_keypad ? EXTENDED_KEY_PAD_HOME : EXTENDED_KEY_FIND, m)); break;
		case 2U:	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_INSERT,m)); break;
		case 3U:	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_DELETE,m)); break;
		case 4U:	vt.WriteInputMessage(MessageForExtendedKey(linux_editing_keypad ? EXTENDED_KEY_PAD_HOME : EXTENDED_KEY_SELECT, m)); break;
		case 5U:	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAGE_UP,m)); break;
		case 6U:	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAGE_DOWN,m)); break;
		case 7U:	vt.WriteInputMessage(MessageForExtendedKey(linux_editing_keypad ? EXTENDED_KEY_FIND : EXTENDED_KEY_HOME, m)); break;
		case 8U:	vt.WriteInputMessage(MessageForExtendedKey(linux_editing_keypad ? EXTENDED_KEY_SELECT : EXTENDED_KEY_END, m)); break;
		case 11U:	vt.WriteInputMessage(MessageForFunctionKey(1U,m)); break;
		case 12U:	vt.WriteInputMessage(MessageForFunctionKey(2U,m)); break;
		case 13U:	vt.WriteInputMessage(MessageForFunctionKey(3U,m)); break;
		case 14U:	vt.WriteInputMessage(MessageForFunctionKey(4U,m)); break;
		case 15U:	vt.WriteInputMessage(MessageForFunctionKey(5U,m)); break;
		case 17U:	vt.WriteInputMessage(MessageForFunctionKey(6U,m)); break;
		case 18U:	vt.WriteInputMessage(MessageForFunctionKey(7U,m)); break;
		case 19U:	vt.WriteInputMessage(MessageForFunctionKey(8U,m)); break;
		case 20U:	vt.WriteInputMessage(MessageForFunctionKey(9U,m)); break;
		case 21U:	vt.WriteInputMessage(MessageForFunctionKey(10U,m)); break;
		case 23U:	vt.WriteInputMessage(MessageForFunctionKey(11U,m)); break;
		case 24U:	vt.WriteInputMessage(MessageForFunctionKey(12U,m)); break;
		case 25U:	vt.WriteInputMessage(MessageForFunctionKey(13U,m)); break;
		case 26U:	vt.WriteInputMessage(MessageForFunctionKey(14U,m)); break;
		case 28U:	vt.WriteInputMessage(MessageForFunctionKey(15U,m)); break;
		case 29U:	vt.WriteInputMessage(MessageForFunctionKey(16U,m)); break;
		case 31U:	vt.WriteInputMessage(MessageForFunctionKey(17U,m)); break;
		case 32U:	vt.WriteInputMessage(MessageForFunctionKey(18U,m)); break;
		case 33U:	vt.WriteInputMessage(MessageForFunctionKey(19U,m)); break;
		case 34U:	vt.WriteInputMessage(MessageForFunctionKey(20U,m)); break;
		case 35U:	vt.WriteInputMessage(MessageForFunctionKey(21U,m)); break;
		case 36U:	vt.WriteInputMessage(MessageForFunctionKey(22U,m)); break;
	}
}

inline
void 
Realizer::set_pointer_x(
	unsigned int col,
	uint8_t modifiers
) {
	if (c.change_pointer_col(col)) {
		vt.WriteInputMessage(MessageForMouseColumn(col, modifiers));
		update_needed = true;
	}
}

inline
void 
Realizer::set_pointer_y(
	unsigned int row,
	uint8_t modifiers
) {
	if (c.change_pointer_row(row)) {
		if (wrong_way_up)
			row = c.query_h() - row - 1U;
		vt.WriteInputMessage(MessageForMouseRow(row, modifiers));
		update_needed = true;
	}
}

void
Realizer::WriteMouseFromXTermSGR(
	bool press
) {
	const unsigned flags(argc > 0U ? args[0U]: 0U);
	const unsigned col(TranslateDECCoordinates(OneIfZero(argc > 1U ? args[1U] : 0U)));
	const unsigned row(TranslateDECCoordinates(OneIfZero(argc > 2U ? args[2U] : 0U)));

	uint_fast8_t modifiers(0);
	if (flags & 4U)
		modifiers |= INPUT_MODIFIER_LEVEL2;
	if (flags & 8U)
		modifiers |= INPUT_MODIFIER_SUPER;
	if (flags & 16U)
		modifiers |= INPUT_MODIFIER_CONTROL;

	set_pointer_x(col,modifiers);
	set_pointer_y(row,modifiers);

	if (!(flags & 32U)) {
		const uint_fast16_t button = (flags & 3U);
		if (flags & 64U)
			// vim gets confused by wheel release events encoded this way, so the terminal should never send them.
			// However, we account for receiving them, just in case.
			vt.WriteInputMessage(MessageForMouseWheel(button,press ? 1U : 0U,modifiers));
		else
			vt.WriteInputMessage(MessageForMouseButton(button,press,modifiers));
	}
}

void
Realizer::PrintableCharacter(bool error, unsigned short shift_level, uint_fast32_t character)
{
	if (10U == shift_level) {
		// The Interix system has no F0 ('0') and omits 'l' for some reason.
		if ('0' <  character && character <= '9')
			vt.WriteInputMessage(MessageForFunctionKey(character - '0',0));
		else
		if ('A' <= character && character <= 'Z')
			vt.WriteInputMessage(MessageForFunctionKey(character - 'A' + 10U,0));
		else
		if ('a' <= character && character <= 'k')
			vt.WriteInputMessage(MessageForFunctionKey(character - 'a' + 36U,0));
		else
		if ('m' <= character && character <= 'z')
			vt.WriteInputMessage(MessageForFunctionKey(character - 'm' + 47U,0));
		else
			;
	} else
	if (3U == shift_level) {
		if (rxvt_function_keys) switch (character) {
			case 'a':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_UP,INPUT_MODIFIER_CONTROL)); goto skip_dec;
			case 'b':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_DOWN,INPUT_MODIFIER_CONTROL)); goto skip_dec;
			case 'c':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_RIGHT,INPUT_MODIFIER_CONTROL)); goto skip_dec;
			case 'd':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_LEFT,INPUT_MODIFIER_CONTROL)); goto skip_dec;
		}
		switch (character) {
			case 'j':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_ASTERISK,0)); break;
			case 'k':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_PLUS,0)); break;
			case 'l':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_COMMA,0)); break;
			case 'm':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_MINUS,0)); break;
			case 'n':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_DELETE,0)); break;
			case 'o':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_SLASH,0)); break;
			case 'p':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_INSERT,0)); break;
			case 'q':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_END,0)); break;
			case 'r':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_DOWN,0)); break;
			case 's':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_PAGE_DOWN,0)); break;
			case 't':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_LEFT,0)); break;
			case 'u':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_CENTRE,0)); break;
			case 'v':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_RIGHT,0)); break;
			case 'w':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_HOME,0)); break;
			case 'x':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_UP,0)); break;
			case 'y':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_PAGE_UP,0)); break;
			case 'M':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_ENTER,0)); break;
			case 'P':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_F1,0)); break;
			case 'Q':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_F2,0)); break;
			case 'R':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_F3,0)); break;
			case 'S':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_F4,0)); break;
			case 'T':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_F5,0)); break;
			case 'X':	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_PAD_EQUALS,0)); break;
		}
skip_dec:		;
	} else
	if (1U < shift_level) {
		// Do nothing.
	} else
	if (!error)
	{
		vt.WriteInputMessage(MessageForUCS3(character));
	}
}

void
Realizer::ControlCharacter(uint_fast32_t character)
{
	switch (character) {
		default:		vt.WriteInputMessage(MessageForUCS3(character)); break;
		// We have sent the right DECBKM and XTerm private mode sequences to ensure that these are the case.
		case 0x00000008:	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_BACKSPACE,0)); break;
		case 0x0000000A:	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_RETURN_OR_ENTER,INPUT_MODIFIER_CONTROL)); break;
		case 0x0000000D:	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_RETURN_OR_ENTER,0)); break;
		case 0x0000007F:	vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_BACKSPACE,INPUT_MODIFIER_CONTROL)); break;
	}
}

void
Realizer::EscapeSequence(uint_fast32_t /*character*/)
{
}

void
Realizer::ControlSequence(uint_fast32_t character)
{
	// Finish the final argument, using the relevant defaults.
	FinishArg(0U); 

	// Enact the action.
	if (NUL == last_intermediate) {
		if (NUL == first_private_parameter) {
			if (sco_function_keys) {
				static const char other[9] = "@[<]^_'{";
				if ('N' <  character && character <= 'Z') {
					vt.WriteInputMessage(MessageForFunctionKey(character - 'N', 0));
					goto skip_dec;
				} else
				if ('a' <= character && character <= 'z') {
					vt.WriteInputMessage(MessageForFunctionKey(character - 'a' + 15U, 0));
					goto skip_dec;
				} else
				if (const char * p = 0x20 < character && character < 0x80 ? std::strchr(other, static_cast<char>(character)) : 0) {
					vt.WriteInputMessage(MessageForFunctionKey(p - other + 41U, 0));
					goto skip_dec;
				} else
					;
			}
			if (rxvt_function_keys) switch (character) {
				case '$':	WriteKeyFromDECFNKArgs(INPUT_MODIFIER_LEVEL2); break;
				case '^':	WriteKeyFromDECFNKArgs(INPUT_MODIFIER_CONTROL); break;
				case '@':	WriteKeyFromDECFNKArgs(INPUT_MODIFIER_LEVEL2|INPUT_MODIFIER_CONTROL); break;
				case 'a':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_UP_ARROW, INPUT_MODIFIER_LEVEL2); break;
				case 'b':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_DOWN_ARROW, INPUT_MODIFIER_LEVEL2); break;
				case 'c':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_RIGHT_ARROW, INPUT_MODIFIER_LEVEL2); break;
				case 'd':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_LEFT_ARROW, INPUT_MODIFIER_LEVEL2); break;
			}
			switch (character) {
				case 'A':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_UP_ARROW); break;
				case 'B':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_DOWN_ARROW); break;
				case 'C':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_RIGHT_ARROW); break;
				case 'D':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_LEFT_ARROW); break;
				case 'E':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_CENTRE); break;
				case 'F':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_END); break;
				case 'G':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAGE_DOWN); break;
				case 'H':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_HOME); break;
				case 'I':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAGE_UP); break;
				case 'L':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_INSERT); break;
				case 'M':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAD_ENTER); break;
				case 'P':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAD_F1); break;
				case 'Q':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAD_F2); break;
				case 'R':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAD_F3); break;
				case 'S':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAD_F4); break;
				case 'T':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAD_F5); break;
				case 'Z':	WriteExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_BACKTAB); break;
				case '~':	WriteKeyFromDECFNKArgs(0U); break;
			}
skip_dec: 		;
		} else
		if ('<' == first_private_parameter) switch (character) {
			case 'M':	WriteMouseFromXTermSGR(true); break;
			case 'm':	WriteMouseFromXTermSGR(false); break;
		} else
			;
	} else
		;
}

void
Realizer::ControlString(uint_fast32_t /*character*/)
{
}

void
Realizer::handle_stdin (
	int n		///< number of characters available; can be 0 erroneously
) {
	do {
		int l(read(STDIN_FILENO, stdin_buffer, sizeof stdin_buffer));
		if (0 >= l) break;
		n -= l;
		for (int i(0); i < l; ++i)
			utf8_decoder.Process(stdin_buffer[i]);
	} while (n > 0);
	ecma48_decoder.AbortSequence();
}

void
Realizer::handle_signal (
	int signo
) {
	switch (signo) {
		case SIGWINCH:	window_resized = true; break;
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
		case SIGUSR1:	usr1_signalled = true; break;
		case SIGUSR2:	usr2_signalled = true; break;
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_termio_realizer [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	unsigned long quadrant(3U);
	bool wrong_way_up(false);
	bool display_only(false);
	bool bold_as_colour(false);
	bool faint_as_colour(false);
	bool cursor_application_mode(false);
	bool calculator_application_mode(false);

	try {
		popt::unsigned_number_definition quadrant_option('\0', "quadrant", "number", "Position the terminal in quadrant 0, 1, 2, or 3.", quadrant, 0);
		popt::bool_definition wrong_way_up_option('\0', "wrong-way-up", "Display from bottom to top.", wrong_way_up);
		popt::bool_definition display_only_option('\0', "display-only", "Only render the display; do not send input.", display_only);
		popt::bool_definition bold_as_colour_option('\0', "bold-as-colour", "Forcibly render boldface as a colour brightness change.", bold_as_colour);
		popt::bool_definition faint_as_colour_option('\0', "faint-as-colour", "Forcibly render faint as a colour brightness change.", faint_as_colour);
		popt::bool_definition permit_fake_truecolour_option('\0', "permit-fake-truecolour", "Permit using 24-bit colour when the terminal is known to fake this ability.", Realizer::permit_fake_truecolour);
		popt::bool_definition cursor_application_mode_option('\0', "cursor-keypad-application-mode", "Set the cursor keypad to application mode instead of normal mode.", cursor_application_mode);
		popt::bool_definition calculator_application_mode_option('\0', "calculator-keypad-application-mode", "Set the calculator keypad to application mode instead of normal mode.", calculator_application_mode);
		popt::definition * top_table[] = {
			&quadrant_option,
			&wrong_way_up_option,
			&display_only_option,
			&bold_as_colour_option,
			&faint_as_colour_option,
			&permit_fake_truecolour_option,
			&cursor_application_mode_option,
			&calculator_application_mode_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{virtual-terminal}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing virtual terminal directory name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * vt_dirname(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}
	std::vector<struct kevent> ip;

	FileDescriptorOwner vt_dir_fd(open_dir_at(AT_FDCWD, vt_dirname));
	if (0 > vt_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, vt_dirname, std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileDescriptorOwner buffer_fd(open_read_at(vt_dir_fd.get(), "display"));
	if (0 > buffer_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, vt_dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileStar buffer_file(fdopen(buffer_fd.get(), "r"));
	if (!buffer_file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, vt_dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	buffer_fd.release();
	FileDescriptorOwner input_fd(display_only ? -1 : open_writeexisting_at(vt_dir_fd.get(), "input"));
	if (!display_only && input_fd.get() < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, vt_dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	vt_dir_fd.release();

	if (!display_only)
		append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGWINCH, SIGUSR1, SIGUSR2, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, 0);
	append_event(ip, SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);

	VirtualTerminalBackEnd vt(vt_dirname, buffer_file.release(), input_fd.release());
	append_event(ip, vt.query_buffer_fd(), EVFILT_VNODE, EV_ADD|EV_ENABLE|EV_CLEAR, NOTE_WRITE, 0, 0);
	append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_ADD|EV_DISABLE, 0, 0, 0);
	TUIDisplayCompositor compositor(24, 80);
	Realizer realizer(envs, quadrant, wrong_way_up, bold_as_colour, faint_as_colour, cursor_application_mode, calculator_application_mode, vt, compositor);

	const struct timespec immediate_timeout = { 0, 0 };

	while (true) {
		if (realizer.exit_signalled())
			break;
		realizer.handle_non_kevents();

		struct kevent p[128];
		const int rc(kevent(queue.get(), ip.data(), ip.size(), p, sizeof p/sizeof *p, vt.query_reload_needed() ? &immediate_timeout : 0));
		ip.clear();

		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}

		if (0 == rc) {
			if (vt.query_reload_needed()) {
				vt.reload();
				realizer.set_display_update_needed();
			}
			continue;
		}

		for (std::size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_VNODE:
					if (vt.query_buffer_fd() == static_cast<int>(e.ident)) 
						vt.set_reload_needed();
					break;
				case EVFILT_SIGNAL:
					realizer.handle_signal(e.ident);
					break;
				case EVFILT_READ:
					if (STDIN_FILENO == e.ident)
						realizer.handle_stdin(e.data);
					break;
				case EVFILT_WRITE:
					if (vt.query_input_fd() == static_cast<int>(e.ident))
						vt.FlushMessages();
					break;
			}
		}

		if (vt.MessageAvailable()) {
			if (!vt.query_polling_for_write()) {
				append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_ENABLE, 0, 0, 0);
				vt.set_polling_for_write(true);
			}
		} else {
			if (vt.query_polling_for_write()) {
				append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_DISABLE, 0, 0, 0);
				vt.set_polling_for_write(false);
			}
		}
	}

	throw EXIT_SUCCESS;
}
