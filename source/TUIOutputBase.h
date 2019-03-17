/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_TUIOUTPUTBASE_H)
#define INCLUDE_TUIOUTPUTBASE_H

#include <termios.h>
#include <csignal>
#include "CharacterCell.h"
#include "ECMA48Output.h"

/// \brief Realize a TUIDisplayCompositor onto an ECMA48Output with the given capabilities and output stream.
///
/// This handles actually emitting the composed output, with ECMA-48 characters and control/escape sequences.
/// It is where output optimization is performed, to reduce the amount of terminal output generated.
/// Various data members record the (assumed) current pen/paper/cursor information of the actual output device.
/// Terminal resize events are handled here, with an initial resize at startup/resume.
class TUIOutputBase
{
public:
	TUIOutputBase(const TerminalCapabilities & t, FILE * f, bool bold_as_colour, bool faint_as_colour, bool cursor_application_mode, bool calculator_application_mode, bool alternate_screen_buffer, TUIDisplayCompositor & comp);
	~TUIOutputBase();

	void handle_resize_event ();
	void handle_refresh_event ();
	void handle_update_event ();
	void set_refresh_needed() { refresh_needed = true; }
	bool has_update_pending() const { return update_needed; }
	void enter_full_screen_mode() ;
	void exit_full_screen_mode() ;

protected:
	TUIDisplayCompositor & c;

	void set_resized() { window_resized = true; }
	void set_update_needed() { update_needed = true; }
	void invalidate_cur() { c.touch_all(); }
	virtual void redraw_new () = 0;
	void erase_new_to_backdrop ();
	void write_changed_cells_to_output ();

private:
	ECMA48Output out;
	const bool bold_as_colour;
	const bool faint_as_colour;
	const bool cursor_application_mode;
	const bool calculator_application_mode;
	const bool alternate_screen_buffer;
	sig_atomic_t window_resized;
	bool refresh_needed, update_needed;
	unsigned short cursor_y, cursor_x;
	CharacterCell::colour_type current_fg, current_bg;
	CharacterCell::attribute_type current_attr;
	CursorSprite::glyph_type cursor_glyph;
	CursorSprite::attribute_type cursor_attributes;

	unsigned width (uint32_t ch) const;
	bool is_blank(uint32_t cols) const;
	void print(const TUIDisplayCompositor::DirtiableCell & cell, bool inverted);
	bool is_cheap_to_print(unsigned short row, unsigned short col, unsigned cols) const;
	bool is_all_blank(unsigned short row, unsigned short col, unsigned cols) const;
	void GotoYX(unsigned short row, unsigned short col);
	void SGRFGColour(const CharacterCell::colour_type & colour) { if (colour != current_fg) out.SGRColour(true, current_fg = colour); }
	void SGRBGColour(const CharacterCell::colour_type & colour) { if (colour != current_bg) out.SGRColour(false, current_bg = colour); }
	void SGRAttr(const CharacterCell::attribute_type & attr);
	void SGRAttr1(const CharacterCell::attribute_type & attr, const CharacterCell::attribute_type & mask, char m, char & semi) const;
	void SGRAttr1(const CharacterCell::attribute_type & attr, const CharacterCell::attribute_type & mask, const CharacterCell::attribute_type & unit, char m, char & semi) const;

private:
	termios original_attr;
	char out_buffer[64U * 1024U];
};

#endif
