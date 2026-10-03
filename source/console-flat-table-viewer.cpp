/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#define _BSD_SOURCE 1
#include <map>
#include <vector>
#include <limits>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "kqueue_common.h"
#include "popt.h"
#include "utils.h"
#include "fdutils.h"
#include "ttyutils.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
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
#include "UTF8Decoder.h"
#include "VisDecoder.h"

/* Full-screen TUI **********************************************************
// **************************************************************************
*/

namespace {

typedef std::string Field;
typedef std::vector<Field> Record;
typedef std::vector<Record> Table;

struct ParserCharacters {
	ParserCharacters() :
		unit_separator_0(' '),
		unit_separator_1(TAB),
		record_separator(LF),
		group_separator(EOF),
		file_separator(FF),
		eol_comment('#'),
		do_unvis(true),
		allow_empty_field(false),
		allow_empty_record(false)
	{
	}

	int unit_separator_0, unit_separator_1, record_separator, group_separator, file_separator, eol_comment;
	bool do_unvis;
	bool allow_empty_field, allow_empty_record;

	bool IsUnitSeparator(unsigned char c) { return unit_separator_0 == c || unit_separator_1 == c; }
	bool IsRecordSeparator(unsigned char c) { return record_separator == c; }
	bool IsGroupSeparator(unsigned char c) { return group_separator == c; }
	bool IsFileSeparator(unsigned char c) { return file_separator == c; }
	bool IsComment(unsigned char c) { return eol_comment == c; }
	bool AllowEmptyFields() { return allow_empty_field; }
	bool AllowEmptyRecords() { return allow_empty_record; }
	bool DecodingVis() { return do_unvis; }

};

struct UTF8DecoderHelper :
	public UTF8Decoder,
	public UTF8Decoder::UCS32CharacterSink
{
	UTF8DecoderHelper() : UTF8Decoder(*static_cast<UTF8Decoder::UCS32CharacterSink *>(this)), str() {}
	virtual void ProcessDecodedUTF8(char32_t character, bool decoder_error, bool /*overlong*/)
	{
		if (!decoder_error)
			str.push_back(character);
	}
	void Process(const Field & f)
	{
		for (std::string::const_iterator p(f.begin()), e(f.end()); p != e; ++p)
			UTF8Decoder::Process(*p);
	}
	std::basic_string<CharacterCell::character_type> str;
};

struct TUI :
	public ParserCharacters,
	public TerminalCapabilities,
	public TUIOutputBase,
	public TUIInputBase
{
	TUI(ProcessEnvironment & e, Table & m, TUIDisplayCompositor & c, std::iostream & tty, int ttyfd, unsigned long header_count, const ParserCharacters &, const CharacterCell::colour_type &, const CharacterCell::colour_type &, const TUIOutputBase::Options &);
	~TUI();

	bool quit_flagged() const { return pending_quit_event; }
	bool immediate_update() const { return immediate_update_needed; }
	bool exit_signalled() const { return terminate_signalled||interrupt_signalled||hangup_signalled; }
	void handle_signal (int);
	void handle_control (int, int);
	void handle_data (int, int);
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
	const ColourPair body, heading;
	const unsigned long header_count;
	Table & table;
	struct ColumnInfo {
		ColumnInfo() : manual(0U), automatic(0U), override(false) {}
		unsigned width() const { return override ? manual : automatic; }
		void set_min_auto(unsigned a) { if (a > automatic) automatic = a; }
		void clear_auto() { automatic = 0U; }
		void initialize_manual() { if (!override) { manual = automatic; override = true; } }
		bool increase_manual() { initialize_manual(); if (manual < automatic) { ++manual; return true; } else return false; }
		bool decrease_manual() { initialize_manual(); if (manual > 0U) { --manual; return true; } else return false; }
	protected:
		unsigned manual, automatic;
		bool override;
	};
	std::vector<ColumnInfo> info;
	enum { START, FIELD, COMMENT } state;
	std::size_t data_nr, data_nf, current_nr, current_nf;
	VisDecoder unvisdec;

	virtual void redraw_new();
	void set_refresh_and_immediate_update_needed () { immediate_update_needed = true; TUIOutputBase::set_refresh_needed(); }

	void HandleData (const char *, std::size_t);
private:
	char data_buffer[64U * 1024U];
	char control_buffer[1U * 1024U];
};

inline
bool
is_numeric (
	const std::string & s
) {
	for (std::string::const_iterator p(s.begin()), e(s.end()); p != e; ++p) {
		if (!std::isdigit(*p) && '.' != *p && ',' != *p) return false;
	}
	return true;
}

}

TUI::TUI(
	ProcessEnvironment & e,
	Table & m,
	TUIDisplayCompositor & comp,
	std::iostream & tty,
	int tty_fd,
	unsigned long c,
	const ParserCharacters & p,
	const CharacterCell::colour_type & h_colour,
	const CharacterCell::colour_type & b_colour,
	const TUIOutputBase::Options & options
) :
	ParserCharacters(p),
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
	body(ColourPair(b_colour, Map256Colour(COLOUR_BLACK))),
	heading(ColourPair(h_colour, Map256Colour(COLOUR_BLACK))),
	header_count(c),
	table(m),
	state(START),
	data_nr(0U),
	data_nf(0U),
	current_nr(0U),
	current_nf(0U),
	unvisdec()
{
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
		if (n <= 0) break;
	}
	BreakInput();
}

void
TUI::handle_data (
	int fd,
	int n		///< number of characters available; can be <= 0 erroneously
) {
	do {
		int l(read(fd, data_buffer, sizeof data_buffer));
		if (0 >= l) break;
		HandleData(data_buffer, l);
		if (l >= n) break;
		n -= l;
	} while (n > 0);
}

void
TUI::HandleData (
	const char * buf,
	std::size_t len
) {
	while (len) {
		unsigned char c(*buf++);
		--len;
		if (IsFileSeparator(c)) {
			table.clear();
			// We keep the already determined columns, and just clear the automatically determined widths.
			for (std::vector<ColumnInfo>::iterator b(info.begin()), e(info.end()), p(b); e != p; ++p)
				p->clear_auto();
			data_nr = data_nf = 0;
			state = START;
			set_refresh_needed();
		} else
		switch (state) {
			case START:
				if (IsComment(c)) {
					state = COMMENT;
					break;
				} else
				if (IsGroupSeparator(c) || IsRecordSeparator(c)) {
					if (data_nf || AllowEmptyRecords()) ++data_nr;
					data_nf = 0;
					break;
				} else
				if (IsUnitSeparator(c)) {
					if (AllowEmptyFields()) ++data_nf;
					break;
				} else
				{
					state = FIELD;
					if (DecodingVis())
						unvisdec.Begin();
				}
				[[clang::fallthrough]];
			case FIELD:
			{
				if (table.size() < data_nr + 1) table.resize(data_nr + 1);
				Record & record(table[data_nr]);
				if (record.size() < data_nf + 1) record.resize(data_nf + 1);
				Field & field(record[data_nf]);
				if (info.size() < data_nf + 1) info.resize(data_nf + 1);
				ColumnInfo & ci(info[data_nf]);
				bool pushed(false);

				do {
					if (IsGroupSeparator(c) || IsRecordSeparator(c) || IsUnitSeparator(c)) {
						if (DecodingVis()) {
							const std::string s(unvisdec.End());
							if (s.length()) {
								field += s;
								pushed = true;
							}
						}
						if (IsUnitSeparator(c)) {
							++data_nf;
						} else {
							++data_nr;
							data_nf = 0;
						}
						state = START;
						break;
					}
					if (DecodingVis()) {
						const std::string s(unvisdec.Normal(static_cast<char>(c)));
						if (s.length()) {
							field += s;
							pushed = true;
						}
					} else
					{
						field.push_back(static_cast<char>(c));
						pushed = true;
					}
					if (!len) break;
					c = *buf++;
					--len;
				} while (true);
				if (pushed) {
					UTF8DecoderHelper h;
					h.Process(field);
					ci.set_min_auto(h.str.length());
					set_refresh_needed();
				}
				break;
			}
			case COMMENT:
				do {
					if (IsRecordSeparator(c) || IsGroupSeparator(c) || IsFileSeparator(c)) {
						if (table.size() > data_nr) data_nr = table.size();
						data_nf = 0;
						state = START;
						break;
					}
					if (!len) break;
					c = *buf++;
					--len;
				} while (true);
				break;
		}
	}
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
	TUIDisplayCompositor::coordinate y(current_nr), x(0);

	// The window includes the cursor position.
	{
		if (window_y > y) {
			optimize_scroll_up(window_y - y);
			window_y = y;
		} else
		if (window_y + c.query_h() <= y) {
			optimize_scroll_down(y - c.query_h() - window_y + 1);
			window_y = y - c.query_h() + 1;
		}
		std::size_t nf(0U);
		for (std::vector<ColumnInfo>::iterator b(info.begin()), e(info.end()), p(b); e != p && nf < current_nf; ++p, ++nf)
			x += p->width() + 1;
		if (window_x > x) {
			window_x = x;
		} else
		if (window_x + c.query_w() <= x) {
			window_x = x - c.query_w() + 1;
		}
		if (nf) {
			const ColumnInfo & ci(info[nf]);
			unsigned width(ci.width());
			if (width > c.query_w()) width = c.query_w();
			if (x + width > window_x + c.query_w())
				window_x += (x + width) - (window_x + c.query_w());
		}
	}

	vio.CLSToSpace(ColourPair::def);

	std::size_t nr(0U);
	for (Table::const_iterator rb(table.begin()), re(table.end()), ri(rb); re != ri; ++ri, ++nr) {
		const Record & r(*ri);
		const bool is_header(nr < header_count);
		long row(is_header ? nr : nr - window_y);
		if (row < (is_header ? 0 : static_cast<long>(header_count))) continue;
		if (row >= c.query_h()) break;
		const bool is_current_row(nr == current_nr);
		const ColourPair colour(is_header ? heading : body);
		std::size_t nf(0U);
		long col(-window_x);
		for (Record::const_iterator fb(r.begin()), fe(r.end()), fi(fb); fe != fi; ++fi, ++nf) {
			const Field & f(*fi);
			const ColumnInfo & ci(info[nf]);
			const bool is_current_col(nf == current_nf);
			const bool ljust(is_header || !is_numeric(f));
			const unsigned width(ci.width());

			const CharacterCell::attribute_type attr(
				is_header && (is_current_col || is_current_row) ? CharacterCell::INVERSE|CharacterCell::BOLD :
				is_header || is_current_col || is_current_row ? CharacterCell::INVERSE :
				0U
			);
			UTF8DecoderHelper h;
			h.Process(f);
			unsigned l(h.str.length());
			if (l > width) l = width;

			if (!ljust && width > l)
				vio.PrintNCharsAttr(row, col, attr, colour, ' ', width - l);
			vio.PrintCharStrAttr(row, col, attr, colour, h.str.data(), l);
			if (ljust && width > l)
				vio.PrintNCharsAttr(row, col, attr, colour, ' ', width - l);
			if (fi + 1 != fe)
				vio.PrintNCharsAttr(row, col, attr, colour, ' ', 1U);
		}
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
		default:	return TUIInputBase::EventHandler::ExtendedKey(k, m);
		case EXTENDED_KEY_LEFT_ARROW:
			if (ui.current_nf > 0) { --ui.current_nf; ui.set_refresh_and_immediate_update_needed(); }
			return true;
		case EXTENDED_KEY_RIGHT_ARROW:
			if (ui.current_nf + 1 < ui.info.size()) { ++ui.current_nf; ui.set_refresh_and_immediate_update_needed(); }
			return true;
		case EXTENDED_KEY_DOWN_ARROW:
			if (ui.current_nr + 1 < ui.table.size()) { ++ui.current_nr; ui.set_refresh_and_immediate_update_needed(); }
			return true;
		case EXTENDED_KEY_UP_ARROW:
			if (ui.current_nr > 0) { --ui.current_nr; ui.set_refresh_and_immediate_update_needed(); }
			return true;
		case EXTENDED_KEY_END:
			if (std::size_t s = ui.table.size()) {
				if (ui.current_nr + 1 != s) { ui.current_nr = s - 1; ui.set_refresh_and_immediate_update_needed(); }
				return true;
			} else
				[[clang::fallthrough]];
		case EXTENDED_KEY_HOME:
			if (ui.current_nr != 0) { ui.current_nr = 0U; ui.set_refresh_and_immediate_update_needed(); }
			return true;
		case EXTENDED_KEY_PAGE_DOWN:
			if (ui.table.size() && ui.current_nr + 1 < ui.table.size()) {
				unsigned n(ui.c.query_h() - 1U);
				if (ui.current_nr + n < ui.table.size())
					ui.current_nr += n;
				else
					ui.current_nr = ui.table.size() - 1;
				ui.set_refresh_and_immediate_update_needed();
			}
			return true;
		case EXTENDED_KEY_PAGE_UP:
			if (ui.current_nr > 0) {
				unsigned n(ui.c.query_h() - 1U);
				if (ui.current_nr > n)
					ui.current_nr -= n;
				else
					ui.current_nr = 0;
				ui.set_refresh_and_immediate_update_needed();
			}
			return true;
		case EXTENDED_KEY_PAD_PLUS:
		{
			if (ui.current_nf < ui.info.size()) {
				ColumnInfo & ci(ui.info[ui.current_nf]);
				if (ci.increase_manual()) ui.set_refresh_and_immediate_update_needed();
			}
			return true;
		}
		case EXTENDED_KEY_PAD_MINUS:
		{
			if (ui.current_nf < ui.info.size()) {
				ColumnInfo & ci(ui.info[ui.current_nf]);
				if (ci.decrease_manual()) ui.set_refresh_and_immediate_update_needed();
			}
			return true;
		}
		case EXTENDED_KEY_CANCEL:
		case EXTENDED_KEY_CLOSE:
		case EXTENDED_KEY_EXIT:
		case EXTENDED_KEY_PAD_CLEAR:
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

/* Command-line options *****************************************************
// **************************************************************************
*/

namespace {
	struct format_definition : public popt::compound_1arg_named_definition, public ParserCharacters {
	public:
		format_definition(char s, const char * l, const char * d) : popt::compound_1arg_named_definition(s, l, a, d) {}
		virtual ~format_definition();
	protected:
		static const char a[];
		virtual void action(popt::processor &, const char *);
	};
	struct colour_definition : public popt::compound_1arg_named_definition {
	public:
		colour_definition(char s, const char * l, const char * d, CharacterCell::colour_type & v) : popt::compound_1arg_named_definition(s, l, a, d), value(v) {}
		virtual ~colour_definition();
	protected:
                static const char a[];
		virtual void action(popt::processor &, const char *);
		CharacterCell::colour_type & value;
	};

	struct colourname {
		unsigned index;
		const char * name;
	} const colournames[] = {
		{	 0U,	"black"		},
		{	 1U,	"red"		},
		{	 2U,	"green"		},
		{	 3U,	"yellow"	},
		{	 3U,	"brown"		},
		{	 4U,	"blue"		},
		{	 5U,	"magenta"	},
		{	 6U,	"cyan"		},
		{	 7U,	"white"		},
		{	 1U,	"dark red"	},
		{	 2U,	"dark green"	},
		{	 3U,	"dark yellow"	},
		{	 4U,	"dark blue"	},
		{	 5U,	"dark magenta"	},
		{	 6U,	"dark cyan"	},
		{	 7U,	"dark white"	},
		{	 7U,	"bright grey"	},
		{	 8U,	"grey"		},
		{	 8U,	"dark grey"	},
		{	 8U,	"bright black"	},
		{	 9U,	"bright red"	},
		{	10U,	"bright green"	},
		{	11U,	"bright yellow"	},
		{	12U,	"bright blue"	},
		{	13U,	"bright magenta"},
		{	14U,	"bright cyan"	},
		{	15U,	"bright white"	},
	};
}

const char format_definition::a[] = "mode";
format_definition::~format_definition() {}
void format_definition::action(popt::processor & /*proc*/, const char * text)
{
	if (0 == std::strcmp(text, "ascii")) {
		unit_separator_0 = US;
		unit_separator_1 = EOF;
		record_separator = RS;
		group_separator = GS;
		file_separator = FS;
		eol_comment = EOF;
		do_unvis = false;
		allow_empty_field = true;
		allow_empty_record = true;
	} else
	if (0 == std::strcmp(text, "colon")) {
		unit_separator_0 = ':';
		unit_separator_1 = EOF;
		record_separator = LF;
		group_separator = EOF;
		file_separator = EOF;
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
		eol_comment = '#';
#else
		eol_comment = EOF;
#endif
		do_unvis = false;
		allow_empty_field = true;
		allow_empty_record = false;
	} else
	if (0 == std::strcmp(text, "space")) {
		unit_separator_0 = ' ';
		unit_separator_1 = TAB;
		record_separator = LF;
		group_separator = EOF;
		file_separator = FF;
		eol_comment = '#';
		do_unvis = true;
		allow_empty_field = false;
		allow_empty_record = false;
	} else
	if (0 == std::strcmp(text, "tabbed")) {
		unit_separator_0 = TAB;
		unit_separator_1 = EOF;
		record_separator = LF;
		group_separator = EOF;
		file_separator = FF;
		eol_comment = '#';
		do_unvis = true;
		allow_empty_field = true;
		allow_empty_record = false;
	} else
		throw popt::error(text, "format specification is not {ascii|colon|space|tabbed}");
}

const char colour_definition::a[] = "colour";
colour_definition::~colour_definition() {}
void colour_definition::action(popt::processor & /*proc*/, const char * text)
{
	const char * end(text);
	if ('#'== *text) {
		unsigned long rgb(std::strtoul(text + 1, const_cast<char **>(&end), 16));
		if (!*end && end != text + 1) {
			value = MapTrueColour((rgb >> 16) & 0xFF,(rgb >> 8) & 0xFF,(rgb >> 0) & 0xFF);
			return;
		}
	}
	unsigned long index(std::strtoul(text, const_cast<char **>(&end), 0));
	if (!*end && end != text) {
		value = Map256Colour(index);
		return;
	}
	for (const struct colourname * b(colournames), * e(colournames + sizeof colournames/sizeof *colournames), * p(b); e != p; ++p) {
		if (0 == std::strcmp(text, p->name)) {
			value = Map256Colour(p->index);
			return;
		}
	}
	throw popt::error(text, "colour specification is not {[bright] {red|green|blue|cyan|magenta|black|white|grey}|#<hexRGB>|<index>}");
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_flat_table_viewer
(
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	unsigned long header_count(0UL);
	CharacterCell::colour_type header_colour(Map256Colour(COLOUR_YELLOW)), body_colour(Map256Colour(COLOUR_GREEN));
	TUIOutputBase::Options oo;
	format_definition format_option('\0', "format", "Specify the table file format.");
	try {
		popt::bool_definition cursor_application_mode_option('\0', "cursor-keypad-application-mode", "Set the cursor keypad to application mode instead of normal mode.", oo.cursor_application_mode);
		popt::bool_definition calculator_application_mode_option('\0', "calculator-keypad-application-mode", "Set the calculator keypad to application mode instead of normal mode.", oo.calculator_application_mode);
		popt::bool_definition no_alternate_screen_buffer_option('\0', "no-alternate-screen-buffer", "Prevent switching to the XTerm alternate screen buffer.", oo.no_alternate_screen_buffer);
		popt::bool_string_definition scnm_option('\0', "inversescreen", "Switch inverse screen mode on/off.", oo.scnm);
		popt::tui_level_definition tui_level_option('T', "tui-level", "Specify the level of TUI character set.");
		popt::definition * tui_table[] = {
			&cursor_application_mode_option,
			&calculator_application_mode_option,
			&no_alternate_screen_buffer_option,
			&scnm_option,
			&tui_level_option,
		};
		popt::unsigned_number_definition header_count_option('\0', "header-count", "number", "Specify how many of the initial records are headers.", header_count, 0);
		colour_definition header_colour_option('\0', "header-colour", "Specify the colour of header rows.", header_colour);
		colour_definition body_colour_option('\0', "body-colour", "Specify the colour of body rows.", body_colour);
		popt::table_definition tui_table_option(sizeof tui_table/sizeof *tui_table, tui_table, "Terminal quirks options");
		popt::definition * top_table[] = {
			&header_count_option,
			&format_option,
			&header_colour_option,
			&body_colour_option,
			&tui_table_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		oo.tui_level = tui_level_option.value() < oo.TUI_LEVELS ? tui_level_option.value() : oo.TUI_LEVELS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		die_errno(prog, envs, "kqueue");
	}
	std::vector<struct kevent> ip;

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
	}
#if defined(__LINUX__) || defined(__linux__)
	struct stat s;
#endif
	if (!args.empty()
#if defined(__LINUX__) || defined(__linux__)
	// epoll_ctl(), and hence kevent(), cannot poll anything other than devices, sockets, and pipes.
	// So we have to turn regular file standard input into a pipe.
	||  (0 <= fstat(STDIN_FILENO, &s) && !(S_ISCHR(s.st_mode) || S_ISFIFO(s.st_mode) || S_ISSOCK(s.st_mode)))
#endif
	) {
		auto_reap_children();

		int fds[2];
		if (0 > pipe_close_on_exec(fds)) {
			die_errno(prog, envs, "pipe");
		}
		const pid_t child(fork());
		if (0 > child) {
			die_errno(prog, envs, "fork");
		} else
		if (0 == child) {
			dup2(fds[1], STDOUT_FILENO);
			set_non_blocking(STDOUT_FILENO, false);
			// The original pipe descriptors are all open O_CLOEXEC and will be closed when we exec cat.
			args.insert(args.begin(), "--");
			args.insert(args.begin(), "cat");
			next_prog = arg0_of(args);
			return;
		} else
		{
			dup2(fds[0], STDIN_FILENO);
			// Close the write end of the pipe in the parent so that EOF propagates.
			close(fds[1]);
		}
	}

	append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, nullptr);
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

	Table table;

	TUIDisplayCompositor compositor(false /* no software cursor */, 24, 80);
	TUI ui(envs, table, compositor, control, control_fd, header_count, format_option, header_colour, body_colour, oo);

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
					} else
					{
						ui.handle_data(fd, e.data);
						if (EV_EOF & e.flags)
							append_event(ip, fd, EVFILT_READ, EV_DELETE|EV_DISABLE, 0, 0, nullptr);
					}
					break;
				}
			}
		}
	}

	throw EXIT_SUCCESS;
}
