/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_ECMA48OUTPUT_H)
#define INCLUDE_ECMA48OUTPUT_H

#include <vector>
#include <iosfwd>
#include "CharacterCell.h"
#include "ControlCharacters.h"

class TerminalCapabilities;
struct ProcessEnvironment;

/// \brief Encapsulate ECMA-48 output control sequences for a terminal with the given capabilities.
class ECMA48Output
{
public:
	ECMA48Output(const TerminalCapabilities & c, std::ostream & o, bool c1_7, bool c1_8) : caps(c), c1_7bit(c1_7), c1_8bit(c1_8), out(o) {}

	const TerminalCapabilities & caps;
	bool c1_7bit, c1_8bit;

	static bool query_use_colours(const ProcessEnvironment &, int);	///< whether we should do terminal colour changes on this file descriptor
	std::ostream & stream() const { return out; }
	void hard_reset() const { escape_sequence('c'); }
	void newline() const;
	void UTF8(uint32_t ch) const;
	void SGRColour(bool is_fg, const CharacterCell::colour_type & colour) const;
	void SGRColour(bool is_fg) const;
	void SGRAttribute(unsigned n) const { control_sequence('m', n); }
	void SGRAttribute(const std::vector<unsigned> & n) const { control_sequence('m', n); }
	void SGRAttribute(unsigned n, const std::vector<unsigned> & u) const { control_sequence('m', n, u); }
	void set_boldface (bool v) const { SGRAttribute(v ? 1U : 22U); }
	void set_italics (bool v) const { SGRAttribute(v ? 3U : 23U); }
	void set_underline (bool v) const { SGRAttribute(v ? 4U : 24U); }
	void change_cursor_visibility(bool) const;
	void reverse_index(unsigned n) const { control_characters(RI, n); }
	void forward_index(unsigned n) const { control_characters(IND, n); }
	void linefeed(unsigned n) const { control_characters(LF, n); }
	void tab() const { control_character(TAB); }
	void backspace() const { control_character(BS); }
	void backspace(unsigned n) const { control_characters(BS, n); }
	void space(unsigned n) const { control_characters(SPC, n); }
	void carriage_return() const { control_character(CR); }
	void set_horizontal_tabstop_here() const;
	void clear_horizontal_tabstop_here() const;
	void horizontal_position(unsigned n) const;

	void SCUSR(CursorSprite::attribute_type a, CursorSprite::glyph_type g) const;
	void SCUSR() const;
	void ED(unsigned n) const { control_sequence('J', n); }
	void EL(unsigned n) const { control_sequence('K', n); }
	void HPA(unsigned n) const { control_sequence('`', n); }
	void CHA(unsigned n) const { control_sequence('G', n); }
	void CTC(unsigned n) const { control_sequence('W', n); }
	void TBC(unsigned n) const { control_sequence('g', n); }
	void CUP(unsigned r, unsigned c) const { control_sequence('H', r, c); }
	void CUP() const { control_sequence('H'); }
	void CUU(unsigned n) const { control_sequence('A', n); }
	void CUD(unsigned n) const { control_sequence('B', n); }
	void CUR(unsigned n) const { control_sequence('C', n); }
	void CUL(unsigned n) const { control_sequence('D', n); }
	void REP(unsigned n) const { control_sequence('b', n); }
	void IRM(bool v) const { Mode(1U, v); }
	void DECSTR() const { private_control_sequence('!', 'p'); }
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
	void DECSCPP(unsigned n) const { control_sequence('$', '|', n); }
	void DECSNLS(unsigned n) const { control_sequence('*', '|', n); }
	void DECELR(bool enable) const { control_sequence('\'', 'z', enable ? 1U : 0U); }
	void DECSLE(bool press, bool enable) const { control_sequence('\'', '{', 1U + (press ? 0U : 2U) + (enable ? 0U : 1U)); }
	void DECSLE() const { control_sequence('\'', '{', 0U); }
	void DECSLPP(unsigned n) const { control_sequence('t', n); }
	void DTTermResize(unsigned n0, unsigned n1) const { control_sequence('t', 8U, n0, n1); }
	void DECSTBM(unsigned n0, unsigned n1) const { control_sequence('r', n0, n1); }
	void DECSLRM(unsigned n0, unsigned n1) const { control_sequence('s', n0, n1); }
	void DECKPxM(bool application) const { escape_sequence(application ? '=' : '>'); }
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
	void TeraTermEscapeIsFS(bool v) const { DECPrivateMode(7727U, v); }
	void SquareMode(bool v) const { SCOPrivateMode(1U, v); }
	void DECFunctionKeys(bool v) const { SCOPrivateMode(2U, v); }
	void SCOFunctionKeys(bool v) const { SCOPrivateMode(3U, v); }
	void TekenFunctionKeys(bool v) const { SCOPrivateMode(4U, v); }
protected:
	std::ostream & out;

	void control_character(unsigned char c) const;
	void control_characters(unsigned char c, unsigned int n) const;
	void escape_sequence(char) const;
	void control_sequence(char) const;
	void control_sequence(char, char) const;
	void control_sequence(char, unsigned) const;
	void control_sequence(char, unsigned, unsigned) const;
	void control_sequence(char, unsigned, unsigned, unsigned) const;
	void control_sequence(char, unsigned, unsigned, unsigned, unsigned, unsigned) const;
	void control_sequence(char, const std::vector<unsigned> &) const;
	void control_sequence(char, unsigned, const std::vector<unsigned> &) const;
	void control_sequence(char, char, unsigned) const;
	void private_control_sequence(char, char) const;
	void private_control_sequence(char, char, unsigned) const;

	void Mode(unsigned n, bool v) const { control_sequence(v ? 'h' : 'l', n); }
	void DECPrivateMode(unsigned n, bool v) const { private_control_sequence('?', v ? 'h' : 'l', n); }
	void DECCursorTabulationControl(unsigned n) const { private_control_sequence('?', 'W', n); }
	void DECSCUSR(unsigned n) const { control_sequence(' ', 'q', n); }
	void DECSCUSR() const { control_sequence(' ', 'q'); }
	// LINUXSCUSR is documented in VGA-softcursor.txt.
	void LINUXSCUSR(unsigned n) const { private_control_sequence('?', 'c', n); }
	void LINUXSCUSR() const { private_control_sequence('?', 'c'); }
	// SCO Private modes are an extension, following the SCO screen(HW) pattern of using '=' as an intermediate character.
	void SCOPrivateMode(unsigned n, bool v) const { private_control_sequence('=', v ? 'h' : 'l', n); }
	void SGRColour8(bool is_fg, unsigned n) const { control_sequence('m', (is_fg ? 30U : 40U) + n); }
	void SGRColour16(bool is_fg, unsigned n) const { if (n >= 8U) n += 90U - 38U; control_sequence('m', (is_fg ? 30U : 40U) + n); }
	void SGRColour256Ambig(bool is_fg, unsigned n) const { control_sequence('m', (is_fg ? 38U : 48U), 5U, n); }
	void SGRColour256(bool is_fg, unsigned n) const;
	void SGRTrueColourAmbig(bool is_fg, unsigned r, unsigned g, unsigned b) const { control_sequence('m', (is_fg ? 38U : 48U), 2U, r, g, b); }
	void SGRTrueColourFaulty(bool is_fg, unsigned r, unsigned g, unsigned b) const;
	void SGRTrueColour(bool is_fg, unsigned r, unsigned g, unsigned b) const;
};

#endif
