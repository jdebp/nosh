/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_ECMA48OUTPUT_H)
#define INCLUDE_ECMA48OUTPUT_H

#include <cstdio>
#include "CharacterCell.h"
#include "ControlCharacters.h"

class TerminalCapabilities;

/// \brief Encapsulate ECMA-48 output control sequences for a terminal with the given capabilities.
class ECMA48Output
{
public:
	ECMA48Output(const TerminalCapabilities & c, FILE * f) : caps(c), out(f) {}
protected:
	const TerminalCapabilities & caps;
	FILE * const out;

	void DECPrivateMode(unsigned n, bool v) const { print_control_character(CSI); std::fprintf(out, "?%u%c", n, v ? 'h' : 'l'); }
	void DECCKM(bool v) const { DECPrivateMode(7U, v); }
	void DECAWM(bool v) const { DECPrivateMode(7U, v); }
	void DECTCEM(bool v) const { DECPrivateMode(25U, v); }
	void DECNKM(bool v) const { DECPrivateMode(66U, v); }
	void DECBKM(bool v) const { DECPrivateMode(67U, v); }
	void DECECM(bool v) const { DECPrivateMode(117U, v); }
	void XTermAlternateScreenBuffer(bool v) const { DECPrivateMode(1047U, v); }
	void XTermSaveRestore(bool v) const { DECPrivateMode(1048U, v); }
	void XTermAnyMouseEvents(bool v) const { DECPrivateMode(1003U, v); }
	void XTermSGRMouseReports(bool v) const { DECPrivateMode(1006U, v); }
	void XTermDeleteIsDEL(bool v) const { DECPrivateMode(1037U, v); }
	void DECELR(bool v) const { print_control_character(CSI); std::fprintf(out, "%u\'z", v ? 1U : 0U); }
	void DECSLE(bool press, bool enable) const { print_control_character(CSI); std::fprintf(out, "%u\'{", (press ? 0U : 2U) + (enable ? 0U : 1U)); }
	void DECSCUSR(unsigned n) const { print_control_character(CSI); std::fprintf(out, "%u q", n); }
	void DECSCUSR() const { print_control_character(CSI); std::fputs(" q", out); }
	// LINUXSCUSR is documented in VGA-softcursor.txt.
	void LINUXSCUSR(unsigned n) const { print_control_character(CSI); std::fprintf(out, "?%uc", n); }
	void LINUXSCUSR() const { print_control_character(CSI); std::fputs("?c", out); }
	void CUU(unsigned n) const { print_control_character(CSI); std::fprintf(out, "%uA", n); }
	void CUD(unsigned n) const { print_control_character(CSI); std::fprintf(out, "%uB", n); }
	void CUR(unsigned n) const { print_control_character(CSI); std::fprintf(out, "%uC", n); }
	void CUL(unsigned n) const { print_control_character(CSI); std::fprintf(out, "%uD", n); }
	void CUP(unsigned r, unsigned c) const { print_control_character(CSI); std::fprintf(out, "%u;%uH", r, c); }
	void CUP() const { print_control_character(CSI); std::fputs("H", out); }
	void EL(unsigned n) const { print_control_character(CSI); std::fprintf(out, "%uK", n); }
	void SGRColour8(bool is_fg, unsigned n) const { print_control_character(CSI); std::fprintf(out, "%um", (is_fg ? 30U : 40U) + n); }
	void SGRColour16(bool is_fg, unsigned n) const { if (n >= 8U) n += 90U - 30U; print_control_character(CSI); std::fprintf(out, "%um", (is_fg ? 30U : 40U) + n); }
	void SGRColour256Ambig(bool is_fg, unsigned n) const { print_control_character(CSI); std::fprintf(out, "%u;5;%um", (is_fg ? 38U : 48U), n); }
	void SGRColour256(bool is_fg, unsigned n) const { print_control_character(CSI); std::fprintf(out, "%u:5:%um", (is_fg ? 38U : 48U), n); }
	void SGRTrueColourAmbig(bool is_fg, unsigned r, unsigned g, unsigned b) const { print_control_character(CSI); std::fprintf(out, "%u;2;%u;%u;%um", (is_fg ? 38U : 48U), r, b, g); }
	void SGRTrueColour(bool is_fg, unsigned r, unsigned g, unsigned b) const { print_control_character(CSI); std::fprintf(out, "%u:2:%u;%u;%um", (is_fg ? 38U : 48U), r, b, g); }
	void SGRColour(bool is_fg, const CharacterCell::colour_type & colour) const;

	void newline() const;
	void print_control_character(unsigned char c) const;
	void print_control_characters(unsigned char c, unsigned int n) const;
	void UTF8(uint32_t ch) const;
	void SCUSR(CursorSprite::attribute_type a, CursorSprite::glyph_type g) const;
	void SCUSR() const;
	void change_cursor_visibility(bool) const;
};

#endif
