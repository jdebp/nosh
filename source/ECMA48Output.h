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
	ECMA48Output(const TerminalCapabilities & c, FILE * f, bool c1_7) : caps(c), c1_7bit(c1_7), c1_8bit(false), out(f) {}

	const TerminalCapabilities & caps;
	bool c1_7bit, c1_8bit;

	int fd() const { return fileno(out); }
	FILE * file() const { return out; }
	void flush() const { std::fflush(out); }
        void csi() const { print_control_character(CSI); }
	void newline() const;
	void UTF8(uint32_t ch) const;
	void SGRColour(bool is_fg, const CharacterCell::colour_type & colour) const;
	void SGRColour(bool is_fg) const;
	void SGRAttribute(unsigned n) const { csi(); std::fprintf(out, "%um", n); }
	void print_subparameter(unsigned n) const { std::fprintf(out, ":%u", n); }
	void print_graphic_character(unsigned char c) const { std::fputc(c, out); }
	void print_control_character(unsigned char c) const;
	void print_control_characters(unsigned char c, unsigned int n) const;
	void change_cursor_visibility(bool) const;

	void SCUSR(CursorSprite::attribute_type a, CursorSprite::glyph_type g) const;
	void SCUSR() const;
	void ED(unsigned n) const { csi(); std::fprintf(out, "%uJ", n); }
	void EL(unsigned n) const { csi(); std::fprintf(out, "%uK", n); }
	void HPA(unsigned n) const { csi(); std::fprintf(out, "%u`", n); }
	void CHA(unsigned n) const { csi(); std::fprintf(out, "%uG", n); }
	void CTC(unsigned n) const { csi(); std::fprintf(out, "%uW", n); }
	void TBC(unsigned n) const { csi(); std::fprintf(out, "%ug", n); }
	void CUP(unsigned r, unsigned c) const { csi(); std::fprintf(out, "%u;%uH", r, c); }
	void CUP() const { csi(); std::fputs("H", out); }
	void CUU(unsigned n) const { csi(); std::fprintf(out, "%uA", n); }
	void CUD(unsigned n) const { csi(); std::fprintf(out, "%uB", n); }
	void CUR(unsigned n) const { csi(); std::fprintf(out, "%uC", n); }
	void CUL(unsigned n) const { csi(); std::fprintf(out, "%uD", n); }
	void IRM(bool v) const { Mode(1U, v); }
	void DECSTR() const { csi(); std::fputs("!p", out); }
	void DECST8C() const { DECCursorTabulationControl(5U); }
	void DECCKM(bool v) const { DECPrivateMode(1U, v); }
	void DECCOLM(bool v) const { DECPrivateMode(3U, v); }
	void DECSCNM(bool v) const { DECPrivateMode(5U, v); }
	void DECAWM(bool v) const { DECPrivateMode(7U, v); }
	void DECTCEM(bool v) const { DECPrivateMode(25U, v); }
	void DECNKM(bool v) const { DECPrivateMode(66U, v); }
	void DECBKM(bool v) const { DECPrivateMode(67U, v); }
	void DECSLRMM(bool v) const { DECPrivateMode(69U, v); }
	void DECECM(bool v) const { DECPrivateMode(117U, v); }
	void DECSCPP(unsigned n) const { csi(); std::fprintf(out, "%u$|", n); }
	void DECSNLS(unsigned n) const { csi(); std::fprintf(out, "%u*|", n); }
	void DECELR(bool enable) const { csi(); std::fprintf(out, "%u\'z", enable ? 1U : 0U); }
	void DECSLE(bool press, bool enable) const { csi(); std::fprintf(out, "%u\'{", 1U + (press ? 0U : 2U) + (enable ? 0U : 1U)); }
	void DECSLE() const { csi(); std::fprintf(out, "%u\'{", 0U); }
	void DECSLPP(unsigned n) const { csi(); std::fprintf(out, "%ut", n); }
	void DTTermResize(unsigned n0, unsigned n1) const { csi(); std::fprintf(out, "8;%u;%ut", n0, n1); }
	void DECSTBM(unsigned n0, unsigned n1) const { csi(); std::fprintf(out, "%u;%ur", n0, n1); }
	void DECSLRM(unsigned n0, unsigned n1) const { csi(); std::fprintf(out, "%u;%us", n0, n1); }
	// The 1006 private mode is not separately tweakable because we *always* want 1006 encoding; it entirely supersedes the 1005 and 1015 encodings.other encodings are inferior and superseded.
	// The 1000, 1002, and 1003 private modes are radio buttons in a terminal emulator, but not all emulators implement all modes (MobaXTerm lacking 1003 support, for example).
	// So we push lower-functionality buttons before pushing the higher-functionality ones.
	void XTermSendClickMouseEvents() const { DECPrivateMode(1006U, true); DECPrivateMode(1000U, true); }
	void XTermSendClickDragMouseEvents() const { DECPrivateMode(1006U, true); DECPrivateMode(1000U, true); DECPrivateMode(1002U, true); }
	void XTermSendAnyMouseEvents() const { DECPrivateMode(1006U, true); DECPrivateMode(1000U, true); DECPrivateMode(1002U, true); DECPrivateMode(1003U, true); }
	void XTermSendNoMouseEvents() const { DECPrivateMode(1006U, false); DECPrivateMode(1003U, false); DECPrivateMode(1002U, false); DECPrivateMode(1000U, false); }
	void XTermAlternateScreenBuffer(bool v) const { DECPrivateMode(1047U, v); }
	void XTermSaveRestore(bool v) const { DECPrivateMode(1048U, v); }
	void XTermDeleteIsDEL(bool v) const { DECPrivateMode(1037U, v); }
	void SquareMode(bool v) const { DECPrivateMode(1369U, v); }
protected:
	FILE * const out;

	void Mode(unsigned n, bool v) const { csi(); std::fprintf(out, "%u%c", n, v ? 'h' : 'l'); }
	void DECPrivateMode(unsigned n, bool v) const { csi(); std::fprintf(out, "?%u%c", n, v ? 'h' : 'l'); }
	void DECCursorTabulationControl(unsigned n) const { csi(); std::fprintf(out, "?%uW", n); }
	void DECSCUSR(unsigned n) const { csi(); std::fprintf(out, "%u q", n); }
	void DECSCUSR() const { csi(); std::fputs(" q", out); }
	// LINUXSCUSR is documented in VGA-softcursor.txt.
	void LINUXSCUSR(unsigned n) const { csi(); std::fprintf(out, "?%uc", n); }
	void LINUXSCUSR() const { csi(); std::fputs("?c", out); }
	void SGRColour8(bool is_fg, unsigned n) const { csi(); std::fprintf(out, "%um", (is_fg ? 30U : 40U) + n); }
	void SGRColour16(bool is_fg, unsigned n) const { if (n >= 8U) n += 90U - 38U; csi(); std::fprintf(out, "%um", (is_fg ? 30U : 40U) + n); }
	void SGRColour256Ambig(bool is_fg, unsigned n) const { csi(); std::fprintf(out, "%u;5;%um", (is_fg ? 38U : 48U), n); }
	void SGRColour256(bool is_fg, unsigned n) const { csi(); std::fprintf(out, "%u:5:%um", (is_fg ? 38U : 48U), n); }
	void SGRTrueColourAmbig(bool is_fg, unsigned r, unsigned g, unsigned b) const { csi(); std::fprintf(out, "%u;2;%u;%u;%um", (is_fg ? 38U : 48U), r, g, b); }
	void SGRTrueColour(bool is_fg, unsigned r, unsigned g, unsigned b) const { csi(); std::fprintf(out, "%u:2:%u:%u:%um", (is_fg ? 38U : 48U), r, g, b); }
};

#endif
