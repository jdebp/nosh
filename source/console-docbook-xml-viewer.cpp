/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <map>
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

/* Full-screen TUI **********************************************************
// **************************************************************************
*/

namespace {

typedef short ColourPaletteIndex;

enum : short { NO_COLOUR = -1 };

enum { BULLET = 0x2022 };

bool
Equals (
	const u32string & s,
	const char * p
) {
	const std::size_t len(std::strlen(p));
	if (len != s.length()) return false;
	for (std::size_t i(0U); i < len; ++i)
		if (s[i] != uint32_t(p[i]))
			return false;
	return true;
}

void
Copy (
	u32string & s,
	const char * p
) {
	s.clear();
	while (*p) s.insert(s.end(), *p++);
}

u32string
Make (
	const char * p
) {
	u32string r;
	Copy(r, p);
	return r;
}

bool IsNewline(uint32_t character) { return CR == character || LF == character || VT == character || FF == character || NEL == character; }

bool IsWhitespace(uint32_t character) { return IsNewline(character) || SPC == character || TAB == character || DEL == character || NUL == character; }

bool
HasNonWhitespace(
	const u32string & s
) {
	for (u32string::const_iterator p(s.begin()), e(s.end()); p != e; ++p) {
		if (!IsWhitespace(*p))
			return true;
	}
	return false;
}

struct DisplayItem {
	DisplayItem(CharacterCell::attribute_type a, ColourPaletteIndex c, const u32string & s) : attr(a), colour_index(c), text(s) {}
	DisplayItem(CharacterCell::attribute_type a, ColourPaletteIndex c, const u32string::const_iterator & b, const u32string::const_iterator & e) : attr(a), colour_index(c), text(b, e) {}
	CharacterCell::attribute_type attr;
	ColourPaletteIndex colour_index;
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

typedef std::map<u32string, u32string> Attributes;

struct Element;

struct Tag {
	Tag(bool c) : opening(!c), closing(c), has_name(false), literal(false), name(), attributes() {}
	Tag(bool c, const Element &);
	bool opening, closing, has_name, literal;
	u32string name;
	Attributes attributes;
};

struct Element {
	u32string name;
	Attributes attributes;
	bool query_element_content() const { return element_content; }
	bool query_literal() const { return literal; }
	bool query_block() const { return block; }
	bool query_margin() const { return margin; }
	bool query_indent() const { return indent; }
	bool query_outdent() const { return outdent; }
	bool take_bullet() { const bool b(bullet); bullet = false; return b; }
	CharacterCell::attribute_type query_attr() const { return attr; }
	ColourPaletteIndex query_colour_index() const { return colour_index; }
	bool matches_attribute(const u32string & name, const char * value) const;
	bool query_attribute(const u32string & name, u32string & value) const;
	Element(const Tag & t);
	bool has_children, has_child_elements;
protected:
	bool element_content, literal, block, margin, indent, outdent, bullet;
	ColourPaletteIndex colour_index;
	CharacterCell::attribute_type attr;
};

struct DocumentParser : public UTF8Decoder::UCS32CharacterSink {
	DocumentParser(DisplayDocument & d, unsigned long c) : doc(d), columns(c), state(CONTENT), current_line(nullptr), content(), name(), value(), tag(false) {}
protected:
	typedef std::list<Element> Elements;

	DisplayDocument & doc;
	unsigned long columns;
	enum State {
		CONTENT,
		ENTITY,
		LESS_THAN,
		TAG_NAME,
		TAG_EQUALS,
		TAG_VALUE_UNQUOTED,
		TAG_VALUE_SINGLE,
		TAG_VALUE_DOUBLE,
		TAG_WHITESPACE,
		TAG_CLOSING,
		DIRECTIVE_HEAD,
		DIRECTIVE_HEAD_DASH,
		DIRECTIVE_BODY,
		COMMENT_BODY,
		COMMENT_DASH,
		COMMENT_DASHDASH,
		PROCESSOR_COMMAND_BODY,
		PROCESSOR_COMMAND_QUESTION,
	} state;
	DisplayLine * current_line;
	u32string content, name, value;
	Tag tag;
	Elements elements;

	virtual void ProcessDecodedUTF8(char32_t character, bool decoder_error, bool overlong);
	void flush_content();
	void flush_entity();
	void flush_attribute();
	void flush_tag();
	void append_item_pre(CharacterCell::attribute_type a, ColourPaletteIndex c, const u32string::const_iterator & b, const u32string::const_iterator & e);
	void append_item_wrap(CharacterCell::attribute_type a, ColourPaletteIndex c, const u32string::const_iterator & b, const u32string::const_iterator & e);
	void append_items_pre(CharacterCell::attribute_type a, ColourPaletteIndex c, const u32string & s);
	void append_items_wrap(CharacterCell::attribute_type a, ColourPaletteIndex c, const u32string & s);
	void append_items(const Element & e, CharacterCell::attribute_type a, const u32string & s);
	void append_items(const Element & e, const u32string & s);
	void indent();
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

Tag::Tag(
	bool c,
	const Element & e
) :
	opening(!c),
	closing(c),
	has_name(false),
	literal(e.query_literal()),
	name(),
	attributes()
{
}

Element::Element(
	const Tag & t
) :
	name(t.name),
	attributes(t.attributes),
	has_children(false),
	has_child_elements(false),
	element_content(false),
	literal(t.literal),
	block(false),
	margin(false),
	indent(false),
	outdent(false),
	bullet(false),
	colour_index(NO_COLOUR),
	attr(0)
{
	// Layout

	element_content =
		Equals(name, "refentry") ||
//		Equals(name, "refmeta") ||
		Equals(name, "refnamediv") ||
		Equals(name, "refsynopsisdiv") ||
		Equals(name, "refsection") ||
		Equals(name, "refsect1") ||
		Equals(name, "sect1") ||
		Equals(name, "sect2") ||
		Equals(name, "funcsynopsis") ||
		Equals(name, "classynopsis") ||
		Equals(name, "group") ||
		Equals(name, "orderedlist") ||
		Equals(name, "itemizedlist") ||
		Equals(name, "variablelist") ||
		Equals(name, "varlistentry") ||
		Equals(name, "listitem") ||
		Equals(name, "table") ||
		Equals(name, "thead") ||
		Equals(name, "tbody") ||
		Equals(name, "tr") ||
		false
	;
	const bool is_literal =
		Equals(name, "address") ||
		Equals(name, "literallayout") ||
		Equals(name, "programlisting") ||
		Equals(name, "screen") ||
		Equals(name, "screenshot") ||
		Equals(name, "synopsis") ||
		Equals(name, "funcsynopsisinfo") ||
		Equals(name, "classynopsisinfo") ||
		false
	;
	if (is_literal) literal = is_literal;	// Otherwise inherit from the tag which inherits from the enclosing element.
	block =
		Equals(name, "refentry") ||
		Equals(name, "refsection") ||
		Equals(name, "refsect1") ||
		Equals(name, "sect1") ||
		Equals(name, "sect2") ||
		Equals(name, "refsynopsisdiv") ||
		Equals(name, "refnamediv") ||
		Equals(name, "example") ||
		Equals(name, "informalexample") ||
		Equals(name, "programlisting") ||
		Equals(name, "literallayout") ||
		Equals(name, "screen") ||
		Equals(name, "refmeta") ||
		Equals(name, "title") ||
		Equals(name, "subtitle") ||
		Equals(name, "para") ||
		Equals(name, "funcsynopsis") ||
		Equals(name, "funcprototype") ||
		Equals(name, "cmdsynopsis") ||
		Equals(name, "orderedlist") ||
		Equals(name, "itemizedlist") ||
		Equals(name, "variablelist") ||
		Equals(name, "varlistentry") ||
		Equals(name, "table") ||
		Equals(name, "thead") ||
		Equals(name, "tbody") ||
		Equals(name, "tr") ||
		Equals(name, "warning") ||
		Equals(name, "caution") ||
		Equals(name, "note") ||
		Equals(name, "tip") ||
		Equals(name, "important") ||
		false
	;
	margin =
		Equals(name, "title") ||
		Equals(name, "para") ||
		Equals(name, "refmeta") ||
		Equals(name, "refsection") ||
		Equals(name, "refsect1") ||
		Equals(name, "sect1") ||
		Equals(name, "sect2") ||
		Equals(name, "refsynopsisdiv") ||
		Equals(name, "refnamediv") ||
		Equals(name, "example") ||
		Equals(name, "informalexample") ||
		Equals(name, "table") ||
		Equals(name, "warning") ||
		Equals(name, "caution") ||
		Equals(name, "note") ||
		Equals(name, "tip") ||
		Equals(name, "important") ||
		false
	;
	outdent =
		Equals(name, "title") ||
		Equals(name, "subtitle") ||
		false
	;
	indent =
		Equals(name, "refmeta") ||
		Equals(name, "refsection") ||
		Equals(name, "refsect1") ||
		Equals(name, "sect1") ||
		Equals(name, "sect2") ||
		Equals(name, "refsynopsisdiv") ||
		Equals(name, "refnamediv") ||
		Equals(name, "listitem") ||
		Equals(name, "literallayout") ||
		Equals(name, "note") ||
		Equals(name, "table") ||
		false
	;
	bullet =
		Equals(name, "listitem") ||
		false
	;

	// Attributes

	const bool bold =
		Equals(name, "refname") ||
		Equals(name, "thead") ||
		Equals(name, "th") ||
		false
	;
	const bool italic =
		Equals(name, "emphasis") ||
		Equals(name, "replaceable") ||
		Equals(name, "citetitle") ||
		false
	;
	const bool simple_underline =
		Equals(name, "ulink") ||
		Equals(name, "refentrytitle") ||
		Equals(name, "manvolnum") ||
		false
	;
	const bool double_underline =
		Equals(name, "subtitle") ||
		false
	;
	const bool inverse =
		Equals(name, "title") ||
		Equals(name, "keycap") ||
		false
	;
	attr =
		(bold ? CharacterCell::BOLD : 0) |
		(italic ? CharacterCell::ITALIC : 0) |
		(simple_underline ? CharacterCell::SIMPLE_UNDERLINE : 0) |
		(double_underline ? CharacterCell::DOUBLE_UNDERLINE : 0) |
		(inverse ? CharacterCell::INVERSE : 0) |
		0
	;

	// Colours

	if (Equals(name, "filename"))
		colour_index = COLOUR_MAGENTA;
	else
	if (Equals(name, "arg"))
		colour_index = COLOUR_CYAN;
	else
	if (Equals(name, "code"))
		colour_index = COLOUR_LIGHT_CYAN;
	else
	if (Equals(name, "command")
	||  Equals(name, "function")
	||  Equals(name, "class")
	||  Equals(name, "refentrytitle")
	||  Equals(name, "manvolnum")
	)
		colour_index = COLOUR_GREEN;
	else
	if (Equals(name, "keycap")
	||  Equals(name, "envar")
	)
		colour_index = COLOUR_YELLOW;
	else
	if (Equals(name, "userinput"))
		colour_index = COLOUR_DARK_VIOLET;
	else
	if (Equals(name, "warning")
	||  Equals(name, "caution")
	)
		colour_index = COLOUR_DARK_ORANGE3;
}

bool
Element::matches_attribute(
	const u32string & attribute_name,
	const char * value
) const {
	const Attributes::const_iterator i(attributes.find(attribute_name));
	if (attributes.end() == i) return false;
	return Equals(i->second, value);
}

bool
Element::query_attribute(
	const u32string & attribute_name,
	u32string & value
) const {
	const Attributes::const_iterator i(attributes.find(attribute_name));
	if (attributes.end() == i) return false;
	value = i->second;
	return true;
}

void
DocumentParser::indent()
{
	std::size_t spaces(0U);
	bool bullet(false);
	for (Elements::iterator ep(elements.begin()), ee(elements.end()); ee != ep; ++ep) {
		if (ep->query_indent() && spaces + 2U < columns)
			spaces += 2U;
		if (ep->query_outdent() && spaces >= 2U)
			spaces -= 2U;
		if (!bullet) bullet = ep->take_bullet();
	}
	if (spaces) {
		u32string s(spaces, SPC);
		if (spaces > 1U && bullet)
			s[spaces - 2U] = BULLET;
		current_line->push_back(DisplayItem(0, NO_COLOUR, s));
	}
}

void
DocumentParser::append_item_pre(
	CharacterCell::attribute_type a,
	ColourPaletteIndex c,
	const u32string::const_iterator & b,
	const u32string::const_iterator & e
) {
	if (!current_line) {
		current_line = doc.append_line();
		indent();
	}
	if (e != b)
		current_line->push_back(DisplayItem(a, c, b, e));
}

void
DocumentParser::append_item_wrap(
	CharacterCell::attribute_type a,
	ColourPaletteIndex c,
	const u32string::const_iterator & b,
	const u32string::const_iterator & e
) {
	const std::size_t len(e - b);
	if (!current_line) {
		current_line = doc.append_line();
		indent();
	} else
	if (current_line->width() + len > columns) {
		current_line = doc.append_line();
		indent();
	}
	if (e != b)
		current_line->push_back(DisplayItem(a, c, b, e));
}

void
DocumentParser::append_items_pre(
	CharacterCell::attribute_type a,
	ColourPaletteIndex c,
	const u32string & s
) {
	u32string::const_iterator b(s.begin()), p(b);
	for (u32string::const_iterator e(s.end()); p != e; ++p) {
		const uint32_t character(*p);
		if (IsNewline(character)) {
			append_item_pre(a, c, b, p);
			b = p + 1;
			current_line = nullptr;
		}
	}
	append_item_pre(a, c, b, p);
}

void
DocumentParser::append_items_wrap(
	CharacterCell::attribute_type a,
	ColourPaletteIndex c,
	const u32string & s
) {
	u32string::const_iterator b(s.begin()), e(s.end()), p(b);
	for (bool last_was_space(false); p != e; ++p) {
		const uint32_t character(*p);
		const bool is_space(IsWhitespace(character));
		if (last_was_space && !is_space && p != b) {
			append_item_wrap(a, c, b, p);
			b = p;
		}
		last_was_space = is_space;
	}
	if (p != b)
		append_item_wrap(a, c, b, p);
}

void
DocumentParser::append_items(
	const Element & e,
	CharacterCell::attribute_type a,
	const u32string & s
) {
	if (e.query_literal())
		append_items_pre(a, e.query_colour_index(), s);
	else
		append_items_wrap(a, e.query_colour_index(), s);
}

void
DocumentParser::append_items(
	const Element & e,
	const u32string & s
) {
	if (e.query_literal())
		append_items_pre(e.query_attr(), e.query_colour_index(), s);
	else
		append_items_wrap(e.query_attr(), e.query_colour_index(), s);
}

namespace {
	const u32string choice(Make("choice"));
	const u32string repeat(Make("repeat"));
	const u32string url(Make("url"));
}

void
DocumentParser::flush_tag()
{
	if (tag.opening) {
		Element element(tag);

		// block prefixes
		if (Equals(tag.name, "refnamediv")) {
			u32string s;
			Copy(s, "Name");
			current_line = nullptr;
			append_items(element, element.query_attr() ^ CharacterCell::INVERSE, s);
			current_line = doc.append_line();
		}
		if (Equals(tag.name, "refsynopsisdiv")) {
			u32string s;
			Copy(s, "Synopsis");
			current_line = nullptr;
			append_items(element, element.query_attr() ^ CharacterCell::INVERSE, s);
			current_line = doc.append_line();
		}
		if (Equals(tag.name, "thead")
		) {
			u32string s(3U, 0x2501);
			s[0] = 0x250D;
			current_line = nullptr;
			append_items(element, s);
		}
		if (Equals(tag.name, "tbody")
		) {
			u32string s(3U, 0x2501);
			s[0] = 0x251D;
			current_line = nullptr;
			append_items(element, s);
		}

		if (element.query_block())
			current_line = nullptr;

		// inline prefixes that check the parent element
		if (Equals(tag.name, "term")
		||  Equals(tag.name, "refname")
		) {
			if (!elements.empty()) {
				const Element & parent(elements.back());
				if (parent.has_child_elements && current_line) {
					u32string s(1U, ',');
					append_items(element, s);
				}
			}
		}
		if (Equals(tag.name, "arg")
		||  Equals(tag.name, "group")
		) {
			if (!elements.empty()) {
				const Element & parent(elements.back());
				if (Equals(parent.name, "group") && Equals(tag.name, "arg")) {
					if (parent.has_child_elements) {
						u32string s(1U, '|');
						append_items(element, s);
					}
				}
			}
		}

		if (!elements.empty()) {
			Element & parent(elements.back());
			parent.has_children = true;
			parent.has_child_elements = true;
		}
		elements.push_back(element);

		// inline prefixes that might need to have the same indent
		if (Equals(tag.name, "refpurpose")) {
			u32string s;
			Copy(s, " - ");
			s[1] = 0x2014;
			append_items(element, s);
		}
		if (Equals(tag.name, "manvolnum")) {
			u32string s(1U, '(');
			append_items(element, s);
		}
		if (Equals(tag.name, "quote")) {
			u32string s(1U, 0x2018);
			append_items(element, s);
		}
		if (Equals(tag.name, "arg")
		||  Equals(tag.name, "group")
		) {
			if (element.matches_attribute(choice, "opt")) {
				u32string s(1U, '[');
				append_items(element, s);
			} else
			if (element.matches_attribute(choice, "req")) {
				u32string s(1U, '{');
				append_items(element, s);
			}
		}
		if (Equals(tag.name, "note")) {
			u32string s;
			Copy(s, "Note: ");
			append_items(element, element.query_attr() ^ CharacterCell::BOLD, s);
		}
		if (Equals(tag.name, "tip")) {
			u32string s;
			Copy(s, "Tip: ");
			append_items(element, element.query_attr() ^ CharacterCell::BOLD, s);
		}
		if (Equals(tag.name, "important")) {
			u32string s;
			Copy(s, "Important: ");
			append_items(element, element.query_attr() ^ CharacterCell::BOLD, s);
		}
		if (Equals(tag.name, "tr")) {
			u32string s;
			Copy(s, "| ");
			s[0] = 0x2502;
			append_items(element, s);
		}
	}
	if (tag.closing) {
		if (elements.empty())
			throw "No opening tag.";
		Element & element(elements.back());
		if (element.name != tag.name)
			throw "Mismatched closing tag.";

		// inline suffixes
		if (Equals(tag.name, "refpurpose")) {
			current_line = nullptr;
		}
		if (Equals(tag.name, "manvolnum")) {
			u32string s(1U, ')');
			append_items(element, s);
		}
		if (Equals(tag.name, "quote")) {
			u32string s(1U, 0x2019);
			append_items(element, s);
		}
		if (Equals(tag.name, "arg")
		||  Equals(tag.name, "group")
		) {
			if (element.matches_attribute(choice, "opt")) {
				u32string s(1U, ']');
				append_items(element, s);
			} else
			if (element.matches_attribute(choice, "req")) {
				u32string s(1U, '}');
				append_items(element, s);
			}
			if (element.matches_attribute(repeat, "rep")) {
				u32string s(1U, 0x2026);
				append_items(element, s);
			}
		}
		if (Equals(tag.name, "ulink")) {
			u32string attrvalue;
			if (element.query_attribute(url, attrvalue)) {
				u32string s(1U, '(');
				append_items(element, s);
				append_items(element, attrvalue);
				s[0] = ')';
				append_items(element, s);
			}
		}
		if (Equals(tag.name, "th")
		||  Equals(tag.name, "td")
		) {
			u32string s;
			Copy(s, " | ");
			s[1] = 0x2502;
			append_items(element, s);
		}

		if (element.query_block()) {
			if (element.query_margin())
				doc.append_line();
		}

		elements.pop_back();
	}
}

void
DocumentParser::flush_attribute()
{
	if (!tag.has_name) {
		if (name.empty()) return;
		if (!value.empty()) throw "A tag name may not have a value.";
		tag.name = name;
		tag.has_name = true;
	} else {
		Attributes::iterator i(tag.attributes.find(name));
		if (i != tag.attributes.end()) throw "Multiple attributes may not have the same names.";
		tag.attributes[name] = value;
	}
	name.clear();
	value.clear();
}

void
DocumentParser::flush_content()
{
	if (content.empty()) return;

	// The newline conversions are mandatory even for literal-layout content.
	{
		bool cr(false);
		for (u32string::iterator p(content.begin()); p != content.end(); ) {
			const uint32_t c(*p);
			if (CR == c) {
				cr = true;
				p = content.erase(p);
			} else
			if (LF != c) {
				if (cr) {
					p = content.insert(p, LF);
					cr = false;
				}
				++p;
			} else
			{
				cr = false;
				++p;
			}
		}
		if (cr)
			content.insert(content.end(), LF);
	}

	if (elements.empty()) return;
	Element & element(elements.back());

	if (element.query_literal()) {
		if (!content.empty()) {
			append_items_pre(element.query_attr(), element.query_colour_index(), content);
			content.clear();
			element.has_children = true;
		}
		return;
	}

	// Non-literal layout turns all whitespace sequences into a single SPC character.
	bool seen_space(!element.has_children);
	for (u32string::iterator p(content.begin()), e(content.end()); e != p; ) {
		const uint32_t c(*p);
		if (IsWhitespace(c)) {
			p = content.erase(p);
			if (!seen_space) {
				p = content.insert(p, SPC);
				++p;
				seen_space = true;
			}
			e = content.end();
		} else {
			seen_space = false;
			++p;
		}
	}

	// Only element-content elements (as opposed to mixed-content elements) have ignorable whitespace.
	if (HasNonWhitespace(content) || !element.query_element_content())
		append_items_wrap(element.query_attr(), element.query_colour_index(), content);
	content.clear();
	element.has_children = true;
}

void
DocumentParser::flush_entity()
{
	if (content.empty()) return;

	if ('#' == content[0]) {
		content.erase(content.begin());
		unsigned base(10U);
		if ('x' == content[0]) {
			base = 16U;
			content.erase(content.begin());
		}
		if (content.empty()) throw "Invalid number.";
		uint32_t n(0);
		do {
			uint32_t c(content[0]);
			if ('0' <= c && c <= '9')
				n = n * base + (c - '0');
			else
			if (16 == base && ('a' <= c && c <= 'f'))
				n = n * base + 10 + (c - 'a');
			else
			if (16 == base && ('A' <= c && c <= 'F'))
				n = n * base + 10 + (c - 'A');
			else
				throw "Invalid digit.";
			content.erase(content.begin());
		} while (!content.empty());
		content.insert(content.begin(), n);
	} else
	if (Equals(content, "lt"))
		Copy(content, "<");
	else
	if (Equals(content, "gt"))
		Copy(content, ">");
	else
	if (Equals(content, "amp"))
		Copy(content, "&");
	else
		throw "Invalid entity reference.";

	if (elements.empty()) return;
	Element & element(elements.back());
	append_items(element, content);

	content.clear();
}

void
DocumentParser::ProcessDecodedUTF8 (
	char32_t character,
	bool decoder_error,
	bool /*overlong*/
) {
	if (decoder_error)
		throw "Invalid UTF-8 encoding.";
	switch (state) {
		case CONTENT:
			if ('<' == character) {
				flush_content();
				state = LESS_THAN;
			} else
			if ('&' == character) {
				flush_content();
				state = ENTITY;
			} else
			if (elements.empty()) {
				if (!IsWhitespace(character))
					throw "Non-whitespace is not allowed outwith the root element.";
			} else
				content += character;
			break;
		case ENTITY:
			if (';' == character) {
				flush_entity();
				state = CONTENT;
			} else
			if ('<' == character || IsWhitespace(character))
				throw "Unterminated entity.";
			else
				content += character;
			break;
		case LESS_THAN:
			if ('!' == character)
				state = DIRECTIVE_HEAD;
			else
			if ('?' == character)
				state = PROCESSOR_COMMAND_BODY;
			else
			if ('/' == character) {
				tag = elements.empty() ? Tag(true) : Tag(true, elements.back());
				state = TAG_NAME;
			} else
			if ('>' == character)
				state = CONTENT;
			else
			{
				tag = elements.empty() ? Tag(false) : Tag(false, elements.back());
				state = TAG_NAME;
				goto tag_name;
			}
			break;
		tag_name:
		case TAG_NAME:
			if ('>' == character) {
				flush_attribute();
				goto tag_closing;
			} else
			if ('/' == character) {
				flush_attribute();
				tag.closing = true;
				state = TAG_CLOSING;
			} else
			if ('=' == character)
				state = TAG_EQUALS;
			else
			if (IsWhitespace(character)) {
				flush_attribute();
				state = TAG_WHITESPACE;
			} else
				name += character;
			break;
		case TAG_EQUALS:
			if ('>' == character) {
				flush_attribute();
				goto tag_closing;
			} else
			if ('/' == character) {
				tag.closing = true;
				state = TAG_CLOSING;
			} else
			if ('\'' == character)
				state = TAG_VALUE_SINGLE;
			else
			if ('\"' == character)
				state = TAG_VALUE_DOUBLE;
			else
			if (IsWhitespace(character)) {
				flush_attribute();
				state = TAG_WHITESPACE;
			} else {
				state = TAG_VALUE_UNQUOTED;
				goto tag_value_unquoted;
			}
			break;
		tag_value_unquoted:
		case TAG_VALUE_UNQUOTED:
			if ('>' == character) {
				flush_attribute();
				goto tag_closing;
			} else
			if ('/' == character) {
				if (tag.closing) throw "Closing tags cannot be self-closing as well.";
				flush_attribute();
				tag.closing = true;
				state = TAG_CLOSING;
			} else
			if (IsWhitespace(character)) {
				flush_attribute();
				state = TAG_WHITESPACE;
			} else
				value += character;
			break;
		case TAG_VALUE_SINGLE:
			if ('\'' == character) {
				flush_attribute();
				state = TAG_WHITESPACE;
			} else
			{
				if (IsWhitespace(character))
					character = SPC;
				value += character;
			}
			break;
		case TAG_VALUE_DOUBLE:
			if ('\"' == character) {
				flush_attribute();
				state = TAG_WHITESPACE;
			} else
			{
				if (IsWhitespace(character))
					character = SPC;
				value += character;
			}
			break;
		case TAG_WHITESPACE:
			if ('>' == character)
				goto tag_closing;
			else
			if ('/' == character) {
				if (tag.closing) throw "Closing tags cannot be self-closing as well.";
				tag.closing = true;
				state = TAG_CLOSING;
			} else
			if (!IsWhitespace(character)) {
				state = TAG_NAME;
				goto tag_name;
			}
			break;
		tag_closing:
		case TAG_CLOSING:
			if ('>' == character) {
				flush_tag();
				state = CONTENT;
			} else
				throw "Nothing may follow tag closure except the end of the tag.";
			break;
		case DIRECTIVE_HEAD:
			if ('-' == character)
				state = DIRECTIVE_HEAD_DASH;
			else
				state = DIRECTIVE_BODY;
			break;
		case DIRECTIVE_HEAD_DASH:
			if ('-' == character)
				state = COMMENT_BODY;
			else
				state = DIRECTIVE_BODY;
			break;
		case DIRECTIVE_BODY:
			if ('>' == character)
				state = CONTENT;
			break;
		case COMMENT_BODY:
			if ('-' == character)
				state = COMMENT_DASH;
			break;
		case COMMENT_DASH:
			if ('-' == character)
				state = COMMENT_DASHDASH;
			else
				state = COMMENT_BODY;
			break;
		case COMMENT_DASHDASH:
			if ('>' == character)
				state = CONTENT;
			else
				state = COMMENT_BODY;
			break;
		case PROCESSOR_COMMAND_BODY:
			if ('?' == character)
				state = PROCESSOR_COMMAND_QUESTION;
			break;
		case PROCESSOR_COMMAND_QUESTION:
			if ('>' == character)
				state = CONTENT;
			else
			if ('?' != character)
				state = PROCESSOR_COMMAND_BODY;
			break;
	}
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
			const ColourPair pair(ColourPair(f.colour_index < 0 ? CharacterCell::colour_type::default_foreground : Map256Colour(f.colour_index), CharacterCell::colour_type::default_background));
			vio.PrintCharStrAttr(row, col, f.attr, pair, f.text.c_str(), f.text.length());
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
console_docbook_xml_viewer [[gnu::noreturn]]
(
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	TUIOutputBase::Options options;
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
		popt::definition * top_table[] = {
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
		DocumentParser parser(doc, columns);
		UTF8Decoder decoder(parser);

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
			unsigned long long line(1ULL);
			try {
				for (int c = std::fgetc(stdin); EOF != c; c = std::fgetc(stdin)) {
					decoder.Process(c);
					if (LF == c) ++line;
				}
				if (std::ferror(stdin)) throw std::strerror(errno);
			} catch (const char * s) {
				die_parser_error(prog, envs, "<stdin>", line, s);
			}
		} else
		{
			while (!args.empty()) {
				unsigned long long line(1ULL);
				const char * file(args.front());
				args.erase(args.begin());
				FileStar f(std::fopen(file, "r"));
				try {
					if (!f) throw std::strerror(errno);
					for (int c = std::fgetc(f); EOF != c; c = std::fgetc(f)) {
						decoder.Process(c);
						if (LF == c) ++line;
					}
					if (std::ferror(f)) throw std::strerror(errno);
				} catch (const char * s) {
					die_parser_error(prog, envs, file, line, s);
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
