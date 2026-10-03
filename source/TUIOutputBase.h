/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_TUIOUTPUTBASE_H)
#define INCLUDE_TUIOUTPUTBASE_H

#include <termios.h>
#include <csignal>
#include <iosfwd>
#include "CharacterCell.h"
#include "ECMA48Output.h"
#include "TUIDisplayCompositor.h"

/// \brief Realize a TUIDisplayCompositor onto an ECMA48Output with the given capabilities and output stream.
///
/// This handles actually emitting the composed output, with ECMA-48 characters and control/escape sequences.
/// It is where output optimization is performed, to reduce the amount of terminal output generated.
/// Various data members record the (assumed) current pen/paper/cursor information of the actual output device.
/// Terminal resize events are handled here, with an initial resize at startup/resume.
class TUIOutputBase
{
public:
	/// \brief Options for a TUIOutputBase
	class Options {
	public:
		Options() : bold_as_colour(false), faint_as_colour(false), no_default_colour(false), cursor_application_mode(false), calculator_application_mode(false), no_alternate_screen_buffer(false), scnm(false), tui_level(0U) {}
		bool bold_as_colour;
		bool faint_as_colour;
		bool no_default_colour;
		bool cursor_application_mode;
		bool calculator_application_mode;
		bool no_alternate_screen_buffer;
		bool scnm;
		unsigned short tui_level;
		enum { TUI_LEVELS = 3U };
	};

	TUIOutputBase(const TerminalCapabilities & t, std::ostream & o, int d, const Options & options, TUIDisplayCompositor & comp);
	~TUIOutputBase();

	/// \brief event handling, called by the main loops of TUIs to handle events, if they are pending
	/// A resize is when the display compositor changes size to reflect the new ECMA48Output size.
	/// A refresh is when the derived class's redraw_new() is called to redraw the new display onto the compositor.
	/// An update is when changes in the display compositor are sent to the ECMA48Output.
	/// A TUI program can send itself a terminal suspension signal.
	/// @{
	void handle_resize_event ();
	void handle_refresh_event ();
	void handle_update_event ();
	void set_refresh_needed() { refresh_needed = true; }
	bool has_update_pending() const { return update_needed; }
	void tstp_signal ();
	void suspend_self ();
	/// @}

protected:
	TUIDisplayCompositor & c;
	const Options options;

	void set_resized() { window_resized = true; }	///< permitted to be called from a signal handler
	void set_update_needed() { update_needed = true; }
	void invalidate_cur() { c.touch_all(); }	///< the next update will update the entire screen
	virtual void redraw_new () = 0;
	void write_changed_cells_to_output ();
	void suspended ();
	void continued ();
	void optimize_scroll_up(unsigned short rows);
	void optimize_scroll_down(unsigned short rows);

private:
	ECMA48Output out;
	int fd;
	/// \brief event pending flags
	/// @{
	sig_atomic_t window_resized;
	bool refresh_needed, update_needed;
	/// @}
	unsigned short cursor_y, cursor_x;
	ColourPair current;
	CharacterCell::attribute_type current_attr;
	CursorSprite::glyph_type cursor_glyph;
	CursorSprite::attribute_type cursor_attributes;
	/// \brief The inversion state of the display compositor.
	///
	/// We do not pass the inversion state through to ECMA48Output::DECSCNM, because various terminal emluators get that wildly wrong.
	/// Instead, we flip the inverse video attribute of each character cell as it is output.
	short int invert_screen;
	bool current_attr_unknown;

	unsigned width (char32_t ch) const;
	void fixup(CharacterCell &, bool, bool) const;
	void print(CharacterCell, bool, bool);
	unsigned count_cheap_narrow(unsigned short row, unsigned short col, unsigned cols) const;
	unsigned count_cheap(unsigned short row, unsigned short col, unsigned cols, CharacterCell::attribute_type attr, uint32_t ch) const;
	unsigned count_cheap_spaces(unsigned short row, unsigned short col, unsigned cols) const;
	unsigned count_cheap_eraseable(unsigned short row, unsigned short col, unsigned cols, CharacterCell::attribute_type attr) const;
	unsigned count_cheap_repeatable(unsigned short row, unsigned short col, unsigned cols, uint32_t ch) const;
	void GotoYX(unsigned short row, unsigned short col);
	void SGRFGColour(const CharacterCell::colour_type & colour) { if (colour != current.foreground) out.SGRColour(true, current.foreground = colour); }
	void SGRBGColour(const CharacterCell::colour_type & colour) { if (colour != current.background) out.SGRColour(false, current.background = colour); }
	void SGRAttr(const CharacterCell::attribute_type & attr);
	void enter_full_screen_mode() ;
	void exit_full_screen_mode() ;

private:
	termios original_attr;
	char out_buffer[64U * 1024U];
};

#endif
