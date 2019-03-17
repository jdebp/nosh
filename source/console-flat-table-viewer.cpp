/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <map>
#include <vector>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
#include <vis.h>
#endif
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
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
		do_unvis(true),
#endif
		allow_empty_field(false),
		allow_empty_record(false)
	{
	}

	int unit_separator_0, unit_separator_1, record_separator, group_separator, file_separator, eol_comment;
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	bool do_unvis;
#endif
	bool allow_empty_field, allow_empty_record;
};

struct TUI :
	public ParserCharacters,
	public TerminalCapabilities,
	public TUIOutputBase,
	public TUIInputBase
{
	TUI(ProcessEnvironment & e, Table & m, TUIDisplayCompositor & c, FILE * tty, unsigned long header_count, const ParserCharacters &, const CharacterCell::colour_type &, const CharacterCell::colour_type &, bool, bool, bool);
	~TUI();

	bool quit_flagged() const { return pending_quit_event; }
	bool immediate_update() const { return immediate_update_needed; }
	bool exit_signalled() const { return terminate_signalled||interrupt_signalled||hangup_signalled; }
	void handle_signal (int);
	void handle_control (int, int);
	void handle_data (int, int);
	void handle_update_event () { immediate_update_needed = false; TUIOutputBase::handle_update_event(); }

protected:
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
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	int unvis_state;
#endif
	std::size_t data_nr, data_nf, current_nr, current_nf;

	virtual void redraw_new();
	void set_refresh_and_immediate_update_needed () { immediate_update_needed = true; TUIOutputBase::set_refresh_needed(); }

	virtual void ExtendedKey(uint_fast16_t k, uint_fast8_t m);
	virtual void FunctionKey(uint_fast16_t k, uint_fast8_t m);
	virtual void UCS3(uint_fast32_t character);
	virtual void Accelerator(uint_fast32_t character);
	virtual void MouseMove(uint_fast16_t, uint_fast16_t, uint8_t);
	virtual void MouseWheel(uint_fast8_t n, int_fast8_t v, uint_fast8_t m);
	virtual void MouseButton(uint_fast8_t n, uint_fast8_t v, uint_fast8_t m);

	bool IsUnitSeparator(int c) { return unit_separator_0 == c || unit_separator_1 == c; }
	bool IsRecordSeparator(int c) { return record_separator == c; }
	bool IsGroupSeparator(int c) { return group_separator == c; }
	bool IsFileSeparator(int c) { return file_separator == c; }
	bool IsComment(int c) { return eol_comment == c; }
	bool AllowEmptyFields() { return allow_empty_field; }
	bool AllowEmptyRecords() { return allow_empty_record; }
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	bool DecodingVis() { return do_unvis; }
#endif

	void HandleData (const char *, std::size_t);
private:
	char data_buffer[64U * 1024U];
	char control_buffer[1U * 1024U];
};

}

TUI::TUI(
	ProcessEnvironment & e,
	Table & m,
	TUIDisplayCompositor & comp,
	FILE * tty,
	unsigned long c,
	const ParserCharacters & p,
	const CharacterCell::colour_type & h_colour,
	const CharacterCell::colour_type & b_colour,
	bool cursor_application_mode,
	bool calculator_application_mode,
	bool alternate_screen_buffer
) :
	ParserCharacters(p),
	TerminalCapabilities(e),
	TUIOutputBase(*this, tty, false /* no bold as colour */, false /* no faint as colour */, cursor_application_mode, calculator_application_mode, alternate_screen_buffer, comp),
	TUIInputBase(static_cast<const TerminalCapabilities &>(*this), tty),
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
	heading(ColourPair(h_colour, Map256Colour(COLOUR_BLACK))),
	body(ColourPair(b_colour, Map256Colour(COLOUR_BLACK))),
	header_count(c),
	table(m),
	state(START),
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	unvis_state(),
#endif
	data_nr(0U),
	data_nf(0U),
	current_nr(0U),
	current_nf(0U)
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
	}
	BreakInput();
}

void
TUI::handle_data (
	int fd,
	int n		///< number of characters available; can be <= 0 erroneously
) {
	for (;;) {
		int l(read(fd, data_buffer, sizeof data_buffer));
		if (0 >= l) break;
		HandleData(data_buffer, l);
		if (l >= n) break;
		n -= l;
	}
}

void
TUI::HandleData (
	const char * buf,
	std::size_t len
) {
	while (len) {
		if (IsFileSeparator(*buf)) {
			++buf;
			--len;
			table.clear();
			for (std::vector<ColumnInfo>::iterator b(info.begin()), e(info.end()), p(b); e != p; ++p)
				p->clear_auto();
			data_nr = data_nf = 0;
			set_refresh_needed();
		} else
		switch (state) {
			case START:
				if (IsComment(*buf)) {
					state = COMMENT;
					++buf;
					--len;
					break;
				} else
				if (IsGroupSeparator(*buf) || IsRecordSeparator(*buf)) {
					if (data_nf || AllowEmptyRecords()) ++data_nr;
					data_nf = 0;
					++buf;
					--len;
					break;
				} else
				if (IsUnitSeparator(*buf)) {
					if (AllowEmptyFields()) ++data_nf;
					++buf;
					--len;
					break;
				} else
				{
					state = FIELD;
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
					unvis_state = 0;
#endif
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

				if (IsGroupSeparator(*buf) || IsRecordSeparator(*buf) || IsUnitSeparator(*buf)) {
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
					if (DecodingVis()) {
						char c;
						if (UNVIS_VALID == unvis(&c, '\0', &unvis_state, UNVIS_END)) {
							field.push_back(c);
							ci.set_min_auto(field.length());
							set_refresh_needed();
						}
						unvis_state = 0;
					}
#endif
					if (IsUnitSeparator(*buf)) {
						++data_nf;
					} else {
						++data_nr;
						data_nf = 0;
					}
					state = START;
				} else
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
				if (DecodingVis()) {
					char c;
			again:
					switch (unvis(&c, *buf, &unvis_state, 0)) {
						case 0:
						case UNVIS_NOCHAR:
						case UNVIS_SYNBAD:
							break;
						case UNVIS_VALID:
							field.push_back(c);
							ci.set_min_auto(field.length());
							set_refresh_needed();
							break;
						case UNVIS_VALIDPUSH:
							field.push_back(c);
							ci.set_min_auto(field.length());
							set_refresh_needed();
							goto again;
					}
				} else
#endif
				{
					field.push_back(*buf);
					ci.set_min_auto(field.length());
					set_refresh_needed();
				}
				++buf;
				--len;
				break;
			}
			case COMMENT:
				do {
					const char c(*buf);
					++buf;
					--len;
					if (IsRecordSeparator(c) || IsGroupSeparator(c) || IsFileSeparator(c)) {
						if (table.size() > data_nr) data_nr = table.size();
						data_nf = 0;
						state = START;
						break;
					}
				} while (len);
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
		case SIGTSTP:	exit_full_screen_mode(); raise(SIGSTOP); break;
		case SIGCONT:	enter_full_screen_mode(); invalidate_cur(); set_update_needed(); break;
	}
}

void
TUI::redraw_new (
) {
	TUIDisplayCompositor::coordinate y(current_nr), x(0);
	{
		std::size_t nf(0U);
		for (std::vector<ColumnInfo>::iterator b(info.begin()), e(info.end()), p(b); e != p && nf < current_nf; ++p, ++nf)
			x += p->width() + 1;
	}
	// The window includes the cursor position.
	if (window_y > y) window_y = y; else if (window_y + c.query_h() <= y) window_y = y - c.query_h() + 1;
	if (window_x > x) window_x = x; else if (window_x + c.query_w() <= x) window_x = x - c.query_w() + 1;

	erase_new_to_backdrop();

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
			const unsigned width(ci.width());
			unsigned precision(width);
			if (precision > f.length()) precision = f.length();
			const CharacterCell::attribute_type attr(
				is_header && (is_current_col || is_current_row) ? CharacterCell::INVERSE|CharacterCell::BOLD :
				is_header || is_current_col || is_current_row ? CharacterCell::INVERSE :
				0U
			);
			vio.PrintFormatted(row, col, attr, colour, "%*.*s", width, precision, f.c_str());
			if (fi + 1 != fe)
				vio.Print(row, col, attr, colour, ' ');
		}
	}

	c.move_cursor(y - window_y, x - window_x);
	c.set_cursor_state(CursorSprite::BLINK, CursorSprite::BOX);
}

void
TUI::ExtendedKey(
	uint_fast16_t k,
	uint_fast8_t /*m*/
) {
	switch (k) {
		case EXTENDED_KEY_LEFT_ARROW:
		case EXTENDED_KEY_PAD_LEFT:
			if (current_nf > 0) { --current_nf; set_refresh_and_immediate_update_needed(); }
			break;
		case EXTENDED_KEY_RIGHT_ARROW:
		case EXTENDED_KEY_PAD_RIGHT:
			if (current_nf + 1 < info.size()) { ++current_nf; set_refresh_and_immediate_update_needed(); }
			break;
		case EXTENDED_KEY_DOWN_ARROW:
		case EXTENDED_KEY_PAD_DOWN:
			if (current_nr + 1 < table.size()) { ++current_nr; set_refresh_and_immediate_update_needed(); }
			break;
		case EXTENDED_KEY_UP_ARROW:
		case EXTENDED_KEY_PAD_UP:
			if (current_nr > 0) { --current_nr; set_refresh_and_immediate_update_needed(); }
			break;
		case EXTENDED_KEY_END:
		case EXTENDED_KEY_PAD_END:
			if (std::size_t s = table.size()) {
				if (current_nr + 1 != s) { current_nr = s - 1; set_refresh_and_immediate_update_needed(); }
				break;
			} else 
				[[clang::fallthrough]];
		case EXTENDED_KEY_HOME:
		case EXTENDED_KEY_PAD_HOME:
			if (current_nr != 0) { current_nr = 0U; set_refresh_and_immediate_update_needed(); }
			break;
		case EXTENDED_KEY_PAGE_DOWN:
		case EXTENDED_KEY_PAD_PAGE_DOWN:
			if (table.size() && current_nr + 1 < table.size()) {
				unsigned n(c.query_h());
				if (current_nr + n < table.size())
					current_nr += n;
				else
					current_nr = table.size() - 1;
				set_refresh_and_immediate_update_needed();
			}
			break;
		case EXTENDED_KEY_PAGE_UP:
		case EXTENDED_KEY_PAD_PAGE_UP:
			if (current_nr > 0) {
				unsigned n(c.query_h());
				if (current_nr > n)
					current_nr -= n;
				else
					current_nr = 0;
				set_refresh_and_immediate_update_needed();
			}
			break;
	}
}

void
TUI::FunctionKey(
	uint_fast16_t /*k*/,
	uint_fast8_t /*m*/
) {
}

void
TUI::UCS3(
	uint_fast32_t character
) {
	switch (character) {
		case EM:	// Control+Y
		case SUB:	// Control+Z
			killpg(0, SIGTSTP);
			break;
		case ETX:	// Control+C
		case EOT:	// Control+D
		case FS:	// Control+\ .
		case 'Q': case 'q':
			pending_quit_event = true;
			break;
		case 'W': case 'w': case 'J': case'j':
			ExtendedKey(EXTENDED_KEY_UP_ARROW, 0U);
			break;
		case 'S': case 's': case 'K': case 'k':
			ExtendedKey(EXTENDED_KEY_DOWN_ARROW, 0U);
			break;
		case 'A': case 'a': case 'H': case'h':
			ExtendedKey(EXTENDED_KEY_LEFT_ARROW, 0U);
			break;
		case 'D': case 'd': case 'L': case 'l':
			ExtendedKey(EXTENDED_KEY_RIGHT_ARROW, 0U);
			break;
		case STX:	// Control+B
		case CAN:	// Control+P
			ExtendedKey(EXTENDED_KEY_PAGE_UP, 0U);
			break;
		case ACK:	// Control+F
		case SO:	// Control+N
			ExtendedKey(EXTENDED_KEY_PAGE_DOWN, 0U);
			break;
		case '-':
		{
			if (current_nf >= info.size()) break;
			ColumnInfo & ci(info[current_nf]);
			if (ci.decrease_manual()) set_refresh_and_immediate_update_needed();
			break;
		}
		case '+':
		{
			if (current_nf >= info.size()) break;
			ColumnInfo & ci(info[current_nf]);
			if (ci.increase_manual()) set_refresh_and_immediate_update_needed();
			break;
		}
	}
}

void
TUI::Accelerator(
	uint_fast32_t character
) {
	switch (character) {
		case 'W': case 'w':
		case 'Q': case 'q':
			pending_quit_event = true;
			break;
	}
}

void 
TUI::MouseMove(
	uint_fast16_t /*row*/,
	uint_fast16_t /*col*/,
	uint8_t /*modifiers*/
) {
}

void 
TUI::MouseWheel(
	uint_fast8_t wheel,
	int_fast8_t value,
	uint_fast8_t /*modifiers*/
) {
	switch (wheel) {
		case 0U:
			while (value < 0) {
				ExtendedKey(EXTENDED_KEY_UP_ARROW, 0U);
				++value;
			}
			while (0 < value) {
				ExtendedKey(EXTENDED_KEY_DOWN_ARROW, 0U);
				--value;
			}
			break;
		case 1U:
			while (value < 0) {
				ExtendedKey(EXTENDED_KEY_LEFT_ARROW, 0U);
				++value;
			}
			while (0 < value) {
				ExtendedKey(EXTENDED_KEY_RIGHT_ARROW, 0U);
				--value;
			}
			break;
	}
}

void 
TUI::MouseButton(
	uint_fast8_t /*button*/,
	uint_fast8_t /*value*/,
	uint_fast8_t /*modifiers*/
) {
}

/* Command-line options *****************************************************
// **************************************************************************
*/

namespace {
	struct format_definition : public popt::compound_named_definition, public ParserCharacters {
	public:
		format_definition(char s, const char * l, const char * d) : compound_named_definition(s, l, a, d) {}
		virtual ~format_definition();
	protected:
		static const char a[];
		virtual void action(popt::processor &, const char *);
	};
	struct colour_definition : public popt::compound_named_definition {
	public:
		colour_definition(char s, const char * l, const char * d, CharacterCell::colour_type & v) : compound_named_definition(s, l, a, d), value(v) {}
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
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
		do_unvis = false;
#endif
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
		do_unvis = false;
#else
		eol_comment = EOF;
#endif
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
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
		do_unvis = true;
#endif
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
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
		do_unvis = true;
#endif
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
#if !defined(__LINUX__) && !defined(__linux__)
[[gnu::noreturn]] 
#endif
( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	unsigned long header_count(0UL);
	CharacterCell::colour_type header_colour(Map256Colour(COLOUR_YELLOW)), body_colour(Map256Colour(COLOUR_GREEN));
	bool cursor_application_mode(false);
	bool calculator_application_mode(false);
	bool no_alternate_screen_buffer(false);
	format_definition format_option('\0', "format", "Specify the table file format.");
	try {
		popt::unsigned_number_definition header_count_option('\0', "header-count", "number", "Specify how many of the initial records are headers.", header_count, 0);
		colour_definition header_colour_option('\0', "header-colour", "Specify the colour of header rows.", header_colour);
		colour_definition body_colour_option('\0', "body-colour", "Specify the colour of body rows.", body_colour);
		popt::bool_definition cursor_application_mode_option('\0', "cursor-keypad-application-mode", "Set the cursor keypad to application mode instead of normal mode.", cursor_application_mode);
		popt::bool_definition calculator_application_mode_option('\0', "calculator-keypad-application-mode", "Set the calculator keypad to application mode instead of normal mode.", calculator_application_mode);
		popt::bool_definition no_alternate_screen_buffer_option('\0', "no-alternate-screen-buffer", "Prevent switching to the XTerm alternate screen buffer.", no_alternate_screen_buffer);
		popt::definition * top_table[] = {
			&header_count_option,
			&format_option,
			&header_colour_option,
			&body_colour_option,
			&cursor_application_mode_option,
			&calculator_application_mode_option,
			&no_alternate_screen_buffer_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

#if defined(__LINUX__) || defined(__linux__)
	// epoll_ctl(), and hence kevent(), cannot poll anything other than devices, sockets, and pipes.
	// So we have to turn regular file standard input into a pipe.
	struct stat s;
	if (0 <= fstat(STDIN_FILENO, &s) && !(S_ISCHR(s.st_mode) || S_ISFIFO(s.st_mode) || S_ISSOCK(s.st_mode))) {
		int fds[2];
		if (0 > pipe_close_on_exec(fds)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "pipe", std::strerror(error));
			throw EXIT_FAILURE;
		}
		pid_t child(fork());
		if (0 > child) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "fork", std::strerror(error));
			throw EXIT_FAILURE;
		} else
		if (0 == child) {
			dup2(fds[1], STDOUT_FILENO);
			args.push_back("cat");
			next_prog = arg0_of(args);
			return;
		} else
		{
			dup2(fds[0], STDIN_FILENO);
		}
	}
#endif

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}
	std::vector<struct kevent> ip;

	const char * tty(envs.query("TTY"));
	if (!tty) tty = "/dev/tty";
	FileStar control(std::fopen(tty, "w+"));
	if (!control) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, tty, std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (isatty(STDIN_FILENO)) {
		struct stat s0, st;

		if (0 <= fstat(STDIN_FILENO, &s0)
		&&  0 <= fstat(fileno(control), &st)
		&&  S_ISCHR(s0.st_mode)
		&&  (s0.st_rdev == st.st_rdev)
		) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, tty, "The controlling terminal cannot be both standard input and control input.");
			throw EXIT_FAILURE;
		}
	}

	append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
	append_event(ip, fileno(control), EVFILT_READ, EV_ADD, 0, 0, 0);
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, SIGWINCH, SIGTSTP, SIGCONT, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, 0);
	append_event(ip, SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGCONT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);

	Table table;

	TUIDisplayCompositor compositor(false /* no software cursor */, 24, 80);
	TUI ui(envs, table, compositor, control, header_count, format_option, header_colour, body_colour, cursor_application_mode, calculator_application_mode, !no_alternate_screen_buffer);

	// How long to wait with updates pending.
	const struct timespec short_timeout = { 0, 100000000L };
	const struct timespec immediate_timeout = { 0, 0 };

	std::vector<struct kevent> p(4);
	while (true) {
		if (ui.exit_signalled() || ui.quit_flagged())
			break;
		ui.handle_resize_event();
		ui.handle_refresh_event();

		const struct timespec * timeout(ui.immediate_update() ? &immediate_timeout : ui.has_update_pending() ? &short_timeout : 0);
		const int rc(kevent(queue.get(), ip.data(), ip.size(), p.data(), p.size(), timeout));
		ip.clear();

		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
#if defined(__LINUX__) || defined(__linux__)
			if (EINVAL == error) continue;	// This works around a Linux bug when an inotify queue overflows.
			if (0 == error) continue;	// This works around another Linux bug.
#endif
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
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
					if (fileno(control) == fd) {
						ui.handle_control(fd, e.data);
					} else
					{
						ui.handle_data(fd, e.data);
						if (EV_EOF & e.flags)
							append_event(ip, fd, EVFILT_READ, EV_DELETE|EV_DISABLE, 0, 0, 0);
					}
					break;
				}
			}
		}
	}

	throw EXIT_SUCCESS;
}
