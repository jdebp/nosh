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
		/// \name Abstract API
		/// screen output control API called by the terminal emulator, to be implemented by a derived class
		/// @{
		typedef uint16_t coordinate;
		virtual void ReadCell(coordinate s, CharacterCell & c) = 0;
		virtual void WriteNCells(coordinate s, coordinate n, const CharacterCell & c) = 0;
		virtual void ModifyNCells(coordinate s, coordinate n, CharacterCell::attribute_type turnoff, CharacterCell::attribute_type flipon, bool fg_touched, const CharacterCell::colour_type & fg, bool bg_touched, const CharacterCell::colour_type & bg) = 0;
		virtual void CopyNCells(coordinate d, coordinate s, coordinate n) = 0;
		virtual void ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c) = 0;
		virtual void ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c) = 0;
		virtual void SetCursorPos(coordinate x, coordinate y) = 0;
		virtual void SetCursorType(CursorSprite::glyph_type, CursorSprite::attribute_type) = 0;
		virtual void SetPointerType(PointerSprite::attribute_type) = 0;
		virtual void SetScreenFlags(ScreenFlags::flag_type) = 0;
		virtual void SetSize(coordinate w, coordinate h) = 0;
		virtual void SetAltBuffer(bool) = 0;
		/// @}
	};
	class KeyboardBuffer {
	public:
		/// \name Abstract API
		/// keyboard input control API called by the terminal emulator, to be implemented by a derived class
		/// @{
		typedef uint16_t coordinate;
		virtual void WriteLatin1Characters(std::size_t, const char *) = 0;
		virtual void WriteControl1Character(uint8_t) = 0;
		virtual void Set8BitControl1(bool) = 0;
		virtual void SetBackspaceIsBS(bool) = 0;
		virtual void SetEscapeIsFS(bool) = 0;
		virtual void SetDeleteIsDEL(bool) = 0;
		virtual void SetSendPasteEvent(bool) = 0;
		virtual void SetDECFunctionKeys(bool) = 0;
		virtual void SetSCOFunctionKeys(bool) = 0;
		virtual void SetTekenFunctionKeys(bool) = 0;
		virtual void SetCursorApplicationMode(bool) = 0;
		virtual void SetCalculatorApplicationMode(bool) = 0;
		virtual void ReportSize(coordinate w, coordinate h) = 0;
		/// @}
	};
	class MouseBuffer {
	public:
		/// \name Abstract API
		/// mouse input control API called by the terminal emulator, to be implemented by a derived class
		/// @{
		virtual void SetSendXTermMouse(bool) = 0;
		virtual void SetSendXTermMouseClicks(bool) = 0;
		virtual void SetSendXTermMouseButtonMotions(bool) = 0;
		virtual void SetSendXTermMouseNoButtonMotions(bool) = 0;
		virtual void SetSendDECLocator(unsigned int) = 0;
		virtual void SetSendDECLocatorPressEvent(bool) = 0;
		virtual void SetSendDECLocatorReleaseEvent(bool) = 0;
		virtual void RequestDECLocatorReport() = 0;
		/// @}
	};
	typedef uint8_t coordinate;
	struct Setup {
		Setup(coordinate wi, coordinate hi, bool inv, bool pan) : w(wi), h(hi), inverted(inv), pan_is_scroll(pan) {}
		Setup() : w(80U), h(24U), inverted(false), pan_is_scroll(false) {}
		coordinate w, h;
		bool inverted, pan_is_scroll;
	};
	SoftTerm(ScreenBuffer & s, KeyboardBuffer & k, MouseBuffer & m, const Setup & i);
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
	} scroll_origin, display_origin;
	struct xyp : public xy {
		/// This emulates an undocumented DEC VT mechanism, the details of which are in the manual.
		bool advance_pending;
		xyp() : xy(), advance_pending(false) {}
		xyp & operator=(const xy & o) { this->xy::operator=(o); advance_pending = false; return *this; }
	} active_cursor, saved_cursor;
	struct wh {
		coordinate w, h;
		wh(coordinate wp, coordinate hp) : w(wp), h(hp) {}
	} scroll_margin, display_margin;
	bool h_tab_pins[256];
	bool v_tab_pins[256];
	bool scrolling, overstrike, square, altbuffer;
	struct mode {
		bool automatic_right_margin, background_colour_erase, origin, left_right_margins;
		mode();
	} active_modes, saved_modes;
	bool no_clear_screen_on_column_change, pan_is_scroll;
	CharacterCell::attribute_type attributes, saved_attributes;
	ColourPair colour, saved_colour;
	CursorSprite::glyph_type cursor_type;
	CursorSprite::attribute_type cursor_attributes;
	bool send_DECLocator, send_XTermMouse, invert_screen;
	CharacterCell::character_type last_printable_character;

	void Resize(coordinate columns, coordinate rows);
	void UpdateCursorPos();
	void UpdateCursorType();
	void UpdatePointerType();
	void UpdateScreenFlags();
	void SetTopBottomMargins();
	void SetLeftRightMargins();
	void ResetMargins();

	/// \name Concrete UTF-8 Sink
	/// Our implementation of UTF8Decoder::UCS32CharacterSink
	/// @{
	virtual void ProcessDecodedUTF8(char32_t character, bool decoder_error, bool overlong);
	/// @}

	/// \name Concrete ECMA-48 Sink
	/// Our implementation of ECMA48Decoder::ECMA48ControlSequenceSink
	/// @{
	virtual void PrintableCharacter(bool, unsigned short, char32_t);
	virtual void ControlCharacter(char32_t);
	virtual void EscapeSequence(char32_t, char32_t);
	virtual void ControlSequence(char32_t, char32_t, char32_t);
	virtual void ControlString(char32_t, char32_t);
	/// @}

	void SetHorizontalTabstop();
	void SetRegularHorizontalTabstops(argument_type n);
	void ClearAllHorizontalTabstops();
	void ClearAllVerticalTabstops();
	void HorizontalTab(argument_type n, bool);
	void BackwardsHorizontalTab(argument_type n, bool);
	void VerticalTab(argument_type n, bool);
	void DECCursorTabulationControl();
	void CursorTabulationControl();
	void TabClear();

	bool IsVerticalTabstopAt(coordinate p) { return v_tab_pins[p % (sizeof v_tab_pins/sizeof *v_tab_pins)]; }
	void SetVerticalTabstopAt(coordinate p, bool v) { v_tab_pins[p % (sizeof v_tab_pins/sizeof *v_tab_pins)] = v; }
	bool IsHorizontalTabstopAt(coordinate p) { return h_tab_pins[p % (sizeof h_tab_pins/sizeof *h_tab_pins)]; }
	void SetHorizontalTabstopAt(coordinate p, bool v) { h_tab_pins[p % (sizeof h_tab_pins/sizeof *h_tab_pins)] = v; }

	void SetModes(bool);
	void SetMode(argument_type, bool);
	void SetPrivateModes(bool);
	void SetPrivateMode(argument_type, bool);
	void SetSCOModes(bool);
	void SetSCOMode(argument_type, bool);
	void SetAttributes();
	void SetAttributes(std::size_t, CharacterCell::attribute_type &, CharacterCell::attribute_type &, bool &, CharacterCell::colour_type &, bool &, CharacterCell::colour_type &);
	void ChangeAreaAttributes();
	void SGR0();
	void DECSC();
	void DECRC();
	void SCOSCorDESCSLRM();
	void SCORC();
	void SetSCOAttributes();
	void SetSCOCursorType();
	void SendPrimaryDeviceAttributes();
	void SendSecondaryDeviceAttributes();
	void SendTertiaryDeviceAttributes();
	void SetLinuxCursorType();
	void SetCursorStyle();
	void SaveAttributes();
	void RestoreAttributes();
	void SendPrimaryDeviceAttribute(argument_type);
	void SendSecondaryDeviceAttribute(argument_type);
	void SendTertiaryDeviceAttribute(argument_type);
	void SendDeviceStatusReports();
	void SendDeviceStatusReport(argument_type);
	void SendPrivateDeviceStatusReports();
	void SendPrivateDeviceStatusReport(argument_type);
	void SendPresentationStateReports();
	void SendPresentationStateReport(argument_type);
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
	void ScrollUp(argument_type);
	void ScrollDown(argument_type);
	void ScrollLeft(argument_type);
	void ScrollRight(argument_type);
	void EraseCharacters(argument_type);
	void DeleteCharacters(argument_type);
	void InsertCharacters(argument_type);
	void DeleteLines(argument_type);
	void InsertLines(argument_type);
	void DeleteLinesInScrollAreaAt(coordinate, argument_type);
	void InsertLinesInScrollAreaAt(coordinate, argument_type);
	void DeleteColumnsInScrollAreaAt(coordinate, argument_type);
	void InsertColumnsInScrollAreaAt(coordinate, argument_type);
	void PanUp(argument_type);
	void PanDown(argument_type);

	void SaveCursor();
	void RestoreCursor();
	bool WillWrap();
	void Advance();
	void ClearPendingAdvance();
	void AdvanceOrPend();
	void GotoYX(argument_type, argument_type);
	void GotoX(argument_type);
	void GotoY(argument_type);
	void Home();
	void CarriageReturn();
	void CarriageReturnNoUpdate();
	void CursorDown(argument_type, bool);
	void CursorUp(argument_type, bool);
	void CursorLeft(argument_type, bool);
	void CursorRight(argument_type, bool);

	void RequestLocatorReport();
	void EnableLocatorReports();
	void SelectLocatorEvents();
	void SelectLocatorEvent(unsigned int);

	void ResetToInitialState();
	void SoftReset();
	void RepeatPrintableCharacter(unsigned int);
};

#endif
