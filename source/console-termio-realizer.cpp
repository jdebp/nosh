/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#define _BSD_SOURCE 1
#include <vector>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <stdint.h>
#include "kqueue_common.h"
#include <sys/param.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "InputMessage.h"
#include "TerminalCapabilities.h"
#include "TUIDisplayCompositor.h"
#include "TUIOutputBase.h"
#include "TUIInputBase.h"
#include "TUIVIO.h"
#include "VirtualTerminalRealizer.h"
#include "VirtualTerminalBackEnd.h"
#include "SignalManagement.h"
#include "kbdmap.h"

/* Realizing a virtual terminal onto a set of physical devices **************
// **************************************************************************
*/

namespace {

using namespace VirtualTerminalRealizer;

struct TUIOptions :
	public TUIOutputBase::Options
{
	TUIOptions() : TUIOutputBase::Options(), quadrant(3U), wrong_way_up(false) {}
	unsigned long quadrant;	///< 0, 1, 2, or 3
	bool wrong_way_up;	///< causes coordinate transforms between c and vt
};

class Realizer :
	public TerminalCapabilities,
	public TUIOutputBase,
	public TUIInputBase
{
public:
	Realizer(const char * prog, const ProcessEnvironment & envs, const TUIOptions &, const bool, const bool);
	~Realizer();

	void configure(const char * vt_dirname, const char * keyboard_state_filename, const char * mouse_state_filename);
	void loop();
protected:
	class EventHandler : public TUIInputBase::EventHandler {
	public:
		EventHandler(Realizer & r0) : TUIInputBase::EventHandler(r0), r(r0) {}
		~EventHandler();
	protected:
		virtual bool ExtendedKey(uint_fast16_t k, uint_fast8_t m);
		virtual bool FunctionKey(uint_fast16_t k, uint_fast8_t m);
		virtual bool UCS3(char32_t character);
		virtual bool Accelerator(char32_t character);
		virtual bool MouseMove(uint_fast16_t, uint_fast16_t, uint8_t);
		virtual bool MouseWheel(uint_fast8_t n, int_fast8_t v, uint_fast8_t m);
		virtual bool MouseButton(uint_fast8_t n, uint_fast8_t v, uint_fast8_t m);
		Realizer & r;
	} handler0;
	friend class EventHandler;

	const char * prog;
	const ProcessEnvironment & envs;
	const FileDescriptorOwner queue;
	std::vector<struct kevent> ip;

	VirtualTerminalBackEnd::xy screen, widgets;
	VirtualTerminalBackEnd::xy visible_origin;
	VirtualTerminalBackEnd::wh visible_size;
	uint8_t modifiers_cur;
	KeyboardLEDs leds;
	bool mouse_buttons[MouseState::NUM_BUTTONS];
	MouseState mouse_state;
	KeyboardModifierState keyboard_state;
	VirtualTerminalBackEnd vt;
	TUIDisplayCompositor compositor;
	TUIOptions options;
	TUIVIO vio;
	enum { widgets_width = 79 };
	const bool has_pointer;
	const bool display_only;
	const bool mouse_primary;
	sig_atomic_t terminate_signalled, interrupt_signalled, hangup_signalled, usr1_signalled, usr2_signalled;

	bool exit_signalled() const { return terminate_signalled||interrupt_signalled||hangup_signalled; }
	void handle_signal (int);
	void handle_stdin (int);
	bool has_idle_work();
	void do_idle_work();

	virtual void redraw_new ();
	void position_vt_visible_area ();
	void compose_new_from_vt ();
	void write_widgets ();

	void update_locators();
	void update_LEDs();
	void update_modifiers(uint_fast8_t modifiers);

#if 0 // TODO: not yet
	static unsigned long column_to_pixel(unsigned short x) { return x * CHARACTER_PIXEL_WIDTH; }
	static unsigned long row_to_pixel(unsigned short y) { return y * CHARACTER_PIXEL_HEIGHT; }
	static unsigned long depth_to_pixel(unsigned short z) { return z * CHARACTER_PIXEL_DEPTH; }
#endif
private:
	char stdin_buffer[1U * 1024U];
};

const CharacterCell::colour_type widget_fg(ALPHA_FOR_TRUE_COLOURED,0xC0,0xC0,0xC0), widget_bg(ALPHA_FOR_TRUE_COLOURED,0,0,0);
const ColourPair widgets_colour(widget_fg, widget_bg);

static_assert(KBDMAP_TOTAL_MODIFIERS <= 32U, "The return type is too small for the number of keyboard map modifiers.");
inline
uint32_t
InputMessageToKbdMapModifiers(
	uint_fast8_t modifiers
) {
	return
		((modifiers & INPUT_MODIFIER_LEVEL2)	? (1U << KBDMAP_MODIFIER_1ST_LEVEL2)	: 0U) |
		((modifiers & INPUT_MODIFIER_LEVEL3)	? (1U << KBDMAP_MODIFIER_1ST_LEVEL3)	: 0U) |
		((modifiers & INPUT_MODIFIER_CONTROL)	? (1U << KBDMAP_MODIFIER_1ST_CONTROL)	: 0U) |
		((modifiers & INPUT_MODIFIER_GROUP2)	? (1U << KBDMAP_MODIFIER_1ST_GROUP2)	: 0U) |
		((modifiers & INPUT_MODIFIER_SUPER)	? (1U << KBDMAP_MODIFIER_1ST_SUPER)	: 0U) ;
}

}

Realizer::Realizer(
	const char * p,
	const ProcessEnvironment & e,
	const TUIOptions & o,
	const bool d_o,
	const bool mp
) :
	TerminalCapabilities(e),
	TUIOutputBase(*this, std::cout, STDOUT_FILENO, o, compositor),
	TUIInputBase(static_cast<const TerminalCapabilities &>(*this), std::cin, STDIN_FILENO),
	handler0(*this),
	prog(p),
	envs(e),
	queue(kqueue()),
	ip(),
	screen(),
	widgets(),
	visible_origin(),
	visible_size(),
	modifiers_cur(-1U),
	leds(),
	mouse_state(),
	keyboard_state(),
	vt(),
	compositor(false /* no software cursor */, 24, 80),
	options(o),
	vio(compositor),
	has_pointer(use_DECLocator || (use_DECPrivateMode && has_XTerm1006Mouse)),
	display_only(d_o),
	mouse_primary(mp),
	terminate_signalled(false),
	interrupt_signalled(false),
	hangup_signalled(false),
	usr1_signalled(false),
	usr2_signalled(false)
{
	if (0 > queue.get()) {
		die_errno(prog, envs, "kqueue");
	}

}

Realizer::~Realizer()
{
}

/// \brief Clip and position the visible portion of the terminal's display buffer.
inline
void
Realizer::position_vt_visible_area (
) {
	// Glue the terminal window to the edges of the display screen buffer.
	screen.y = !(options.quadrant & 0x02) && vt.query_size().h < c.query_h() ? c.query_h() - vt.query_size().h : 0U;
	screen.x = (1U == options.quadrant || 2U == options.quadrant) && vt.query_size().w < c.query_w() ? c.query_w() - vt.query_size().w : 0U;
	// The widgets go above or below the terminal window.
	widgets.y = vt.query_size().h >= c.query_h() ? vt.query_size().h : (options.quadrant & 0x02) ? c.query_h() - 1U : 0U;
	widgets.x = (1U == options.quadrant || 2U == options.quadrant) && c.query_w() > widgets_width ? c.query_w() - widgets_width : 0U;
	// Ask the VirtualTerminal to position the visible window according to cursor placement.
	const VirtualTerminalBackEnd::wh area(c.query_w() - screen.x, c.query_h() - screen.y);
	vt.calculate_visible_rectangle(area, visible_origin, visible_size);
}

/// \brief Render the terminal's display buffer and cursor/pointer states onto the TUI compositor.
inline
void
Realizer::compose_new_from_vt ()
{
	for (unsigned short row(0U); row < visible_size.h; ++row) {
		const unsigned short source_row(visible_origin.y + row);
		const unsigned short dest_row(screen.y + (options.wrong_way_up ? visible_size.h - row - 1U : row));
		for (unsigned short col(0U); col < visible_size.w; ++col)
			c.poke(dest_row, col + screen.x, vt.at(source_row, visible_origin.x + col));
	}
	const CursorSprite::attribute_type a(vt.query_cursor_attributes());
	// If the cursor is invisible, we are not guaranteed that the VirtualTerminal has kept the visible area around it.
	if (CursorSprite::VISIBLE & a) {
		const unsigned short cursor_y(screen.y + (options.wrong_way_up ? visible_size.h - vt.query_cursor().y - 1U : vt.query_cursor().y) - visible_origin.y);
		c.move_cursor(cursor_y, vt.query_cursor().x - visible_origin.x + screen.x);
	}
	c.set_cursor_state(a, vt.query_cursor_glyph());
	if (c.set_screen_flags(vt.query_screen_flags()))
		set_update_needed();
	if (has_pointer)
		c.set_pointer_attributes(vt.query_pointer_attributes());
}

inline
void
Realizer::write_widgets (
) {
	// Per ISO/IEC 9995-7 via Unicode L2/17-072:
	//    Caps Lock		= U+21EC (upwards white arrow on pedestal with horizontal bar)
	//    Level 3 Shift	= U+21EE (upwards white double arrow)
	//    Group2 Latch	= U+21D2 (rightwards white double arrow) not U+1F8B6
	// To which we add:
	//    Level 2 Lock	= U+21EB (upwards white arrow on pedestal)
	//    Level 3 Lock	= U+21EF (upwards white double arrow on pedestal)
	//    Num Lock		= U+21ED (upwards white arrow on pedestal with vertical bar)
	//    Level 2 Shift	= U+21E7 (upwards white single arrow)
	//    Super		= U+2318 (place of interest sign)
	//    Control		= U+2388 (helm symbol)
	//    Alt		= U+2387 (alternative key symbol)
	static const char widget_text_unicode[] =
		"\u21ED\u21EC\u21EB\u21EF\u21D2\u2388\u21E7\u21EE\u2318\u2387"
		"\u2588"	// one black box
		"\U0001fbbc"	// system menu button
		"\u2588\u2588"	// two black boxes
		"\U0001fbb5"	// arrow left
		"\U0001fb96"
		"\U0001fb96"
		"\U0001fbb6"	// arrow right
		"\u2588\u2588"	// two black boxes
		".\u250a\u82f1 \u6570 "			// eisu
		"\u2588"	// one black box
		"K\u250a\u30ab \u30bf \u30ab \u30ca "	// Ka Ta Ka Na
		"\u2588"	// one black box
		"L\u250a\u3072 \u3089 \u30c4 \u306a "	// Hi Ra Ga Na
		"\u2588"	// one black box
		"R\u250a\u30ed \u30fc \u30de \u5b57 "	// Ro Ma Ji
		"\u2588"	// one black box
		"G\u250a\ud55c \uc601 "			// Han Ja
		"\u2588"	// one black box
		"Z\u250a\u6f22 \u5b57 \u250a\ud55c \uc790 "	// Kan Ji Han Ji
		"\u2588"	// one black box
		"C\u250a\u5909 \u63db "			// Hen Kan
		"\u2588"	// one black box
		"N\u250a\u7121 \u5909 \u63db "		// Mu Hen Kan
		"\u2588"	// one black box
		;
	static const char widget_text_old_unicode[] =
		"\u21ED\u21EC\u21EB\u21EF\u21D2\u2388\u21E7\u21EE\u2318\u2387"
		"\u2591"
		"\u23EE "	// rewind
		"\u2591"
		"\u23F4 "	// triangle left
		"\u2591"
		"\u23F5 "	// triangle right
		"\u2591"
		".\u250a\u82f1 \u6570 "			// eisu
		"\u2591"
		"K\u250a\u30ab \u30bf \u30ab \u30ca "	// Ka Ta Ka Na
		"\u2591"
		"L\u250a\u3072 \u3089 \u30c4 \u306a "	// Hi Ra Ga Na
		"\u2591"
		"R\u250a\u30ed \u30fc \u30de \u5b57 "	// Ro Ma Ji
		"\u2591"
		"G\u250a\ud55c \uc601 "			// Han Ja
		"\u2591"
		"Z\u250a\u6f22 \u5b57 |\ud55c \uc790 "	// Kan Ji Han Ji
		"\u2591"
		"C\u250a\u5909 \u63db "			// Hen Kan
		"\u2591"
		"N\u250a\u7121 \u5909 \u63db "		// Mu Hen Kan
		;
	static const char widget_text_ascii[] =
		"NC23$^23GA"
		" "
		"0 "
		" "
		"|<"
		" "
		">|"
		" "
		"./eisu"
		" "
		"Katakana  "
		" "
		"L/Hiragana"
		" "
		"Roumaji   "
		" "
		"G/Hnja"
		" "
		"Z/Chinese "
		" "
		"Conv  "
		" "
		"Noconv  "
		;
	static const char * widget_text[TUIOutputBase::Options::TUI_LEVELS] = { widget_text_unicode, widget_text_old_unicode, widget_text_ascii };
	vio.WriteCharStrAttrUTF8(widgets.y, widgets.x, 0, widgets_colour, widget_text[options.tui_level], std::strlen(widget_text[options.tui_level]));
	vio.WriteNAttrs(widgets.y, widgets.x + 0U, leds.num_lock() ? CharacterCell::INVERSE : 0, widgets_colour, 1U);
	vio.WriteNAttrs(widgets.y, widgets.x + 1U, leds.caps_lock() ? CharacterCell::INVERSE : 0, widgets_colour, 1U);
	vio.WriteNAttrs(widgets.y, widgets.x + 2U, leds.shift2_lock() ? CharacterCell::INVERSE : 0, widgets_colour, 1U);
	vio.WriteNAttrs(widgets.y, widgets.x + 3U, leds.shift3_lock() ? CharacterCell::INVERSE : 0, widgets_colour, 1U);
	vio.WriteNAttrs(widgets.y, widgets.x + 4U, leds.group2() ? CharacterCell::INVERSE : 0, widgets_colour, 1U);
	vio.WriteNAttrs(widgets.y, widgets.x + 5U, leds.control() ? CharacterCell::INVERSE : 0, widgets_colour, 1U);
	vio.WriteNAttrs(widgets.y, widgets.x + 6U, leds.shift2() ? CharacterCell::INVERSE : 0, widgets_colour, 1U);
	vio.WriteNAttrs(widgets.y, widgets.x + 7U, leds.shift3() ? CharacterCell::INVERSE : 0, widgets_colour, 1U);
	vio.WriteNAttrs(widgets.y, widgets.x + 8U, leds.super() ? CharacterCell::INVERSE : 0, widgets_colour, 1U);
	vio.WriteNAttrs(widgets.y, widgets.x + 9U, leds.alt() ? CharacterCell::INVERSE : 0, widgets_colour, 1U);
}

void
Realizer::redraw_new(
) {
	if (options.tui_level < 1U)
		vio.CLSToCheckerBoardFill(ColourPair::def);
	else
		vio.CLSToHalfTone(ColourPair::def);
	position_vt_visible_area();
	compose_new_from_vt();
	write_widgets();
}

void
Realizer::update_modifiers(
	const uint_fast8_t modifiers
) {
	if (modifiers == modifiers_cur) return;
	const uint32_t kbdmap_modifiers(InputMessageToKbdMapModifiers(modifiers));
	for (unsigned m(0); m < KBDMAP_TOTAL_MODIFIERS; ++m)
		keyboard_state.set_pressed(m, kbdmap_modifiers & (1U << m));
	modifiers_cur = modifiers;
}

bool
Realizer::EventHandler::ExtendedKey(
	uint_fast16_t k,
	uint_fast8_t modifiers
) {
	r.update_modifiers(modifiers);
	r.vt.WriteInputMessage(MessageForExtendedKey(k, r.keyboard_state.modifiers()));
	r.update_modifiers(0U);
	return true;
}

bool
Realizer::EventHandler::FunctionKey(
	uint_fast16_t k,
	uint_fast8_t modifiers
) {
	r.update_modifiers(modifiers);
	r.vt.WriteInputMessage(MessageForFunctionKey(k, r.keyboard_state.modifiers()));
	r.update_modifiers(0U);
	return true;
}

bool
Realizer::EventHandler::UCS3(
	char32_t character
) {
	r.vt.WriteInputMessage(MessageForUCS3(character));
	return true;
}

bool
Realizer::EventHandler::Accelerator(
	char32_t character
) {
	r.vt.WriteInputMessage(MessageForAcceleratorKey(character));
	return true;
}

bool
Realizer::EventHandler::MouseMove(
	uint_fast16_t row,
	uint_fast16_t col,
	uint8_t modifiers
) {
	r.update_modifiers(modifiers);
	const unsigned long x(SharedHODResources::column_to_pixel(col));
	const unsigned long y(SharedHODResources::column_to_pixel(row));
	r.mouse_state.abspos(1U, x);
	r.mouse_state.abspos(2U, y);
	r.update_modifiers(0U);
	return true;
}

bool
Realizer::EventHandler::MouseWheel(
	uint_fast8_t wheel,
	int_fast8_t value,
	uint_fast8_t modifiers
) {
	r.update_modifiers(modifiers);
	r.mouse_state.wheel(wheel, value);
	r.update_modifiers(0U);
	return true;
}

bool
Realizer::EventHandler::MouseButton(
	uint_fast8_t button,
	uint_fast8_t value,
	uint_fast8_t modifiers
) {
	if (0U == button
	&&  value
	&&  r.widgets.y == r.c.query_pointer_row()
	&&  r.widgets.x <= r.c.query_pointer_col()
	&&  r.c.query_pointer_col() < r.widgets.x + r.widgets_width
	) {
		switch (r.c.query_pointer_col() - r.widgets.x) {
			case 11U:
				r.vt.WriteInputMessage(MessageForConsumerKey(CONSUMER_KEY_SELECT_TASK, 0));
				break;
			case 14U:
				r.vt.WriteInputMessage(MessageForConsumerKey(CONSUMER_KEY_PREVIOUS_TASK, 0));
				break;
			case 17U:
				r.vt.WriteInputMessage(MessageForConsumerKey(CONSUMER_KEY_NEXT_TASK, 0));
				break;
			case 19U: case 20U: case 21U: case 22U: case 23U: case 24U: 
				r.vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_IM_TOGGLE, 0));
				break;
			case 26U: case 27U: case 28U: case 29U: case 30U: case 31U: case 32U: case 33U: case 34U: case 35U: 
				r.vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_KATAKANA, 0));
				break;
			case 37U: case 38U: case 39U: case 40U: case 41U: case 42U: case 43U: case 44U: case 45U: case 46U: 
				r.vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_HIRAGANA, 0));
				break;
			case 48U: case 49U: case 50U: case 51U: case 52U: case 53U: case 54U: case 55U: case 56U: case 57U: 
				r.vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_ROMAJI, 0));
				break;
			case 59U: case 60U: case 61U: case 62U: case 63U: case 64U: 
				r.vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_HANGEUL, 0));
				break;
			case 66U: case 67U: case 68U: case 69U: case 70U: case 71U: case 72U: case 73U: case 74U: case 75U: 
				r.vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_HANJA, 0));
				break;
			case 77U: case 78U: case 79U: case 80U: case 81U: case 82U: 
				r.vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_HENKAN, 0));
				break;
			case 84U: case 85U: case 86U: case 87U: case 88U: case 89U: case 90U: case 91U: 
				r.vt.WriteInputMessage(MessageForExtendedKey(EXTENDED_KEY_MUHENKAN, 0));
				break;
			default:
				break;
		}
		return true;
	}
	r.update_modifiers(modifiers);
	r.mouse_state.button(button, value);
	r.update_modifiers(0U);
	return true;
}

void
Realizer::update_LEDs(
) {
	const KeyboardLEDs newleds(keyboard_state.query_LEDs());
	if (leds != newleds) {
		leds = newleds;
		set_refresh_needed();
	}
}

void
Realizer::update_locators(
) {
	unsigned long x(mouse_state.query_pos(1U));
	unsigned long y(mouse_state.query_pos(2U));
//	unsigned long z(mouse_state.query_pos(3U));
	const SharedHODResources::coordinate col(SharedHODResources::pixel_to_column(x));
	SharedHODResources::coordinate row(SharedHODResources::pixel_to_row(y));
//	const SharedHODResources::coordinate dep(SharedHODResources::pixel_to_depth(z));
	const uint8_t modifiers(keyboard_state.modifiers());

	if (c.change_pointer_row(row)) {
		if (options.wrong_way_up)
			row = c.query_h() - row - 1U;
		vt.WriteInputMessage(MessageForMouseRow(row, modifiers));
		set_update_needed();
	}
	if (c.change_pointer_col(col)) {
		vt.WriteInputMessage(MessageForMouseColumn(col, modifiers));
		set_update_needed();
	}

	if (!display_only && mouse_primary) {
		bool new_mouse_buttons[MouseState::NUM_BUTTONS];
		mouse_state.query_buttons(new_mouse_buttons);
		for (unsigned i(0U); i < sizeof mouse_buttons/sizeof *mouse_buttons; ++i) {
			const bool v(new_mouse_buttons[i]);
			if (v != mouse_buttons[i]) {
				vt.WriteInputMessage(MessageForMouseButton(i, v, modifiers));
				mouse_buttons[i] = v;
			}
		}
		for (unsigned i(0U); i < MouseState::NUM_WHEELS; ++i) {
			int32_t d(mouse_state.get_and_reset_wheel(i));
			while (d) {
				const int8_t v(d < -128 ? -128 : d > 127 ? 127 : d);
				vt.WriteInputMessage(MessageForMouseWheel(i, v, modifiers));
				d -= v;
			}
		}
	}
}

// Seat of the class
Realizer::EventHandler::~EventHandler() {}

void
Realizer::handle_stdin (
	int n		///< number of characters available; can be <= 0 erroneously
) {
	for (;;) {
		int l(read(STDIN_FILENO, stdin_buffer, sizeof stdin_buffer));
		if (0 >= l) break;
		HandleInput(stdin_buffer, l);
		if (l >= n) break;
		n -= l;
	}
	BreakInput();
}

void
Realizer::handle_signal (
	int signo
) {
	switch (signo) {
		case SIGWINCH:	set_resized(); break;
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
		case SIGUSR1:	usr1_signalled = true; break;
		case SIGUSR2:	usr2_signalled = true; break;
	}
}

inline
bool
Realizer::has_idle_work(
) {
	return vt.query_reload_needed();
}

inline
void
Realizer::do_idle_work(
) {
	if (vt.query_reload_needed()) {
		vt.reload();
		set_refresh_needed();
	}
}

void
Realizer::configure(
	const char * vt_dirname,
	const char * keyboard_state_filename,
	const char * mouse_state_filename
) {
	vt.set_dir_name(vt_dirname);
	FileDescriptorOwner vt_dir_fd(open_dir_at(AT_FDCWD, vt_dirname));
	if (0 > vt_dir_fd.get()) {
		die_errno(prog, envs, vt_dirname);
	}
	{
		FileDescriptorOwner buffer_fd(open_read_at(vt_dir_fd.get(), "display"));
		if (0 > buffer_fd.get()) {
			die_errno(prog, envs, vt_dirname, "display");
		}
		vt.set_buffer_file(fdopen(buffer_fd.get(), "r"));
		if (!vt.query_buffer_file()) {
			die_errno(prog, envs, vt_dirname, "display");
		}
		buffer_fd.release();
		append_event(ip, vt.query_buffer_fd(), EVFILT_VNODE, EV_ADD|EV_ENABLE|EV_CLEAR, NOTE_WRITE, 0, nullptr);
	}

	if (!display_only) {
		FileDescriptorOwner input_fd(open_writeexisting_at(vt_dir_fd.get(), "input"));
		if (input_fd.get() < 0) {
			die_errno(prog, envs, vt_dirname, "input");
		}
		append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, nullptr);
		vt.set_input_fd(input_fd.release());
		append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_ADD|EV_DISABLE, 0, 0, nullptr);
	}

	if (keyboard_state_filename) {
		keyboard_state.set_file_fd(open_readwriteexisting_at(AT_FDCWD, keyboard_state_filename));
		if (0 > keyboard_state.query_file_fd()) {
			die_errno(prog, envs, keyboard_state_filename);
		}
		append_event(ip, keyboard_state.query_file_fd(), EVFILT_VNODE, EV_ADD|EV_ENABLE|EV_CLEAR, NOTE_WRITE, 0, nullptr);
		leds = keyboard_state.query_LEDs();	// Synchronize with the initial pre-existing keyboard state file.
		update_LEDs();
	}
	if (mouse_state_filename) {
		mouse_state.set_file_fd(open_readwriteexisting_at(AT_FDCWD, mouse_state_filename));
		if (0 > mouse_state.query_file_fd()) {
			die_errno(prog, envs, mouse_state_filename);
		}
		mouse_state.query_buttons(mouse_buttons);	// Synchronize with the initial pre-existing mouse state file.
		if (has_pointer||(!display_only&&mouse_primary))
			append_event(ip, mouse_state.query_file_fd(), EVFILT_VNODE, EV_ADD|EV_ENABLE|EV_CLEAR, NOTE_WRITE, 0, nullptr);
		update_locators();
	}
}

void
Realizer::loop(
) {
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGWINCH, SIGUSR1, SIGUSR2, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, 0);
	append_event(ip, SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);

	const struct timespec immediate_timeout = { 0, 0 };

	while (true) {
		if (exit_signalled())
			break;
		handle_resize_event();
		handle_refresh_event();

		struct kevent p[128];
		const int rc(kevent(queue.get(), ip.data(), ip.size(), p, sizeof p/sizeof *p, has_idle_work() || has_update_pending() ? &immediate_timeout : nullptr));
		ip.clear();

		if (0 > rc) {
			if (EINTR == errno) continue;
			die_errno(prog, envs, "kevent");
		}

		if (0 == rc) {
			do_idle_work();
			handle_update_event();
			continue;
		}

		for (std::size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_VNODE:
					if (vt.query_buffer_fd() == static_cast<int>(e.ident))
						vt.set_reload_needed();
					if (keyboard_state.query_file_fd() == static_cast<int>(e.ident))
						update_LEDs();
					if (mouse_state.query_file_fd() == static_cast<int>(e.ident))
						update_locators();
					break;
				case EVFILT_SIGNAL:
					handle_signal(e.ident);
					break;
				case EVFILT_READ:
					if (STDIN_FILENO == e.ident)
						handle_stdin(e.data);
					break;
				case EVFILT_WRITE:
					if (vt.query_input_fd() == static_cast<int>(e.ident))
						vt.FlushMessages();
					break;
			}
		}

		if (vt.MessageAvailable()) {
			if (!vt.query_polling_for_write()) {
				append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_ENABLE, 0, 0, nullptr);
				vt.set_polling_for_write(true);
			}
		} else {
			if (vt.query_polling_for_write()) {
				append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_DISABLE, 0, 0, nullptr);
				vt.set_polling_for_write(false);
			}
		}
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
	TUIOptions oo;
	bool display_only(false);
	bool mouse_primary(false);

	try {
		popt::bool_definition no_default_colour_option('\0', "no-default-colour", "Do not attempt to use the realized-upon terminal's default colour.", oo.no_default_colour);
		popt::bool_definition cursor_application_mode_option('\0', "cursor-keypad-application-mode", "Set the cursor keypad to application mode instead of normal mode.", oo.cursor_application_mode);
		popt::bool_definition calculator_application_mode_option('\0', "calculator-keypad-application-mode", "Set the calculator keypad to application mode instead of normal mode.", oo.calculator_application_mode);
		popt::bool_definition no_alternate_screen_buffer_option('\0', "no-alternate-screen-buffer", "Prevent switching to the XTerm alternate screen buffer.", oo.no_alternate_screen_buffer);
		popt::bool_string_definition scnm_option('\0', "inversescreen", "Switch inverse screen mode on/off.", oo.scnm);
		popt::tui_level_definition tui_level_option('T', "tui-level", "Specify the level of TUI character set.");
		popt::definition * quirks_table[] = {
			&no_default_colour_option,
			&cursor_application_mode_option,
			&calculator_application_mode_option,
			&no_alternate_screen_buffer_option,
			&scnm_option,
			&tui_level_option,
		};
		popt::table_definition quirks_table_option(sizeof quirks_table/sizeof *quirks_table, quirks_table, "Terminal capability options");
		popt::unsigned_number_definition quadrant_option('\0', "quadrant", "number", "Position the terminal in quadrant 0, 1, 2, or 3.", oo.quadrant, 0);
		popt::bool_definition wrong_way_up_option('\0', "wrong-way-up", "Display from bottom to top.", oo.wrong_way_up);
		popt::bool_definition bold_as_colour_option('\0', "bold-as-colour", "Forcibly render boldface as a colour brightness change.", oo.bold_as_colour);
		popt::bool_definition faint_as_colour_option('\0', "faint-as-colour", "Forcibly render faint as a colour brightness change.", oo.faint_as_colour);
		popt::definition * display_table[] = {
			&quadrant_option,
			&wrong_way_up_option,
			&bold_as_colour_option,
			&faint_as_colour_option,
		};
		popt::table_definition display_table_option(sizeof display_table/sizeof *display_table, display_table, "Display options");
		popt::bool_definition display_only_option('\0', "display-only", "Only render the display; do not send input.", display_only);
		popt::bool_definition mouse_primary_option('\0', "mouse-primary", "Pass mouse position data to the terminal emulator.", mouse_primary);
		popt::definition * io_table[] = {
			&display_only_option,
			&mouse_primary_option,
		};
		popt::table_definition io_table_option(sizeof io_table/sizeof *io_table, io_table, "I/O options");
		popt::definition * top_table[] = {
			&io_table_option,
			&display_table_option,
			&quirks_table_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{virtual-terminal} {keyboard-state} {mouse-state}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
		oo.tui_level = tui_level_option.value() < oo.TUI_LEVELS ? tui_level_option.value() : oo.TUI_LEVELS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) {
		die_missing_argument(prog, envs, "virtual terminal directory name");
	}
	const char * vt_dirname(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		die_missing_argument(prog, envs, "keyboard state file name");
	}
	const char * keyboard_state_filename(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		die_missing_argument(prog, envs, "mouse state file name");
	}
	const char * mouse_state_filename(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	Realizer realizer(prog, envs, oo, display_only, mouse_primary);

	realizer.configure(vt_dirname, keyboard_state_filename, mouse_state_filename);

	realizer.loop();

	throw EXIT_SUCCESS;
}
