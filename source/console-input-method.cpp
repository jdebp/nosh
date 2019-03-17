/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#define _XOPEN_SOURCE_EXTENDED
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <csignal>
#include <clocale>
#include <cerrno>
#include <stdint.h>
#include <sys/stat.h>
#include "kqueue_common.h"
#include <sys/param.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "InputMessage.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "SignalManagement.h"
#include "TUIDisplayCompositor.h"
#include "VirtualTerminalBackEnd.h"
#include "InputFIFO.h"
#include "UTF8Decoder.h"


/* Realizing a virtual terminal *********************************************
// **************************************************************************
*/

namespace {

typedef std::vector<uint_fast32_t> charvec;
typedef std::list<charvec> conversion_list;

enum { CHINESE1_DATA_TABLE, CHINESE2_DATA_TABLE, KATAKANA_DATA_TABLE, HIRAGANA_DATA_TABLE, HANGEUL_DATA_TABLE, ROMAJI_DATA_TABLE, MAX_DATA_TABLES };

class DataTable
{
public:
	DataTable();
	~DataTable();

	void set_fold_case(bool v) { fold_case = v; }
	bool query_fold_case() const { return fold_case; }
	void add_raw_to_kana(const std::string & r, const charvec & c);
	void add_engraving(char k, const charvec & c) { raw_to_engraving.insert(std::make_pair(k,c)); }

	bool exceeds_max_conversion_length(std::size_t l) const { return l > max_conversion_length; }
	bool find_conversions (const std::string &, conversion_list &) const;
	charvec engraving_for(char) const;

protected:
	typedef std::multimap<std::string,charvec> conversion_map;
	conversion_map raw_to_kana;
	typedef std::map<char,charvec> engraving_map;
	engraving_map raw_to_engraving;
	bool fold_case;
	std::size_t max_conversion_length;

private:
};

class DataTableLoader :
	public UTF8Decoder::UCS32CharacterSink
{
public:
	DataTableLoader(DataTable & t, const char * p, const char * f);
	void load();
protected:
	DataTable & table;
	UTF8Decoder decoder;
	enum { FIRST, COMMENT_REST, DIRECTIVE_FIRST, DIRECTIVE_SPACE1, DIRECTIVE_SECOND, DIRECTIVE_REST, DATA_FIRST, DATA_SPACE1, DATA_SECOND, DATA_REST } state;
	bool chardef, keyname;
	const char * const prog, * const filename;
	unsigned long line;
	std::string first;
	charvec second;

	virtual void ProcessDecodedUTF8(uint32_t character, bool decoder_error, bool overlong);
};

class Realizer
{
public:
	Realizer(FILE *, VirtualTerminalBackEnd &, TUIDisplayCompositor &, const DataTable tables[MAX_DATA_TABLES]);
	~Realizer();

	void set_refresh_needed() { refresh_needed = true; }
	bool exit_signalled() const { return terminate_signalled||interrupt_signalled||hangup_signalled; }
	void handle_refresh_event ();
	void handle_update_event ();
	void handle_signal (int);
	void handle_input_event (uint32_t);

protected:
	bool refresh_needed, update_needed;
	sig_atomic_t terminate_signalled, interrupt_signalled, hangup_signalled;
	FileStar const buffer_file;
	VirtualTerminalBackEnd & lower_vt;
	TUIDisplayCompositor & comp;
	const DataTable * const tables;
	bool active;

	std::string raw;
	std::string::size_type rawpos;

	charvec data_to_send;
	std::size_t cursorpos;

	enum ConversionMode { TO_HIRAGANA, TO_KATAKANA, TO_HANGEUL, TO_ROMAJI_LOWER, TO_ROMAJI_UPPER, TO_ROMAJI_CAMEL, TO_HANJI1, TO_HANJI2, TO_ROMAJI_MIN = TO_ROMAJI_LOWER, TO_ROMAJI_MAX = TO_ROMAJI_CAMEL, TO_HANJI_MIN = TO_HANJI1, TO_HANJI_MAX = TO_HANJI2 };

	ConversionMode conversion_mode;

	conversion_list conversions;
	conversion_list::const_iterator current_conversion;
	charvec converted_engravings, unconverted_engravings;

	void resize ();
	void compose ();
	void paint_changed_cells ();

	void convert ();
	void use_current_conversion ();
	void copy_conversion_to_send (const charvec &);
	void lookup_all_table_conversions (const charvec &, const DataTable &, const std::string &);
	void create_all_fixed_english_conversions ();
	const DataTable & current_table() const;

	enum { CELL_LENGTH = 16U, HEADER_LENGTH = 16U };
};

const CharacterCell::colour_type overscan_fg(ALPHA_FOR_ERASED,255,255,255), overscan_bg(ALPHA_FOR_ERASED,0,0,0);
const CharacterCell overscan_blank(' ', 0U, overscan_fg, overscan_bg);
const CharacterCell::colour_type border_fg(ALPHA_FOR_ERASED,95,95,95), border_bg(ALPHA_FOR_ERASED,0,0,0);
const CharacterCell blank_cell(' ', CharacterCell::INVERSE, border_fg, border_bg);
const CharacterCell::colour_type converted_fg(ALPHA_FOR_TRUE_COLOURED,255,255,255), converted_bg(ALPHA_FOR_TRUE_COLOURED,0,0,0);
const CharacterCell::colour_type unconverted_fg(ALPHA_FOR_TRUE_COLOURED,191,191,191), unconverted_bg(ALPHA_FOR_TRUE_COLOURED,0,0,0);

inline
bool
is_begin (
	const charvec & v
) {
	return 5 == v.size() && 'b' == v[0] && 'e' == v[1] && 'g' == v[2] && 'i' == v[3] && 'n' == v[4];
}

inline
bool
is_end (
	const charvec & v
) {
	return 3 == v.size() && 'e' == v[0] && 'n' == v[1] && 'd' == v[2];
}

}

DataTable::DataTable(
) :
	fold_case(true),
	max_conversion_length(0U)
{
}

DataTable::~DataTable() {}

void
DataTable::add_raw_to_kana(
	const std::string & r,
	const charvec & c
) {
	if (max_conversion_length < r.length())
		max_conversion_length = r.length();
	raw_to_kana.insert(std::make_pair(r,c));
}

charvec
DataTable::engraving_for (
	char c
) const {
	engraving_map::const_iterator i(raw_to_engraving.find(c));
	if (raw_to_engraving.end() != i)
		return i->second;
	charvec r;
	r.push_back(uint_fast32_t(c));
	return r;
}

bool
DataTable::find_conversions (
	const std::string & ro_lo,
	conversion_list & results
) const {
	results.clear();
	conversion_map::const_iterator i(raw_to_kana.find(ro_lo));
	if (raw_to_kana.end() == i) return false;
	for (conversion_map::const_iterator e(raw_to_kana.upper_bound(i->first)); i != e; ++i)
		results.push_back(i->second);
	return true;
}

DataTableLoader::DataTableLoader(
	DataTable & t,
	const char * p,
	const char * f
) :
	table(t),
	decoder(*this),
	state(FIRST),
	chardef(false),
	keyname(false),
	prog(p),
	filename(f),
	line(0UL)
{
}

void
DataTableLoader::load (
) {
	std::ifstream f(filename);
	if (!f) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
	line = 1UL;
	for (int c(f.get()); EOF != c; c = f.get()) 
		decoder.Process(c);
}

void
DataTableLoader::ProcessDecodedUTF8(
	uint32_t character,
	bool decoder_error,
	bool /*overlong*/
) {
	if (decoder_error) {
		std::fprintf(stderr, "%s: FATAL: %s(%lu): %s\n", prog, filename, line, "Invalid UTF-8 in file.");
		throw EXIT_FAILURE;
	}
	if ('\n' == character) ++line;
	switch (state) {
		case FIRST:
			if ('\n' == character)
				break;
			else
			{
				first.clear();
				second.clear();
				if ('%' == character)
					state = DIRECTIVE_FIRST;
				else
				if ('#' == character)
					state = COMMENT_REST;
				else
				if (character < 0x100 && std::isspace(character))
					state = DATA_FIRST;
				else
				{
					state = DATA_FIRST;
					goto data1;
				}
			}
			break;

		case COMMENT_REST:
			if ('\n' == character)
				state = FIRST;
			break;

		// Parsing directives into a leading ASCII portion and a trailing Unicode portion.

		case DIRECTIVE_FIRST:
			if ('\n' == character)
				goto directive_end;
			if (0x100 <= character) {
				std::fprintf(stderr, "%s: FATAL: %s(%lu): %s\n", prog, filename, line, "Invalid directive name.");
				throw EXIT_FAILURE;
			} else
			if (!std::isspace(static_cast<char>(character)))
				first.push_back(static_cast<char>(character));
			else
			if (first.empty()) {
				std::fprintf(stderr, "%s: FATAL: %s(%lu): %s\n", prog, filename, line, "Invalid directive line with empty first field.");
				throw EXIT_FAILURE;
			} else
			{
				state = DIRECTIVE_SPACE1;
				goto directive_space;
			}
			break;
		case DIRECTIVE_SPACE1:
		directive_space:
			if ('\n' == character)
				goto directive_end;
			else
			if (0x100 <= character || !std::isspace(static_cast<char>(character))) {
				state = DIRECTIVE_SECOND;
				goto directive2;
			}
			break;
		case DIRECTIVE_SECOND:
			if ('\n' == character)
				goto directive_end;
			else
		directive2:
				second.push_back(character);
			break;
		case DIRECTIVE_REST:
			if ('\n' == character)
				goto directive_end;
			break;
		directive_end:
			if ("keyname" == first) {
				if (is_begin(second)) {
					keyname = true;
					chardef = false;
				} else
				if (is_end(second)) {
					keyname = false;
				} else
				{
					std::fprintf(stderr, "%s: FATAL: %s(%lu): %s: %s\n", prog, filename, line, first.c_str(), "Directive followed by neither begin nor end.");
					throw EXIT_FAILURE;
				}
			} else
			if ("chardef" == first) {
				if (is_begin(second)) {
					chardef = true;
					keyname = false;
				} else
				if (is_end(second)) {
					chardef = false;
				} else
				{
					std::fprintf(stderr, "%s: FATAL: %s(%lu): %s: %s\n", prog, filename, line, first.c_str(), "Directive followed by neither begin nor end.");
					throw EXIT_FAILURE;
				}
			} else
			if ("keep_key_case" == first)
				table.set_fold_case(false);
			else
				;
			state = FIRST;
			break;

		// Parsing data lines into a leading ASCII field, a Unicode field, and a trailing portion that is ignored.

		case DATA_FIRST:
			if ('\n' == character) {
				std::fprintf(stderr, "%s: FATAL: %s(%lu): %s\n", prog, filename, line, "Invalid data line with only 1 field.");
				throw EXIT_FAILURE;
			}
		data1:
			if (0x100 <= character) {
				std::fprintf(stderr, "%s: FATAL: %s(%lu): %s\n", prog, filename, line, "Invalid raw data field.");
				throw EXIT_FAILURE;
			} else
			if (!std::isspace(static_cast<char>(character)))
				first.push_back(static_cast<char>(character));
			else
			if (first.empty()) {
				std::fprintf(stderr, "%s: FATAL: %s(%lu): %s\n", prog, filename, line, "Invalid data line with empty first field.");
				throw EXIT_FAILURE;
			} else
			{
				state = DATA_SPACE1;
				goto data_space;
			}
			break;
		case DATA_SPACE1:
		data_space:
			if ('\n' == character) {
				std::fprintf(stderr, "%s: FATAL: %s(%lu): %s\n", prog, filename, line, "Invalid data line with only 1 field.");
				throw EXIT_FAILURE;
			} else
			if (0x100 <= character || !std::isspace(static_cast<char>(character))) {
				state = DATA_SECOND;
				goto data2;
			}
			break;
		case DATA_SECOND:
			if ('\n' == character)
				goto data_end;
		data2:
			if (character < 0x100 && std::isspace(static_cast<char>(character))) {
				state = DATA_REST;
				goto data_rest;
			} else
				second.push_back(character);
			break;
		case DATA_REST:
		data_rest:
			if ('\n' == character)
				goto data_end;
			break;
		data_end:
			if (keyname) {
				if (first.length() != 1U) {
					std::fprintf(stderr, "%s: FATAL: %s(%lu): %s\n", prog, filename, line, "Invalid key name that is not 1 character.");
					throw EXIT_FAILURE;
				}
				table.add_engraving(first.front(), second);
			} else
			if (chardef)
				table.add_raw_to_kana(first, second);
			else
			{
				std::fprintf(stderr, "%s: FATAL: %s(%lu): %s: %s\n", prog, filename, line, first.c_str(), "Bogus data not part of an engraving or a conversion.");
				throw EXIT_FAILURE;
			}
			state = FIRST;
			break;
	}
}

Realizer::Realizer (
	FILE * f,
	VirtualTerminalBackEnd & l,
	TUIDisplayCompositor & c,
	const DataTable t[MAX_DATA_TABLES]
) :
	refresh_needed(true),
	update_needed(true),
	terminate_signalled(false),
	interrupt_signalled(false),
	hangup_signalled(false),
	buffer_file(f),
	lower_vt(l),
	comp(c),
	tables(t),
	active(false),
	rawpos(0U),
	cursorpos(0U),
	conversion_mode(TO_HANJI1),
	current_conversion(conversions.begin())
{
}

Realizer::~Realizer() {}

const DataTable &
Realizer::current_table (
) const {
	switch (conversion_mode) {
		case TO_HANJI1:		return tables[CHINESE1_DATA_TABLE];
		case TO_HANJI2:		return tables[CHINESE2_DATA_TABLE];
		case TO_KATAKANA:	return tables[KATAKANA_DATA_TABLE];
		case TO_HIRAGANA:	return tables[HIRAGANA_DATA_TABLE];
		case TO_HANGEUL:	return tables[HANGEUL_DATA_TABLE];
		default:		return tables[ROMAJI_DATA_TABLE];
	}
}

void
Realizer::create_all_fixed_english_conversions (
) {
	charvec r, u, l, m;
	bool start_word(true);
	for (std::size_t b(0U); b < rawpos; ++b) {
		const unsigned char c(static_cast<unsigned char>(raw[b]));
		if (std::isalpha(c)) {
			int uc(std::toupper(c));
			int lc(std::tolower(c));
			if (EOF == lc) lc = c;
			if (EOF == uc) uc = c;
			r.push_back(c);
			u.push_back(uc);
			l.push_back(lc);
			m.push_back(start_word ? uc : lc);
			start_word = false;
		} else {
			r.push_back(c);
			if (!std::isspace(c)) {
				u.push_back(c);
				l.push_back(c);
				m.push_back(c);
			}
			start_word = true;
		}
	}
	switch (conversion_mode) {
		case TO_ROMAJI_LOWER:
			conversions.push_back(l);
			conversions.push_back(u);
			conversions.push_back(m);
			break;
		case TO_ROMAJI_UPPER:
			conversions.push_back(u);
			conversions.push_back(m);
			conversions.push_back(l);
			break;
		case TO_ROMAJI_CAMEL:
			conversions.push_back(m);
			conversions.push_back(l);
			conversions.push_back(u);
			break;
		default:
			break;
	}
	conversions.push_back(r);
}

void
Realizer::lookup_all_table_conversions (
	const charvec & c,
	const DataTable & table,
	const std::string & to_convert		///< guaranteed to be non-empty by the caller
) {
	const std::size_t len(to_convert.length());

	std::size_t start(0U);
	while (std::isspace(static_cast<unsigned char>(to_convert[start]))) {
		++start;
		if (len <= start) {
			// We reached the end by skipping whitespace, so we need to explicitly add the prefix as a conversion.
			conversions.push_back(c);
			return;
		}
	}

	std::size_t end_or_space(start);
	while (end_or_space < len && !std::isspace(static_cast<unsigned char>(to_convert[end_or_space]))) ++end_or_space;

	bool found(false);
	for (std::size_t e(end_or_space); e > start; --e) {
		if (table.exceeds_max_conversion_length(e - start)) continue;	// Avoid constructing and destroying lists and strings.
		conversion_list l;
		const std::string s(to_convert.substr(start, e - start));
		if (table.find_conversions(s, l)) {
			const std::string remainder(to_convert.substr(e));
			for (conversion_list::const_iterator p(l.begin()); p != l.end(); ++p) {
				const charvec & tail(*p);
				charvec r(c);
				r.insert(r.end(), tail.begin(), tail.end());
				if (len <= e)
					conversions.push_back(r);
				else
					lookup_all_table_conversions(r, table, remainder);
			}
			found = true;
		}
	}
	if (!found) {
		charvec r(c);
		const char k(to_convert[start]);
		const charvec s(table.engraving_for(k));
		r.insert(r.end(), s.begin(), s.end());
		if (len <= start + 1U)
			conversions.push_back(r);
		else
			lookup_all_table_conversions(r, table, to_convert.substr(start + 1U));
	}
}

void
Realizer::copy_conversion_to_send (
	const charvec & c
) {
	data_to_send = c;
	cursorpos = data_to_send.size();
	// Add on the keycaps for the raw data from the insertion point onwards.
	data_to_send.insert(data_to_send.end(), unconverted_engravings.begin(), unconverted_engravings.end());
}

void
Realizer::use_current_conversion (
) {
	if (conversions.end() == current_conversion)
		copy_conversion_to_send(converted_engravings);
	else
		copy_conversion_to_send(*current_conversion);
	set_refresh_needed();
}

void
Realizer::convert (
) {
	const DataTable & table(current_table());
	conversions.clear();
	if (0U < rawpos) {
		std::string to_convert(raw.substr(0U, rawpos));
		if (table.query_fold_case())
			to_convert = tolower(to_convert);
		lookup_all_table_conversions(charvec(), table, to_convert);
		create_all_fixed_english_conversions();
	}
	converted_engravings.clear();
	unconverted_engravings.clear();
	for (std::size_t i(0U); i < raw.length(); ++i) {
		const char k(raw[i]);
		const charvec s(table.engraving_for(table.query_fold_case() ? tolower(k) : k));
		charvec & t (i < rawpos ? converted_engravings : unconverted_engravings);
		t.insert(t.end(), s.begin(), s.end());
	}
	current_conversion = conversions.begin();
	use_current_conversion();
	resize();
}

inline
void
Realizer::resize ()
{
	const std::size_t data_to_sendlen(data_to_send.size());
	std::size_t maxmenulen(0U);
	for (conversion_list::const_iterator p(conversions.begin()), e(conversions.end()); p != e; ++p) {
		if (maxmenulen < p->size())
			maxmenulen = p->size();
	}
	const std::size_t len(data_to_sendlen + 1U < maxmenulen + 1U ? maxmenulen + 1U : data_to_sendlen + 1U);
	unsigned short cols(lower_vt.query_w()), rows(lower_vt.query_h());
	if (cols < len) cols = len;
	if (rows < 2U) rows = 2U;
	comp.resize(rows, cols);
}

/// \brief Render the terminal's display buffer onto the display's screen buffer.
inline
void
Realizer::compose () 
{
	for (unsigned short row(0U); row < lower_vt.query_h(); ++row) {
		for (unsigned short col(0U); col < lower_vt.query_w(); ++col)
			comp.poke(row, col, lower_vt.at(row, col));
		for (unsigned short col(lower_vt.query_w()); col < comp.query_w(); ++col)
			comp.poke(row, col, overscan_blank);
	}
	for (unsigned short row(lower_vt.query_h()); row < comp.query_h(); ++row) {
		for (unsigned short col(0U); col < comp.query_w(); ++col)
			comp.poke(row, col, overscan_blank);
	}

	const unsigned a(lower_vt.query_cursor_attributes());
	comp.set_pointer_attributes(lower_vt.query_pointer_attributes());
	unsigned short x(lower_vt.query_cursor_x());
	unsigned short y(lower_vt.query_cursor_y());
	if (!active) {
		comp.move_cursor(y, x);
		comp.set_cursor_state(a, lower_vt.query_cursor_glyph());
		return;
	}

	const std::size_t data_to_sendlen(data_to_send.size());
	const std::size_t menulen(current_conversion == conversions.end() ? 0U : current_conversion->size());
	const std::size_t len(data_to_sendlen + 1U < menulen + 1U ? menulen + 1U : data_to_sendlen + 1U);
	if (x + len > comp.query_w()) x = comp.query_w() - len;
	if (y + 2U > comp.query_h()) y = comp.query_h() - 2U;

	comp.set_cursor_state(CursorSprite::VISIBLE|CursorSprite::BLINK, CursorSprite::BLOCK);
	comp.move_cursor(y + 0U, x + cursorpos);

	for (unsigned short col(0U); col < data_to_sendlen; ++col) {
		const CharacterCell::colour_type & fg(col < cursorpos ? converted_fg : unconverted_fg);
		const CharacterCell::colour_type & bg(col < cursorpos ? converted_bg : unconverted_bg);
		const CharacterCell cell(data_to_send[col], CharacterCell::INVERSE, fg, bg);
		comp.poke(y + 0U, x + col, cell);
	}
	for (unsigned short col(data_to_sendlen); col < len; ++col)
		comp.poke(y + 0U, x + col, blank_cell);

	if (current_conversion != conversions.end()) {
		const charvec & line(*current_conversion);
		for (unsigned short col(0U); col < menulen; ++col) {
			const CharacterCell cell(line[col], CharacterCell::INVERSE, converted_fg, converted_bg);
			comp.poke(y + 1U, x + col, cell);
		}
	}
	for (unsigned short col(menulen); col < len - 1U; ++col)
		comp.poke(y + 1U, x + col, blank_cell);
	CharacterCell status_cell(' ', CharacterCell::INVERSE, border_fg, border_bg);
	if (current_conversion != conversions.end()) {
		// If there are conversions, the status cell is an arrow indicating scrollability.
		if (current_conversion == conversions.begin()) 
			status_cell.character = 0x2193;
		else {
			conversion_list::const_iterator next_conversion(current_conversion);
			++next_conversion;
			if (next_conversion == conversions.end())
				status_cell.character = 0x2191;
			else
				status_cell.character = 0x2195;
		}
	} else {
		// If there are no conversions, display the conversion mode as the status cell.
		switch (conversion_mode) {
			case TO_HIRAGANA:	status_cell.character = 0x3042 /* ha in hiragana */; break;
			case TO_KATAKANA:	status_cell.character = 0x30ab /* ka in katakana */; break;
			case TO_HANGEUL:	status_cell.character = 0xd55c /* han in hangeul */; break;
			case TO_ROMAJI_LOWER:	status_cell.character = L'r'; break;
			case TO_ROMAJI_UPPER:	status_cell.character = L'R'; break;
			case TO_ROMAJI_CAMEL:	status_cell.character = L'm'; break;
			case TO_HANJI1:		status_cell.character = 0x6c49 /* han in hanzi/kanji/hanja (simplified) */; break;
			case TO_HANJI2:		status_cell.character = 0x6f22 /* han in hanzi/kanji/hanja (traditional) */; break;
			default:		status_cell.character = L'Q'; break;
		}
	}
	comp.poke(y + 1U, x + len - 1U, status_cell);
}

inline
void
Realizer::paint_changed_cells (
) {
	// The stdio buffers may well be out of synch, so we need to reset them.
	std::rewind(buffer_file);
	std::fflush(buffer_file);

	uint8_t header[HEADER_LENGTH] = { 
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		static_cast<uint8_t>(comp.query_cursor_glyph()), 
		static_cast<uint8_t>(comp.query_cursor_attributes()), 
		static_cast<uint8_t>(comp.query_pointer_attributes()), 
		0,
	};
	const uint32_t bom(0xFEFF);
	std::memcpy(&header[ 0], &bom, sizeof bom);
	const uint16_t cols(comp.query_w()), rows(comp.query_h());
	std::memcpy(&header[ 4], &cols, sizeof cols);
	std::memcpy(&header[ 6], &rows, sizeof rows);
	const uint16_t x(comp.query_cursor_col()), y(comp.query_cursor_row());
	std::memcpy(&header[ 8], &x, sizeof x);
	std::memcpy(&header[10], &y, sizeof y);
	std::fwrite(header, sizeof header, 1U, buffer_file);

	if (HEADER_LENGTH != ftello(buffer_file))
		std::fseek(buffer_file, HEADER_LENGTH, SEEK_SET);

	for (unsigned row(0); row < rows; ++row) {
		for (unsigned col(0); col < cols; ++col) {
			TUIDisplayCompositor::DirtiableCell & cell(comp.cur_at(row, col));
			if (!cell.touched()) continue;
			unsigned char b[CELL_LENGTH] = { 
				cell.foreground.alpha, cell.foreground.red, cell.foreground.green, cell.foreground.blue, 
				cell.background.alpha, cell.background.red, cell.background.green, cell.background.blue, 
				0, 0, 0, 0, 
				static_cast<uint8_t>(cell.attributes & 0xFF), static_cast<uint8_t>(cell.attributes >> 8U),
				0, 0
			};
			std::memcpy(&b[8], &cell.character, 4);
			const off_t pos(HEADER_LENGTH + CELL_LENGTH * (row * cols + col));
			if (pos != ftello(buffer_file))
				std::fseek(buffer_file, pos, SEEK_SET);
			std::fwrite(b, sizeof b, 1U, buffer_file);
			cell.untouch();
		}
	}
	const off_t pos(HEADER_LENGTH + CELL_LENGTH * (rows * cols));
	if (pos != ftello(buffer_file))
		std::fseek(buffer_file, pos, SEEK_SET);
	std::fflush(buffer_file);
	ftruncate(fileno(buffer_file), ftello(buffer_file));
}

inline
void
Realizer::handle_refresh_event (
) {
	if (refresh_needed) {
		refresh_needed = false;
		resize();
		compose();
		update_needed = true;
	}
}

inline
void
Realizer::handle_update_event (
) {
	if (update_needed) {
		update_needed = false;
		comp.repaint_new_to_cur();
		paint_changed_cells();
	}
}

inline
void
Realizer::handle_signal (
	int signo
) {
	switch (signo) {
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
	}
}

inline
void
Realizer::handle_input_event(
	uint32_t b
) {
	switch (b & (0x00FFFF00|INPUT_MSG_MASK)) {
		case INPUT_MSG_EKEY|(EXTENDED_KEY_HIRAGANA << 8U):
		case INPUT_MSG_EKEY|(EXTENDED_KEY_KATAKANA << 8U):
		case INPUT_MSG_EKEY|(EXTENDED_KEY_HANGEUL << 8U):
		case INPUT_MSG_EKEY|(EXTENDED_KEY_ROMAJI << 8U):
		case INPUT_MSG_EKEY|(EXTENDED_KEY_ZENKAKU_HANKAKU << 8U):
		case INPUT_MSG_EKEY|(EXTENDED_KEY_KATAKANA_HIRAGANA << 8U):
		case INPUT_MSG_EKEY|(EXTENDED_KEY_HENKAN << 8U):
		case INPUT_MSG_EKEY|(EXTENDED_KEY_MUHENKAN << 8U):
		case INPUT_MSG_EKEY|(EXTENDED_KEY_HANJA << 8U):
		case INPUT_MSG_EKEY|(EXTENDED_KEY_HAN_YEONG << 8U):
			// Pressing these implicitly activates the FEP.
			active = true;
			break;
		toggle:
		case INPUT_MSG_EKEY|(EXTENDED_KEY_IM_TOGGLE << 8U):
			active = !active;
			set_refresh_needed();
			return;
	}
	if (!active) {
		lower_vt.WriteInputMessage(b);
		return;
	}
	const uint_fast16_t k((b >> 8U) & 0xFFFF);
	const uint_fast32_t c(b & ~INPUT_MSG_MASK);
	const uint_fast8_t m(b & 0xFF);
	unsigned repeat(1U);
	switch (b & INPUT_MSG_MASK) {
		case INPUT_MSG_SESSION:
			// Always pass session switches to a lower console-multiplexor, so that users can switch sessions without turning the input method off and back on again.
			lower_vt.WriteInputMessage(b);
			return;
		case INPUT_MSG_CKEY:	
		{
			switch (k) {
				case CONSUMER_KEY_LOCK:
				case CONSUMER_KEY_LOGON:
				case CONSUMER_KEY_LOGOFF:
				case CONSUMER_KEY_HALT_TASK:
				case CONSUMER_KEY_TASK_MANAGER:
				case CONSUMER_KEY_SELECT_TASK:
				case CONSUMER_KEY_NEXT_TASK:
				case CONSUMER_KEY_PREVIOUS_TASK:
					lower_vt.WriteInputMessage(b);
					return;
				default:	
					break;
			}
			break;
		}
		case INPUT_MSG_EKEY:
		{
			switch (k) {
				muhenkan:
				case EXTENDED_KEY_MUHENKAN:
					break;
				case EXTENDED_KEY_ZENKAKU_HANKAKU:
					// We do not have any notion of half-width romaji or katakana.
					break;
				kanji:
				case EXTENDED_KEY_HENKAN:
				case EXTENDED_KEY_HANJA:
					if (conversion_mode < TO_HANJI_MIN  || TO_HANJI_MAX <= conversion_mode)
						conversion_mode = TO_HANJI_MIN;
					else
						conversion_mode = static_cast<ConversionMode>(conversion_mode + 1);
					convert();
					break;
				hiragana:
				case EXTENDED_KEY_HIRAGANA:
					conversion_mode = TO_HIRAGANA;
					convert();
					break;
				katakana:
				case EXTENDED_KEY_KATAKANA:
					conversion_mode = TO_KATAKANA;
					convert();
					break;
				case EXTENDED_KEY_HANGEUL:
				hangeul:
					conversion_mode = TO_HANGEUL;
					convert();
					break;
				romaji:
				case EXTENDED_KEY_ROMAJI:
					if (conversion_mode < TO_ROMAJI_MIN  || TO_ROMAJI_MAX <= conversion_mode)
						conversion_mode = TO_ROMAJI_MIN;
					else
						conversion_mode = static_cast<ConversionMode>(conversion_mode + 1);
					convert();
					break;
				case EXTENDED_KEY_HAN_YEONG:
					if (TO_ROMAJI_MAX == conversion_mode)
						goto hangeul;
					else
						goto romaji;
				case EXTENDED_KEY_KATAKANA_HIRAGANA:
					switch (m & (INPUT_MODIFIER_LEVEL2|INPUT_MODIFIER_LEVEL3)) {
						case INPUT_MODIFIER_LEVEL3:	goto romaji;
						case INPUT_MODIFIER_LEVEL2:	goto katakana;
						case 0:				goto hiragana;
					}
					break;
				case EXTENDED_KEY_LEFT_ARROW:
				case EXTENDED_KEY_PAD_LEFT:
					if (0U < rawpos) {
						--rawpos;
						convert();
					}
					break;
				case EXTENDED_KEY_RIGHT_ARROW:
				case EXTENDED_KEY_PAD_RIGHT:
					if (raw.size() > rawpos) {
						++rawpos;
						convert();
					}
					break;
				home:
				case EXTENDED_KEY_HOME:
				case EXTENDED_KEY_PAD_HOME:
					if (0U != rawpos) {
						rawpos = 0U;
						convert();
					}
					break;
				end:
				case EXTENDED_KEY_END:
				case EXTENDED_KEY_PAD_END:
					if (raw.size() != rawpos) {
						rawpos = raw.size();
						convert();
					}
					break;
				case EXTENDED_KEY_DEL_CHAR:
				case EXTENDED_KEY_DELETE:
				case EXTENDED_KEY_PAD_DELETE:
					if (raw.size() > rawpos) {
						for (std::size_t i(rawpos + 1U); i < raw.size(); ++i) raw[i - 1U] = raw[i];
						raw.resize(raw.size() - 1U);
						convert();
					}
					break;
				case EXTENDED_KEY_BACKSPACE:
				case EXTENDED_KEY_PAD_BACKSPACE:
					if (0U < rawpos) {
						for (std::size_t i(rawpos); i < raw.size(); ++i) raw[i - 1U] = raw[i];
						raw.resize(raw.size() - 1U);
						--rawpos;
						convert();
					}
					break;
				case EXTENDED_KEY_PAGE_UP:
				case EXTENDED_KEY_PAD_PAGE_UP:
					repeat = 100U;
					[[clang::fallthrough]];
				case EXTENDED_KEY_UP_ARROW:
				case EXTENDED_KEY_PAD_UP:
					while (repeat) {
						--repeat;
						if (conversions.begin() != current_conversion) {
							--current_conversion;
							use_current_conversion();
						} else
							break;
					}
					break;
				case EXTENDED_KEY_PAGE_DOWN:
				case EXTENDED_KEY_PAD_PAGE_DOWN:
					repeat = 100U;
					[[clang::fallthrough]];
				case EXTENDED_KEY_DOWN_ARROW:
				case EXTENDED_KEY_PAD_DOWN:
					while (repeat) {
						--repeat;
						if (conversions.end() != current_conversion) {
							conversion_list::const_iterator next_conversion(current_conversion);
							++next_conversion;
							if (conversions.end() != next_conversion) {
								current_conversion = next_conversion;
								use_current_conversion();
							} else
								break;
						} else
							break;
					}
					break;
				case EXTENDED_KEY_RETURN:
				case EXTENDED_KEY_RETURN_OR_ENTER:
				case EXTENDED_KEY_EXECUTE:
				case EXTENDED_KEY_PAD_ENTER:
					for (std::size_t i(0U); i < data_to_send.size(); ++i)
						lower_vt.WriteInputMessage(MessageForUCS3(data_to_send[i]));
					raw.clear();
					rawpos = 0U;
					convert();
					break;
			}
			break;
		}
		case INPUT_MSG_FKEY:
		{
			switch (k) {
				case 6U:	goto hiragana;	// per Windows
				case 7U:	goto katakana;	// per Windows
				case 8U:	goto katakana;	// ignore half-width, per Windows
				case 9U:	goto romaji;	// per Windows
				case 10U:	goto romaji;	// ignore half-width, per Windows
			}
			break;
		}
		case INPUT_MSG_PUCS3:
		{
			switch (c) {
				// Ignore any pasted control characters, for security.
				rawkey:
				default:
					if (0x00000020 <= c && c < 0x0000007F) {
						raw.insert(rawpos, 1U, static_cast<char>(c));
						++rawpos;
						convert();
					}
					break;
			}
			break;
		}
		case INPUT_MSG_UCS3:
		{
			switch (c) {
				case 0x00000000:	goto toggle;	// Control+@
				case 0x00000001:	goto home;	// Control+A, per emacs
				case 0x00000005:	goto end;	// Control+E, per emacs
				case 0x00000007:	goto hangeul;	// Control+G
				case 0x0000000C:	goto hiragana;	// Control+L, per OSF/1 IMLIB
				case 0x0000000B:	goto katakana;	// Control+K, per OSF/1 IMLIB and MacOS
				case 0x0000000E:	goto muhenkan;	// Control+N, per OSF/1 IMLIB
				case 0x00000012:	goto romaji;	// Control+R
				case 0x0000001A:	goto kanji;	// Control+Z
				// Realizers should never send enter/return/backspace/tab/del as raw UCS3 characters.
				// They should rather send them as the relevant extended keys, with modifiers.
				default:		goto rawkey;
			}
			break;
		}
		default:
			break;
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_input_method [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool chinese1_table(false), chinese2_table(false), kana_table(false), hangeul_table(false), romaji_table(false);

	try {
		popt::bool_definition chinese1_table_option('\0', "chinese1", "Perform conversion to hanzi/kanji/hanja using a data table.", chinese1_table);
		popt::bool_definition chinese2_table_option('\0', "chinese2", "Perform conversion to hanzi/kanji/hanja using a data table.", chinese2_table);
		popt::bool_definition kana_table_option('\0', "kana", "Perform conversion to hiragana/katakana using two data tables.", kana_table);
		popt::bool_definition hangeul_table_option('\0', "hangeul", "Perform conversion to hangeul using a data table.", hangeul_table);
		popt::bool_definition romaji_table_option('\0', "romaji", "Perform conversion to romaji using a data table.", romaji_table);
		popt::definition * top_table[] = {
			&chinese1_table_option,
			&chinese2_table_option,
			&kana_table_option,
			&hangeul_table_option,
			&romaji_table_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{upper-vc} {lower-vc} [data-tables...]");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing upper virtual terminal directory name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * uvcname(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing lower virtual terminal directory name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * lvcname(args.front());
	args.erase(args.begin());

	DataTable tables[MAX_DATA_TABLES];
	if (chinese1_table) {
		if (args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing chinese data table name.");
			throw static_cast<int>(EXIT_USAGE);
		}
		const char * table_name(args.front());
		args.erase(args.begin());
		DataTableLoader loader(tables[CHINESE1_DATA_TABLE], prog, table_name);
		loader.load();
	}
	if (chinese2_table) {
		if (args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing chinese data table name.");
			throw static_cast<int>(EXIT_USAGE);
		}
		const char * table_name(args.front());
		args.erase(args.begin());
		DataTableLoader loader(tables[CHINESE2_DATA_TABLE], prog, table_name);
		loader.load();
	}
	if (kana_table) {
		if (args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing kana data table name.");
			throw static_cast<int>(EXIT_USAGE);
		}
		const char * table_name(args.front());
		args.erase(args.begin());
		DataTableLoader loader(tables[HIRAGANA_DATA_TABLE], prog, table_name);
		loader.load();
	}
	if (kana_table) {
		if (args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing kana data table name.");
			throw static_cast<int>(EXIT_USAGE);
		}
		const char * table_name(args.front());
		args.erase(args.begin());
		DataTableLoader loader(tables[KATAKANA_DATA_TABLE], prog, table_name);
		loader.load();
	}
	if (hangeul_table) {
		if (args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing hangeul data table name.");
			throw static_cast<int>(EXIT_USAGE);
		}
		const char * table_name(args.front());
		args.erase(args.begin());
		DataTableLoader loader(tables[HANGEUL_DATA_TABLE], prog, table_name);
		loader.load();
	}
	if (romaji_table) {
		if (args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing romaji data table name.");
			throw static_cast<int>(EXIT_USAGE);
		}
		const char * table_name(args.front());
		args.erase(args.begin());
		DataTableLoader loader(tables[ROMAJI_DATA_TABLE], prog, table_name);
		loader.load();
	}
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}
	std::vector<struct kevent> ip;

	FileDescriptorOwner upper_vt_dir_fd(open_dir_at(AT_FDCWD, uvcname));
	if (0 > upper_vt_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, uvcname, std::strerror(error));
		throw EXIT_FAILURE;
	}

	// We need an explicit lock file, because we cannot lock FIFOs.
	FileDescriptorOwner lock_fd(open_lockfile_at(upper_vt_dir_fd.get(), "lock"));
	if (0 > lock_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, uvcname, "lock", std::strerror(error));
		throw EXIT_FAILURE;
	}
	// We are allowed to open the read end of a FIFO in non-blocking mode without having to wait for a writer.
	mkfifoat(upper_vt_dir_fd.get(), "input", 0620);
	InputFIFO upper_input(open_read_at(upper_vt_dir_fd.get(), "input"));
	if (0 > upper_input.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, uvcname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (0 > fchown(upper_input.get(), -1, getegid())) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, uvcname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	// We have to keep a client (write) end descriptor open to the input FIFO.
	// Otherwise, the first console client process triggers POLLHUP when it closes its end.
	// Opening the FIFO for read+write isn't standard, although it would work on Linux.
	FileDescriptorOwner upper_input_write_fd(open_writeexisting_at(upper_vt_dir_fd.get(), "input"));
	if (0 > upper_input_write_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, uvcname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileDescriptorOwner upper_buffer_fd(open_writecreate_at(upper_vt_dir_fd.get(), "display", 0640));
	if (0 > upper_buffer_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, uvcname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (0 > fchown(upper_buffer_fd.get(), -1, getegid())) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, uvcname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileStar upper_buffer_file(fdopen(upper_buffer_fd.get(), "w"));
	if (!upper_buffer_file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, uvcname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	upper_buffer_fd.release();

	FileDescriptorOwner lower_vt_dir_fd(open_dir_at(AT_FDCWD, lvcname));
	if (0 > lower_vt_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, lvcname, std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileDescriptorOwner lower_buffer_fd(open_read_at(lower_vt_dir_fd.get(), "display"));
	if (0 > lower_buffer_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, lvcname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileStar lower_buffer_file(fdopen(lower_buffer_fd.get(), "r"));
	if (!lower_buffer_file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, lvcname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	lower_buffer_fd.release();
	FileDescriptorOwner lower_input_fd(open_writeexisting_at(lower_vt_dir_fd.get(), "input"));
	if (lower_input_fd.get() < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, lvcname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	lower_vt_dir_fd.release();

	append_event(ip, upper_input.get(), EVFILT_READ, EV_ADD, 0, 0, 0);
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, 0);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);

	VirtualTerminalBackEnd lower_vt(lvcname, lower_buffer_file.release(), lower_input_fd.release());
	append_event(ip, lower_vt.query_buffer_fd(), EVFILT_VNODE, EV_ADD|EV_ENABLE|EV_CLEAR, NOTE_WRITE, 0, 0);
	append_event(ip, lower_vt.query_input_fd(), EVFILT_WRITE, EV_ADD|EV_DISABLE, 0, 0, 0);
	TUIDisplayCompositor compositor(false /* no software cursor */, 24, 80);
	Realizer realizer(upper_buffer_file, lower_vt, compositor, tables);

	const struct timespec immediate_timeout = { 0, 0 };

	while (true) {
		if (realizer.exit_signalled())
			break;
		realizer.handle_refresh_event();
		realizer.handle_update_event();

		struct kevent p[128];
		const int rc(kevent(queue.get(), ip.data(), ip.size(), p, sizeof p/sizeof *p, lower_vt.query_reload_needed() ? &immediate_timeout : 0));
		ip.clear();

		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}

		if (0 == rc) {
			if (lower_vt.query_reload_needed()) {
				lower_vt.reload();
				realizer.set_refresh_needed();
			}
			continue;
		}

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_VNODE:
					if (lower_vt.query_buffer_fd() == static_cast<int>(e.ident))
						lower_vt.set_reload_needed();
					break;
				case EVFILT_SIGNAL:
					realizer.handle_signal(e.ident);
					break;
				case EVFILT_READ:
					if (upper_input.get() == static_cast<int>(e.ident))
						upper_input.ReadInput();
					break;
				case EVFILT_WRITE:
					if (lower_vt.query_input_fd() == static_cast<int>(e.ident))
						lower_vt.FlushMessages();
					break;
			}
		}

		while (upper_input.HasMessage())
			realizer.handle_input_event(upper_input.PullMessage());
		if (lower_vt.MessageAvailable()) {
			if (!lower_vt.query_polling_for_write()) {
				append_event(ip, lower_vt.query_input_fd(), EVFILT_WRITE, EV_ENABLE, 0, 0, 0);
				lower_vt.set_polling_for_write(true);
			}
		} else {
			if (lower_vt.query_polling_for_write()) {
				append_event(ip, lower_vt.query_input_fd(), EVFILT_WRITE, EV_DISABLE, 0, 0, 0);
				lower_vt.set_polling_for_write(false);
			}
		}
	}

	throw EXIT_SUCCESS;
}
