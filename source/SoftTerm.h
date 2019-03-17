/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_SOFTTERM_H)
#define INCLUDE_SOFTTERM_H

#include <stdint.h>
#include <cstddef>
#include "CharacterCell.h"
#include "ControlCharacters.h"
#include "UTF8Decoder.h"
#include "ECMA48Decoder.h"

class SoftTerm :
	public UTF8Decoder::UCS32CharacterSink,
	public ECMA48Decoder::ECMA48ControlSequenceSink
{
public:
	class ScreenBuffer {
	public:
		typedef uint16_t coordinate;
		virtual void WriteNCells(coordinate s, coordinate n, const CharacterCell & c) = 0;
		virtual void CopyNCells(coordinate d, coordinate s, coordinate n) = 0;
		virtual void ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c) = 0;
		virtual void ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c) = 0;
		virtual void SetCursorPos(coordinate x, coordinate y) = 0;
		virtual void SetCursorType(CursorSprite::glyph_type, CursorSprite::attribute_type) = 0;
		virtual void SetPointerType(PointerSprite::attribute_type) = 0;
		virtual void SetSize(const coordinate & w, const coordinate & h) = 0;
	};
	class KeyboardBuffer {
	public:
		typedef uint16_t coordinate;
		virtual void WriteLatin1Characters(std::size_t, const char *) = 0;
		virtual void WriteControl1Character(uint8_t) = 0;
		virtual void Set8BitControl1(bool) = 0;
		virtual void SetBackspaceIsBS(bool) = 0;
		virtual void SetDeleteIsDEL(bool) = 0;
		virtual void SetSendPasteEvent(bool) = 0;
		virtual void SetCursorApplicationMode(bool) = 0;
		virtual void SetCalculatorApplicationMode(bool) = 0;
		virtual void ReportSize(coordinate w, coordinate h) = 0;
	};
	class MouseBuffer {
	public:
		virtual void SetSendXTermMouse(bool) = 0;
		virtual void SetSendXTermMouseClicks(bool) = 0;
		virtual void SetSendXTermMouseButtonMotions(bool) = 0;
		virtual void SetSendXTermMouseNoButtonMotions(bool) = 0;
		virtual void SetSendDECLocator(unsigned int) = 0;
		virtual void SetSendDECLocatorPressEvent(bool) = 0;
		virtual void SetSendDECLocatorReleaseEvent(bool) = 0;
		virtual void RequestDECLocatorReport() = 0;
	};
	typedef uint8_t coordinate;
	SoftTerm(ScreenBuffer & s, KeyboardBuffer & k, MouseBuffer & m, coordinate w, coordinate h);
	~SoftTerm();
	void Process(uint_fast8_t character) { utf8_decoder.Process(character); }
protected:
	UTF8Decoder utf8_decoder;
	ECMA48Decoder ecma48_decoder;
	ScreenBuffer & screen;
	KeyboardBuffer & keyboard;
	MouseBuffer & mouse;
	struct xy {
		coordinate x, y;
		xy() : x(0U), y(0U) {}
	} active_cursor, saved_cursor, scroll_origin, display_origin;
	struct wh {
		coordinate w, h;
		wh(coordinate wp, coordinate hp) : w(wp), h(hp) {}
	} scroll_margin, display_margin;
	bool h_tab_pins[256];
	bool v_tab_pins[256];
	bool scrolling, overstrike, square;
	struct mode {
		bool automatic_right_margin, background_colour_erase, origin, left_right_margins;
		mode();
	} active_modes, saved_modes;
	bool no_clear_screen_on_column_change;
	/// This emulates an undocumented DEC VT mechanism, the details of which are in the manual.
	bool advance_pending;
	CharacterCell::attribute_type attributes, saved_attributes;
	CharacterCell::colour_type foreground, background, saved_foreground, saved_background;
	CursorSprite::glyph_type cursor_type;
	CursorSprite::attribute_type cursor_attributes;
	bool send_DECLocator, send_XTermMouse;

	void ResetToInitialState();
	void SoftReset();

	void Resize(coordinate columns, coordinate rows);
	void UpdateCursorPos();
	void UpdateCursorType();
	void UpdatePointerType();
	void SetTopBottomMargins();
	void SetLeftRightMargins();
	void ResetMargins();

	virtual void ProcessDecodedUTF8(uint32_t character, bool decoder_error, bool overlong);
	virtual void PrintableCharacter(bool, unsigned short, uint_fast32_t);
	virtual void ControlCharacter(uint_fast32_t);
	virtual void EscapeSequence(uint_fast32_t, char);
	virtual void ControlSequence(uint_fast32_t, char, char);
	virtual void ControlString(uint_fast32_t);

	coordinate SumArgs();

	void SetHorizontalTabstop();
	void SetRegularHorizontalTabstops(coordinate n);
	void ClearAllHorizontalTabstops();
	void ClearAllVerticalTabstops();
	void HorizontalTab(coordinate n, bool);
	void BackwardsHorizontalTab(coordinate n, bool);
	void VerticalTab(coordinate n, bool);
	void DECCursorTabulationControl();
	void CursorTabulationControl();
	void TabClear();

	bool IsVerticalTabstopAt(coordinate p) { return v_tab_pins[p % (sizeof v_tab_pins/sizeof *v_tab_pins)]; }
	void SetVerticalTabstopAt(coordinate p, bool v) { v_tab_pins[p % (sizeof v_tab_pins/sizeof *v_tab_pins)] = v; }
	bool IsHorizontalTabstopAt(coordinate p) { return h_tab_pins[p % (sizeof h_tab_pins/sizeof *h_tab_pins)]; }
	void SetHorizontalTabstopAt(coordinate p, bool v) { h_tab_pins[p % (sizeof h_tab_pins/sizeof *h_tab_pins)] = v; }

	void SetModes(bool);
	void SetMode(unsigned int, bool);
	void SetPrivateModes(bool);
	void SetPrivateMode(unsigned int, bool);
	void SetAttributes();
	void SGR0();
	void SCOSCorDESCSLRM();
	void SetSCOAttributes();
	void SetSCOCursorType();
	void SendPrimaryDeviceAttributes();
	void SendSecondaryDeviceAttributes();
	void SendTertiaryDeviceAttributes();
	void SetLinuxCursorType();
	void SetCursorStyle();
	void SaveAttributes();
	void RestoreAttributes();
	void SetAttribute(unsigned int);
	void SendPrimaryDeviceAttribute(unsigned int);
	void SendSecondaryDeviceAttribute(unsigned int);
	void SendTertiaryDeviceAttribute(unsigned int);
	void SendDeviceStatusReports();
	void SendDeviceStatusReport(unsigned int);
	void SendPrivateDeviceStatusReports();
	void SendPrivateDeviceStatusReport(unsigned int);
	void SaveModes();
	void RestoreModes();
	void SetLinesPerPage();
	void SetLinesPerPageOrDTTerm();
	void SetColumnsPerPage();
	void SetScrollbackBuffer(bool);

	CharacterCell ErasureCell(uint32_t c = ' ');
	void ClearDisplay(uint32_t c = ' ');
	void ClearLine();
	void ClearToEOD();
	void ClearToEOL();
	void ClearFromBOD();
	void ClearFromBOL();
	void EraseInDisplay();
	void EraseInLine();
	void ScrollUp(coordinate);
	void ScrollDown(coordinate);
	void ScrollLeft(coordinate);
	void ScrollRight(coordinate);
	void EraseCharacters(coordinate);
	void DeleteCharacters(coordinate);
	void InsertCharacters(coordinate);
	void DeleteLines(coordinate);
	void InsertLines(coordinate);
	void DeleteLinesInScrollAreaAt(coordinate, coordinate);
	void InsertLinesInScrollAreaAt(coordinate, coordinate);
	void DeleteColumnsInScrollAreaAt(coordinate, coordinate);
	void InsertColumnsInScrollAreaAt(coordinate, coordinate);

	void GotoYX();
	void SaveCursor();
	void RestoreCursor();
	bool WillWrap();
	void Advance();
	void GotoX(coordinate);
	void GotoY(coordinate);
	void Home();
	void CarriageReturn();
	void CarriageReturnNoUpdate();
	void CursorDown(coordinate, bool, bool);
	void CursorUp(coordinate, bool, bool);
	void CursorLeft(coordinate, bool, bool);
	void CursorRight(coordinate, bool, bool);

	void RequestLocatorReport();
	void EnableLocatorReports();
	void SelectLocatorEvents();
	void SelectLocatorEvent(unsigned int);
};

#endif
