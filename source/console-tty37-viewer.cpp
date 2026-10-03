/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <vector>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include "kqueue_common.h"
#include "popt.h"
#include "utils.h"
#include "fdutils.h"
#include "ttyname.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "UTF8Decoder.h"
#include "ECMA48Decoder.h"
#include "CharacterCell.h"
#include "ControlCharacters.h"
#include "InputMessage.h"
#include "TerminalCapabilities.h"
#include "TUIDisplayCompositor.h"
#include "TUIOutputBase.h"
#include "TUIInputBase.h"
#include "TUIVIO.h"
#include "SignalManagement.h"
#include "ProcessEnvironment.h"
#include "u32string.h"
#include "UnicodeKeyboard.h"
#include "UnicodeClassification.h"

/* Full-screen TUI **********************************************************
// **************************************************************************
*/

namespace {

typedef std::vector<CharacterCell> cellvector;

struct DisplayItem {
	DisplayItem(CharacterCell::attribute_type a, const CharacterCell::ColourPair & c, const u32string & s) : attributes(a), colour(c), text(s) {}
	DisplayItem(CharacterCell::attribute_type a, const CharacterCell::ColourPair & c, const u32string::const_iterator & b, const u32string::const_iterator & e) : attributes(a), colour(c), text(b, e) {}
	CharacterCell::attribute_type attributes;
	CharacterCell::ColourPair colour;
	u32string text;
};

struct DisplayLine : public std::vector<DisplayItem> {
	unsigned long width() const;
};

struct DisplayDocument : public std::list<DisplayLine> {
	DisplayDocument() {}
	DisplayLine * append_line() {
		push_back(DisplayLine());
		return &back();
	}
};

struct DocumentParser :
	public ECMA48Decoder::ECMA48ControlSequenceSink
{
	DocumentParser(DisplayDocument & d, unsigned short c, bool s) : doc(d), current_line(nullptr), line_number(0UL), printline(), printhead(printline.begin()), columns(c), squeeze_empty_lines(s), last_was_empty(true), colour(CharacterCell::ColourPair::def), attributes(0U) {}
	void reset_line_number() { line_number = 0UL; }
	unsigned long long query_line_number() const { return line_number; }
protected:
	DisplayDocument & doc;
	DisplayLine * current_line;
	unsigned long long line_number;
	cellvector printline;
	cellvector::iterator printhead;
	const unsigned short columns;
	const bool squeeze_empty_lines;
	bool last_was_empty;
	CharacterCell::ColourPair colour;
	CharacterCell::attribute_type attributes;

	/// \name concrete implementation of the sink API called by the decoder
	/// @{
	virtual void PrintableCharacter(bool error, unsigned short shift_level, char32_t character);
	virtual void ControlCharacter(char32_t character);
	virtual void EscapeSequence(char32_t character, char32_t first_intermediate);
	virtual void ControlSequence(char32_t character, char32_t last_intermediate, char32_t first_private_parameter);
	virtual void ControlString(char32_t start_char, char32_t term_char);
	/// @}

	void off(CharacterCell::attribute_type a) { attributes &= ~a; }
	void on(CharacterCell::attribute_type a) { attributes |= a; }
	void append_item(CharacterCell::attribute_type a, const CharacterCell::ColourPair & c, const u32string & s);
};

struct UTF8ToECMA48 :
	public UTF8Decoder::UCS32CharacterSink
{
	UTF8ToECMA48(ECMA48Decoder::ECMA48ControlSequenceSink & sink, bool discard_control_strings) : decoder(sink, ECMA48Decoder::Options(discard_control_strings, true /* allow cancel */, true /* 7-bit extensions */, false /* no Interix shift */, false /* no rxvt function keys */, false /* no Linux function keys */)) {}
protected:
	ECMA48Decoder decoder;
	virtual void ProcessDecodedUTF8(char32_t character, bool decoder_error, bool overlong);
};

struct TUI :
	public TerminalCapabilities,
	public TUIOutputBase,
	public TUIInputBase
{
	TUI(ProcessEnvironment & e, DisplayDocument & m, TUIDisplayCompositor & comp, std::iostream & tty, int tty_fd, unsigned long c, const TUIOutputBase::Options &);
	~TUI();

	bool quit_flagged() const { return pending_quit_event; }
	bool immediate_update() const { return immediate_update_needed; }
	bool exit_signalled() const { return terminate_signalled||interrupt_signalled||hangup_signalled; }
	void handle_signal (int);
	void handle_control (int, int);
	void handle_update_event () { immediate_update_needed = false; TUIOutputBase::handle_update_event(); }

protected:
	TUIInputBase::WheelToKeyboard handler0;
	TUIInputBase::GamingToCursorKeypad handler1;
	TUIInputBase::EMACSToCursorKeypad handler2;
	TUIInputBase::CUAToExtendedKeys handler3;
	TUIInputBase::LessToExtendedKeys handler4;
	TUIInputBase::ConsumerKeysToExtendedKeys handler5;
	TUIInputBase::CalculatorToEditingKeypad handler6;
	class EventHandler : public TUIInputBase::EventHandler {
	public:
		EventHandler(TUI & i) : TUIInputBase::EventHandler(i), ui(i) {}
		~EventHandler();
	protected:
		virtual bool ExtendedKey(uint_fast16_t k, uint_fast8_t m);
		TUI & ui;
	} handler7;
	friend class EventHandler;
	sig_atomic_t terminate_signalled, interrupt_signalled, hangup_signalled, usr1_signalled, usr2_signalled;
	TUIVIO vio;
	bool pending_quit_event, immediate_update_needed;
	TUIDisplayCompositor::coordinate window_y, window_x;
	unsigned long columns;
	DisplayDocument & doc;
	std::size_t current_row, current_col;

	virtual void redraw_new();
	void set_refresh_and_immediate_update_needed () { immediate_update_needed = true; TUIOutputBase::set_refresh_needed(); }

private:
	char control_buffer[1U * 1024U];
};

}

inline
unsigned long
DisplayLine::width() const
{
	unsigned long w(0UL);
	for (const_iterator p(begin()), e(end()); e != p; ++p)
		w += p->text.length();
	return w;
}

void
DocumentParser::append_item(
	CharacterCell::attribute_type a,
	const CharacterCell::ColourPair & c,
	const u32string & s
) {
	if (!current_line) {
		current_line = doc.append_line();
		++line_number;
	}
	if (!s.empty())
		current_line->push_back(DisplayItem(a, c, s));
}

void
DocumentParser::PrintableCharacter(
	bool error,
	unsigned short shift_level,
	char32_t character
) {
	const CharacterCell::attribute_type a(attributes ^ (error || 1U != shift_level ? CharacterCell::INVERSE : 0U));
	if (printline.end() == printhead) {
		if (printline.size() >= columns)
			ControlCharacter(NEL);
		printhead = printline.insert(printhead, CharacterCell(SPC, static_cast<CharacterCell::attribute_type>(-1U), colour));
	}

	if (static_cast<CharacterCell::attribute_type>(-1U) == printhead->attributes) {
		printhead->attributes = a;
		printhead->character = character;
	} else
	if (UnicodeKeyboard::combine_unicode(character, printhead->character))
		printhead->attributes = a;
	else
	if (UnicodeKeyboard::combine_unicode(printhead->character, character)) {
		printhead->attributes = a;
		printhead->character = character;
	} else
	if (UnicodeKeyboard::combine_peculiar_non_combiners(character, printhead->character))
		printhead->attributes = a;
	else
	if (UnicodeKeyboard::combine_peculiar_non_combiners(printhead->character, character)) {
		printhead->attributes = a;
		printhead->character = character;
	} else
	if ('_' == printhead->character) {
		if ('_' == character) {
			printhead->attributes = attributes & ~CharacterCell::UNDERLINES;
			printhead->attributes |= CharacterCell::DOUBLE_UNDERLINE;
		} else
		{
			printhead->attributes = attributes & ~CharacterCell::UNDERLINES;
			printhead->attributes |= CharacterCell::SIMPLE_UNDERLINE;
			printhead->character = character;
		}
	} else
	if ('_' == character) {
		const CharacterCell::attribute_type old(printhead->attributes & CharacterCell::UNDERLINES);
		printhead->attributes = attributes & ~CharacterCell::UNDERLINES;
		switch (old) {
			default:
				printhead->attributes |= CharacterCell::SIMPLE_UNDERLINE;
				break;
			case CharacterCell::SIMPLE_UNDERLINE:
			case CharacterCell::DOUBLE_UNDERLINE:
				printhead->attributes |= CharacterCell::DOUBLE_UNDERLINE;
				break;
		}
	} else
	// Do this test, which has combinations that involve underscores, after eliminating the possibility of underscores.
	if (UnicodeKeyboard::combine_grotty_combiners(character, printhead->character))
		printhead->attributes = a;
	else
	if (character == printhead->character) {
		printhead->attributes = a|CharacterCell::BOLD|(printhead->attributes & CharacterCell::UNDERLINES);
	} else
	{
		printhead->attributes = a;
		printhead->character = character;
	}

	printhead->CharacterCell::ColourPair::operator=(colour);
	if (!UnicodeCategorization::IsMarkNonSpacing(printhead->character)
	&&  !UnicodeCategorization::IsMarkEnclosing(printhead->character)
	)
		++printhead;
}

void
DocumentParser::ControlCharacter(
	char32_t character
) {
	switch (character) {
		case TAB:
		{
			std::string::size_type c(printhead - printline.begin());
			do {
				PrintableCharacter(false, 1U, SPC);
				++c;
			} while (0 != (c % 8U));
			break;
		}
		case CR:
			printhead = printline.begin();
			break;
		case BS:
			if (printhead != printline.begin())
				--printhead;
			break;
		case LF: case VT: case FF: case NEL:
		{
			if (squeeze_empty_lines) {
				const bool is_empty(printline.empty());
				if (is_empty && last_was_empty) break;
				last_was_empty = is_empty;
			}
			u32string same;
			for (cellvector::const_iterator b(printline.begin()), e(printline.end()), p(b); ; ++p) {
				if (e == p) {
					if (b != p)
						append_item(b->attributes, *b, same);
					else
						append_item(0, CharacterCell::ColourPair::def, same);
					break;
				}
				if (b != p && (b->attributes != p->attributes || b->foreground != p->foreground || b->background != p->background)) {
					append_item(b->attributes, *b, same);
					same.clear();
					b = p;
				}
				same += p->character;
			}
			current_line = nullptr;
			printline.clear();
			printhead = printline.begin();
			break;
		}
	}
}

void
DocumentParser::EscapeSequence(
	char32_t /*character*/,
	char32_t /*first_intermediate*/
) {
}

void
DocumentParser::ControlSequence(
	char32_t character,
	char32_t last_intermediate,
	char32_t first_private_parameter
) {
	if ('m' == character && NUL == last_intermediate && NUL == first_private_parameter) {
		MinimumOneArg();
		for (std::size_t i(0U); i < QueryArgCount(); ++i) {
			const unsigned attr(GetArgZeroIfEmpty(i, 0U));
			switch (attr) {
				default:
					break;
				case 0U:
					off(-static_cast<CharacterCell::attribute_type>(1));
					colour.foreground = CharacterCell::colour_type::default_foreground;
					colour.background = CharacterCell::colour_type::default_background;
					break;
				case 1U:	on(CharacterCell::BOLD); break;
				case 2U:	on(CharacterCell::FAINT); break;
				case 3U:	on(CharacterCell::ITALIC); break;
				case 4U:
				{
					const unsigned style(GetArgZeroIfEmpty(i, 1U));
					off(CharacterCell::UNDERLINES);
					switch (style) {
						case 0U:
						case 1U:	on(CharacterCell::SIMPLE_UNDERLINE); break;
						case 2U:	on(CharacterCell::DOUBLE_UNDERLINE); break;
						default:
						case 3U:	on(CharacterCell::CURLY_UNDERLINE); break;
						case 4U:	on(CharacterCell::DOTTED_UNDERLINE); break;
						case 5U:	on(CharacterCell::DASHED_UNDERLINE); break;
						case 6U:	on(CharacterCell::LDASHED_UNDERLINE); break;
						case 7U:	on(CharacterCell::LLDASHED_UNDERLINE); break;
						case 8U:	on(CharacterCell::LDOTTED_UNDERLINE); break;
						case 9U:	on(CharacterCell::LLDOTTED_UNDERLINE); break;
						case 10U:	on(CharacterCell::LCURLY_UNDERLINE); break;
					}
					break;
				}
				case 5U:	on(CharacterCell::BLINK); break;
				case 7U:	on(CharacterCell::INVERSE); break;
				case 8U:	on(CharacterCell::INVISIBLE); break;
				case 9U:	on(CharacterCell::STRIKETHROUGH); break;
				case 21U:	off(CharacterCell::UNDERLINES); on(CharacterCell::DOUBLE_UNDERLINE); break;
				case 22U:	off(CharacterCell::BOLD|CharacterCell::FAINT); break;
				case 23U:	off(CharacterCell::ITALIC); break;
				case 24U:	off(CharacterCell::UNDERLINES); break;
				case 25U:	off(CharacterCell::BLINK); break;
				case 27U:	off(CharacterCell::INVERSE); break;
				case 28U:	off(CharacterCell::INVISIBLE); break;
				case 29U:	off(CharacterCell::STRIKETHROUGH); break;
				case 30U: case 31U: case 32U: case 33U:
				case 34U: case 35U: case 36U: case 37U:
						colour.foreground = Map16Colour(attr -  30U); break;
				case 39U:	colour.foreground = ColourPair::colour_type::default_foreground; break;
				case 40U: case 41U: case 42U: case 43U:
				case 44U: case 45U: case 46U: case 47U:
						colour.background = Map16Colour(attr -  40U); break;
				case 49U:	colour.background = ColourPair::colour_type::default_background; break;
				case 51U:	on(CharacterCell::FRAME); break;
				case 52U:	on(CharacterCell::ENCIRCLE); break;
				case 53U:	on(CharacterCell::OVERLINE); break;
				case 54U:	off(CharacterCell::FRAME|CharacterCell::ENCIRCLE); break;
				case 55U:	off(CharacterCell::OVERLINE); break;
				case 90U: case 91U: case 92U: case 93U:
				case 94U: case 95U: case 96U: case 97U:
						colour.foreground = Map16Colour(attr -  90U + 8U); break;
				case 100U: case 101U: case 102U: case 103U:
				case 104U: case 105U: case 106U: case 107U:
						colour.background = Map16Colour(attr - 100U + 8U); break;
				case 38U: case 48U:
				{
					if (HasNoSubArgsFrom(i))
						CollapseArgsToSubArgs(i);
					ColourPair::colour_type & ground(38U == attr ? colour.foreground : colour.background);
					if (5U == GetArgZeroIfEmpty(i, 1U)) {
						ground = Map256Colour(GetArgZeroIfEmpty(i, 2U) % 256U);
					} else
					if (2U == GetArgZeroIfEmpty(i, 1U)) {
						// ISO 8613-6/ITU T.416 section 13.1.8 has a colour space in sub-parameter 2, which is not implemented.
						// Parameter 6 has no meaning per the standard, and parameters 7 and 8 (tolerance value and space) are not implemented.
						if (5U != QuerySubArgCount(i))
							ground = MapTrueColour(GetArgZeroIfEmpty(i, 3U) % 256U,GetArgZeroIfEmpty(i, 4U) % 256U,GetArgZeroIfEmpty(i, 5U) % 256U);
						else
							// A common error is to omit the colour space, which we detect heuristically by a too-short-by-one sequence length.
							ground = MapTrueColour(GetArgZeroIfEmpty(i, 2U) % 256U,GetArgZeroIfEmpty(i, 3U) % 256U,GetArgZeroIfEmpty(i, 4U) % 256U);
					}
					break;
				}
				// ECMA-48 defines these as font changes.  We don't provide that.
				// The Linux console defines them as something else.  We don't provide that, either.
				case 10U:	break;
				case 11U:	break;
			}
		}
	}
}

void
DocumentParser::ControlString(
	char32_t /*start_char*/,
	char32_t /*term_char*/
) {
}

void
UTF8ToECMA48::ProcessDecodedUTF8 (
	char32_t character,
	bool decoder_error,
	bool overlong
) {
	decoder.Process(character, decoder_error, overlong);
}

TUI::TUI(
	ProcessEnvironment & e,
	DisplayDocument & d,
	TUIDisplayCompositor & comp,
	std::iostream & tty,
	int tty_fd,
	unsigned long c,
	const TUIOutputBase::Options & options
) :
	TerminalCapabilities(e),
	TUIOutputBase(*this, tty, tty_fd, options, comp),
	TUIInputBase(static_cast<const TerminalCapabilities &>(*this), tty, tty_fd),
	handler0(*this),
	handler1(*this),
	handler2(*this),
	handler3(*this),
	handler4(*this),
	handler5(*this),
	handler6(*this),
	handler7(*this),
	terminate_signalled(false),
	interrupt_signalled(false),
	hangup_signalled(false),
	usr1_signalled(false),
	usr2_signalled(false),
	vio(comp),
	pending_quit_event(false),
	immediate_update_needed(true),
	window_y(0U),
	window_x(0U),
	columns(c),
	doc(d),
	current_row(0U),
	current_col(0U)
{
	comp.set_screen_flags(options.scnm ? ScreenFlags::INVERTED : 0U);
}

TUI::~TUI(
) {
}

void
TUI::handle_control (
	int fd,
	int n		///< number of characters available; can be <= 0 erroneously
) {
	for (;;) {
		int l(read(fd, control_buffer, sizeof control_buffer));
		if (0 >= l) break;
		HandleInput(control_buffer, l);
		if (l >= n) break;
		n -= l;
	}
	BreakInput();
}

void
TUI::handle_signal (
	int signo
) {
	switch (signo) {
		case SIGWINCH:	set_resized(); break;
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
		case SIGUSR1:	usr1_signalled = true; break;
		case SIGUSR2:	usr2_signalled = true; break;
		case SIGTSTP:	tstp_signal(); break;
		case SIGCONT:	continued(); break;
	}
}

void
TUI::redraw_new (
) {
	TUIDisplayCompositor::coordinate y(current_row), x(current_col);
	// The window includes the cursor position.
	if (window_y > y) {
		optimize_scroll_up(window_y - y);
		window_y = y;
	} else
	if (window_y + c.query_h() <= y) {
		optimize_scroll_down(y - c.query_h() - window_y + 1);
		window_y = y - c.query_h() + 1;
	}
	if (window_x > x) {
		window_x = x;
	} else
	if (window_x + c.query_w() <= x) {
		window_x = x - c.query_w() + 1;
	}

	vio.CLSToSpace(ColourPair::white_on_black);

	std::size_t nr(0U);
	for (DisplayDocument::const_iterator rb(doc.begin()), re(doc.end()), ri(rb); re != ri; ++ri, ++nr) {
		if (nr < window_y) continue;
		long row(nr - window_y);
		if (row >= c.query_h()) break;
		const DisplayLine & r(*ri);
		long col(-window_x);
		for (DisplayLine::const_iterator fb(r.begin()), fe(r.end()), fi(fb); fe != fi; ++fi) {
			const DisplayItem & f(*fi);
			vio.PrintCharStrAttr(row, col, f.attributes, f.colour, f.text.c_str(), f.text.length());
		}
		if (col < c.query_w())
			vio.PrintNCharsAttr(row, col, 0U, ColourPair::def, SPC, c.query_w() - col);
	}

	c.move_cursor(y - window_y, x - window_x);
	c.set_cursor_state(CursorSprite::BLINK|CursorSprite::VISIBLE, CursorSprite::BOX);
}

bool
TUI::EventHandler::ExtendedKey(
	uint_fast16_t k,
	uint_fast8_t m
) {
	switch (k) {
		// The viewer does not have an execute command.
		case EXTENDED_KEY_PAD_ENTER:
		case EXTENDED_KEY_EXECUTE:
		default:	return TUIInputBase::EventHandler::ExtendedKey(k, m);
		case EXTENDED_KEY_LEFT_ARROW:
			if (ui.current_col > 0) { --ui.current_col; ui.set_refresh_and_immediate_update_needed(); }
			return true;
		case EXTENDED_KEY_RIGHT_ARROW:
			if (ui.current_col + 1 < ui.columns) { ++ui.current_col; ui.set_refresh_and_immediate_update_needed(); }
			return true;
		case EXTENDED_KEY_DOWN_ARROW:
			if (ui.current_row + 1 < ui.doc.size()) { ++ui.current_row; ui.set_refresh_and_immediate_update_needed(); }
			return true;
		case EXTENDED_KEY_UP_ARROW:
			if (ui.current_row > 0) { --ui.current_row; ui.set_refresh_and_immediate_update_needed(); }
			return true;
		case EXTENDED_KEY_END:
			if (m & INPUT_MODIFIER_CONTROL) {
				if (ui.current_col + 1 != ui.columns) {
					ui.current_col = ui.columns - 1;
					ui.set_refresh_and_immediate_update_needed();
				}
			} else
			if (std::size_t s = ui.doc.size()) {
				if (ui.current_row + 1 != s) { ui.current_row = s - 1; ui.set_refresh_and_immediate_update_needed(); }
			} else
			if (ui.current_row != 0) {
				ui.current_row = 0U;
				ui.set_refresh_and_immediate_update_needed();
			}
			return true;
		case EXTENDED_KEY_HOME:
			if (m & INPUT_MODIFIER_CONTROL) {
				if (ui.current_col != 0) { ui.current_col = 0; ui.set_refresh_and_immediate_update_needed(); }
			} else
			if (ui.current_row != 0) {
				ui.current_row = 0U;
				ui.set_refresh_and_immediate_update_needed();
			}
			return true;
		case EXTENDED_KEY_PAGE_DOWN:
			if (ui.doc.size() && ui.current_row + 1 < ui.doc.size()) {
				unsigned n(ui.c.query_h() - 1U);
				if (ui.current_row + n < ui.doc.size())
					ui.current_row += n;
				else
					ui.current_row = ui.doc.size() - 1;
				ui.set_refresh_and_immediate_update_needed();
			}
			return true;
		case EXTENDED_KEY_PAGE_UP:
			if (ui.current_row > 0) {
				unsigned n(ui.c.query_h() - 1U);
				if (ui.current_row > n)
					ui.current_row -= n;
				else
					ui.current_row = 0;
				ui.set_refresh_and_immediate_update_needed();
			}
			return true;
		case EXTENDED_KEY_BACKSPACE:
			if (ui.current_col > 0) {
				--ui.current_col;
				ui.set_refresh_and_immediate_update_needed();
			} else
			if (ui.current_row > 0) {
				ui.current_col = ui.columns - 1;
				--ui.current_row;
				ui.set_refresh_and_immediate_update_needed();
			}
			return true;
		case EXTENDED_KEY_PAD_SPACE:
			if (ui.current_col + 1 < ui.columns) {
				++ui.current_col;
				ui.set_refresh_and_immediate_update_needed();
				return true;
			} else
				[[clang::fallthrough]];
		case EXTENDED_KEY_APP_RETURN:
		case EXTENDED_KEY_RETURN_OR_ENTER:
			if (ui.current_row + 1 < ui.doc.size()) {
				if (!(m & INPUT_MODIFIER_CONTROL)) ui.current_col = 0;
				++ui.current_row;
				ui.set_refresh_and_immediate_update_needed();
			}
			return true;
		case EXTENDED_KEY_CANCEL:
		case EXTENDED_KEY_CLOSE:
		case EXTENDED_KEY_EXIT:
			ui.pending_quit_event = true;
			return true;
		case EXTENDED_KEY_STOP:
			ui.suspend_self();
			return true;
		case EXTENDED_KEY_REFRESH:
			ui.invalidate_cur();
			ui.set_update_needed();
			return true;
	}
}

// Seat of the class
TUI::EventHandler::~EventHandler() {}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_tty37_viewer [[gnu::noreturn]]
(
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	TUIOutputBase::Options options;
	bool squeeze_empty_lines(false), discard_control_strings(false);
	try {
		popt::bool_definition cursor_application_mode_option('\0', "cursor-keypad-application-mode", "Set the cursor keypad to application mode instead of normal mode.", options.cursor_application_mode);
		popt::bool_definition calculator_application_mode_option('\0', "calculator-keypad-application-mode", "Set the calculator keypad to application mode instead of normal mode.", options.calculator_application_mode);
		popt::bool_definition no_alternate_screen_buffer_option('\0', "no-alternate-screen-buffer", "Prevent switching to the XTerm alternate screen buffer.", options.no_alternate_screen_buffer);
		popt::bool_string_definition scnm_option('\0', "inversescreen", "Switch inverse screen mode on/off.", options.scnm);
		popt::tui_level_definition tui_level_option('T', "tui-level", "Specify the level of TUI character set.");
		popt::definition * tui_table[] = {
			&cursor_application_mode_option,
			&calculator_application_mode_option,
			&no_alternate_screen_buffer_option,
			&scnm_option,
			&tui_level_option,
		};
		popt::table_definition tui_table_option(sizeof tui_table/sizeof *tui_table, tui_table, "Terminal quirks options");
		popt::bool_definition squeeze_empty_lines_option('s', "squeeze-empty-lines", "Squeeze successive empty lines into 1 line.", squeeze_empty_lines);
		popt::bool_definition discard_control_strings_option('\0', "discard-control-strings", "Recognize ECMA48 control strings.", discard_control_strings);
		popt::definition * top_table[] = {
			&squeeze_empty_lines_option,
			&discard_control_strings_option,
			&tui_table_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{file(s)...}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		options.tui_level = tui_level_option.value() < options.TUI_LEVELS ? tui_level_option.value() : options.TUI_LEVELS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	const char * tty(envs.query("TTY"));
	if (!tty) tty = "/dev/tty";
	std::fstream control(tty);
	if (!control.is_open()) {
		die_errno(prog, envs, tty);
	}
#if __cpp_lib_fstream_native_handle
	const int control_fd(control.rdbuf()->native_handle());
#else
	const int control_fd(open_readwriteexisting_at(AT_FDCWD, tty));
	if (0 > control_fd) {
		die_errno(prog, envs, tty);
	}
#endif

	const unsigned long columns(get_columns(envs, control_fd));

	DisplayDocument doc;

	{
		DocumentParser parser(doc, columns, squeeze_empty_lines);
		UTF8ToECMA48 utf8toecma48(parser, discard_control_strings);
		UTF8Decoder utf8decoder(utf8toecma48);

		if (args.empty()) {
			if (isatty(STDIN_FILENO)) {
				struct stat s0, st;

				if (0 <= fstat(STDIN_FILENO, &s0)
				&&  0 <= fstat(control_fd, &st)
				&&  S_ISCHR(s0.st_mode)
				&&  (s0.st_rdev == st.st_rdev)
				) {
					die_invalid(prog, envs, tty, "The controlling terminal cannot be both standard input and control input.");
				}
			}
			try {
				for (int c = std::fgetc(stdin); EOF != c; c = std::fgetc(stdin))
					utf8decoder.Process(c);
				if (std::ferror(stdin)) throw std::strerror(errno);
			} catch (const char * s) {
				die_parser_error(prog, envs, "<stdin>", parser.query_line_number(), s);
			}
		} else
		{
			while (!args.empty()) {
				parser.reset_line_number();
				const char * file(args.front());
				args.erase(args.begin());
				FileStar f(std::fopen(file, "r"));
				try {
					if (!f) throw std::strerror(errno);
					for (int c = std::fgetc(f); EOF != c; c = std::fgetc(f))
						utf8decoder.Process(c);
					if (std::ferror(f)) throw std::strerror(errno);
				} catch (const char * s) {
					die_parser_error(prog, envs, file, parser.query_line_number(), s);
				}
			}
		}
	}

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		die_errno(prog, envs, "kqueue");
	}
	std::vector<struct kevent> ip;

	append_event(ip, control_fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, SIGWINCH, SIGTSTP, SIGCONT, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, 0);
	append_event(ip, SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGCONT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);

	TUIDisplayCompositor compositor(false /* no software cursor */, 24, 80);
	TUI ui(envs, doc, compositor, control, control_fd, columns, options);

	// How long to wait with updates pending.
	const struct timespec short_timeout = { 0, 100000000L };
	const struct timespec immediate_timeout = { 0, 0 };

	std::vector<struct kevent> p(4);
	while (true) {
		if (ui.exit_signalled() || ui.quit_flagged())
			break;
		ui.handle_resize_event();
		ui.handle_refresh_event();

		const struct timespec * timeout(ui.immediate_update() ? &immediate_timeout : ui.has_update_pending() ? &short_timeout : nullptr);
		const int rc(kevent(queue.get(), ip.data(), ip.size(), p.data(), p.size(), timeout));
		ip.clear();

		if (0 > rc) {
			if (EINTR == errno) continue;
#if defined(__LINUX__) || defined(__linux__)
			if (EINVAL == errno) continue;	// This works around a Linux bug when an inotify queue overflows.
			if (0 == errno) continue;	// This works around another Linux bug.
#endif
			die_errno(prog, envs, "kevent");
		}

		if (0 == rc) {
			ui.handle_update_event();
			continue;
		}

		for (std::size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_SIGNAL:
					ui.handle_signal(e.ident);
					break;
				case EVFILT_READ:
				{
					const int fd(static_cast<int>(e.ident));
					if (control_fd == fd) {
						ui.handle_control(fd, e.data);
					}
					break;
				}
			}
		}
	}

	throw EXIT_SUCCESS;
}
