/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_SOFTTERM_H)
#define INCLUDE_SOFTTERM_H

#include <stdint.h>
#include <cstddef>
#include "CharacterCell.h"

class SoftTerm 
{
public:
	class ScreenBuffer {
	public:
		typedef unsigned short coordinate;
		virtual void WriteNCells(coordinate s, coordinate n, const CharacterCell & c) = 0;
		virtual void CopyNCells(coordinate d, coordinate s, coordinate n) = 0;
		virtual void ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c) = 0;
		virtual void ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c) = 0;
		virtual void SetCursorPos(coordinate x, coordinate y) = 0;
		virtual void SetCursorType(CursorSprite::glyph_type, CursorSprite::attribute_type) = 0;
		virtual void SetPointerType(PointerSprite::attribute_type) = 0;
		virtual void SetSize(coordinate w, coordinate h) = 0;
	};
	class KeyboardBuffer {
	public:
		typedef unsigned short coordinate;
		virtual void WriteLatin1Characters(std::size_t, const char *) = 0;
		virtual void WriteControl1Character(uint8_t) = 0;
		virtual void Set8BitControl1(bool) = 0;
		virtual void SetBackspaceIsBS(bool) = 0;
		virtual void SetDeleteIsDEL(bool) = 0;
		virtual void SetSendPasteEvent(bool) = 0;
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
	SoftTerm(ScreenBuffer & s, KeyboardBuffer & k, MouseBuffer & m);
	~SoftTerm();
	void Write(uint32_t character, bool decoder_error, bool overlong);
protected:
	ScreenBuffer & screen;
	KeyboardBuffer & keyboard;
	MouseBuffer & mouse;
	struct xy {
		coordinate x, y;
		xy();
	} active_cursor, saved_cursor, scroll_origin, display_origin;
	struct wh {
		coordinate w, h;
		wh();
	} scroll_margin, display_margin;
	std::size_t argc;
	unsigned int args[16];
	bool seen_arg_digit;
	char first_private_parameter, last_intermediate;
	bool h_tab_pins[256];
	bool v_tab_pins[256];
	enum { NORMAL, ESCAPE1, ESCAPE2, CONTROL1, CONTROL2 } state;
	bool scrolling, overstrike;
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

	enum {
		NUL = '\0',
		BEL = '\a',
		CR = '\r',
		LF = '\n',
		VT = '\v',
		TAB = '\t',
		FF = '\f',
		BS = '\b',
		CAN = 0x18,
		ESC = 0x1b,
		DEL = 0x7f,
		IND = 0x84,
		NEL = 0x85,
		HTS = 0x88,
		RI = 0x8d,
		SS2 = 0x8e,
		SS3 = 0x8f,
		DCS = 0x90,
		SOS = 0x98,
		CSI = 0x9b,
		ST = 0x9c,
		OSC = 0x9d,
		PM = 0x9e,
		APC = 0x9f,
	};

	void ResetToInitialState();
	void SoftReset();

	void Resize(coordinate columns, coordinate rows);
	void UpdateCursorPos();
	void UpdateCursorType();
	void UpdatePointerType();
	void SetTopBottomMargins();
	void SetLeftRightMargins();
	void ResetMargins();

	bool IsControl(uint32_t);
	bool IsIntermediate(uint32_t);
	bool IsParameter(uint32_t);
	void Print(bool, uint32_t);
	void Escape1(uint32_t);
	void Escape2(uint32_t);
	void ControlSequence(uint32_t);
	void ProcessControlCharacter(uint32_t character);

	void ResetControlSequence();
	void FinishArg(unsigned int d);
	coordinate SumArgs();

	void SetHorizontalTabstop();
	void ClearAllHorizontalTabstops();
	void ClearAllVerticalTabstops();
	void HorizontalTab(coordinate n, bool);
	void BackwardsHorizontalTab(coordinate n, bool);
	void VerticalTab(coordinate n, bool);
	void TabControl();
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
