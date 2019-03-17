/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <map>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/ioctl.h>	// for struct winsize
#include <sys/stat.h>
#include <unistd.h>
#include "kqueue_common.h"
#include "popt.h"
#include "utils.h"
#include "fdutils.h"
#include "ttyutils.h"
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

/* Full-screen TUI **********************************************************
// **************************************************************************
*/

namespace {

typedef std::basic_string<uint32_t> u32string;

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
	DisplayItem(CharacterCell::attribute_type a, uint_fast8_t c, const u32string & s) : attr(a), colour(c), text(s) {}
	DisplayItem(CharacterCell::attribute_type a, uint_fast8_t c, const u32string::const_iterator & b, const u32string::const_iterator & e) : attr(a), colour(c), text(b, e) {}
	CharacterCell::attribute_type attr;
	uint_fast8_t colour;
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
	CharacterCell::attribute_type query_attr() const { return attr; }
	uint_fast8_t query_colour() const { return colour; }
	bool matches_attribute(const u32string & name, const char * value) const;
	Element(const Tag & t);
	bool has_children;
protected:
	bool element_content, literal, block, margin, indent, outdent;
	uint_fast8_t colour;
	CharacterCell::attribute_type attr;
};

struct DocumentParser : public UTF8Decoder::UCS32CharacterSink {
	DocumentParser(DisplayDocument & d, unsigned long c) : doc(d), columns(c), state(CONTENT), current_line(0), content(), name(), value(), tag(false) {}
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

	virtual void ProcessDecodedUTF8(uint32_t character, bool decoder_error, bool overlong);
	void flush_content();
	void flush_entity();
	void flush_attribute();
	void flush_tag();
	void append_item_pre(CharacterCell::attribute_type a, uint_fast8_t c, const u32string::const_iterator & b, const u32string::const_iterator & e);
	void append_item_wrap(CharacterCell::attribute_type a, uint_fast8_t c, const u32string::const_iterator & b, const u32string::const_iterator & e);
	void append_items_pre(CharacterCell::attribute_type a, uint_fast8_t c, const u32string & s);
	void append_items_wrap(CharacterCell::attribute_type a, uint_fast8_t c, const u32string & s);
	void append_items(const Element & e, CharacterCell::attribute_type a, const u32string & s);
	void append_items(const Element & e, const u32string & s);
	void indent();
};

struct TUI :
	public TerminalCapabilities,
	public TUIOutputBase,
	public TUIInputBase
{
	TUI(ProcessEnvironment & e, DisplayDocument & m, TUIDisplayCompositor & c, FILE * tty, unsigned long, bool, bool, bool);
	~TUI();

	bool quit_flagged() const { return pending_quit_event; }
	bool immediate_update() const { return immediate_update_needed; }
	bool exit_signalled() const { return terminate_signalled||interrupt_signalled||hangup_signalled; }
	void handle_signal (int);
	void handle_control (int, int);
	void handle_update_event () { immediate_update_needed = false; TUIOutputBase::handle_update_event(); }

protected:
	static const CharacterCell::colour_type black;
	sig_atomic_t terminate_signalled, interrupt_signalled, hangup_signalled, usr1_signalled, usr2_signalled;
	TUIVIO vio;
	bool pending_quit_event, immediate_update_needed;
	TUIDisplayCompositor::coordinate window_y, window_x;
	unsigned long columns;
	DisplayDocument & doc;
	std::size_t current_row, current_col;

	virtual void redraw_new();
	void set_refresh_and_immediate_update_needed () { immediate_update_needed = true; TUIOutputBase::set_refresh_needed(); }

	virtual void ExtendedKey(uint_fast16_t k, uint_fast8_t m);
	virtual void FunctionKey(uint_fast16_t k, uint_fast8_t m);
	virtual void UCS3(uint_fast32_t character);
	virtual void Accelerator(uint_fast32_t character);
	virtual void MouseMove(uint_fast16_t, uint_fast16_t, uint8_t);
	virtual void MouseWheel(uint_fast8_t n, int_fast8_t v, uint_fast8_t m);
	virtual void MouseButton(uint_fast8_t n, uint_fast8_t v, uint_fast8_t m);
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
	element_content(false),
	literal(t.literal),
	block(false),
	margin(false),
	indent(false),
	outdent(false),
	colour(COLOUR_WHITE),
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
		Equals(name, "refsynopsisdiv") ||
		Equals(name, "refnamediv") ||
		Equals(name, "listitem") ||
		false
	;

	// Attributes

	const bool bold =
		Equals(name, "refentrytitle") ||
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
		colour = COLOUR_MAGENTA;
	else
	if (Equals(name, "arg"))
		colour = COLOUR_CYAN;
	else
	if (Equals(name, "command")
	||  Equals(name, "function")
	||  Equals(name, "class")
	)
		colour = COLOUR_GREEN;
	else
	if (Equals(name, "keycap")
	||  Equals(name, "envar")
	)
		colour = COLOUR_YELLOW;
	else
	if (Equals(name, "userinput"))
		colour = COLOUR_DARK_VIOLET;
	else
	if (Equals(name, "warning")
	||  Equals(name, "caution")
	)
		colour = COLOUR_DARK_ORANGE3;
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

void
DocumentParser::indent()
{
	std::size_t spaces(0U);
	for (Elements::iterator ep(elements.begin()), ee(elements.end()); ee != ep; ++ep) {
		if (ep->query_indent() && spaces + 2U < columns)
			spaces += 2U;
		if (ep->query_outdent() && spaces >= 2U)
			spaces -= 2U;
	}
	if (spaces)
		current_line->push_back(DisplayItem(0, COLOUR_WHITE, u32string(spaces, SPC)));
}

void
DocumentParser::append_item_pre(
	CharacterCell::attribute_type a,
	uint_fast8_t c,
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
	uint_fast8_t c,
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
	uint_fast8_t c,
	const u32string & s
) {
	u32string::const_iterator b(s.begin()), p(b);
	for (u32string::const_iterator e(s.end()); p != e; ++p) {
		const uint32_t character(*p);
		if (IsNewline(character)) {
			append_item_pre(a, c, b, p);
			b = p + 1;
			current_line = 0;
		}
	}
	append_item_pre(a, c, b, p);
}

void
DocumentParser::append_items_wrap(
	CharacterCell::attribute_type a,
	uint_fast8_t c,
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
		append_items_pre(a, e.query_colour(), s);
	else
		append_items_wrap(a, e.query_colour(), s);
}

void
DocumentParser::append_items(
	const Element & e,
	const u32string & s
) {
	if (e.query_literal())
		append_items_pre(e.query_attr(), e.query_colour(), s);
	else
		append_items_wrap(e.query_attr(), e.query_colour(), s);
}

namespace {
	const u32string choice(Make("choice"));
	const u32string repeat(Make("repeat"));
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
			current_line = 0;
			append_items(element, CharacterCell::INVERSE, s);
			doc.append_line();
			current_line = 0;
		}
		if (Equals(tag.name, "refsynopsisdiv")) {
			u32string s;
			Copy(s, "Synopsis");
			current_line = 0;
			append_items(element, CharacterCell::INVERSE, s);
			doc.append_line();
			current_line = 0;
		}

		if (element.query_block())
			current_line = 0;

		// inline prefixes
		if (Equals(tag.name, "refpurpose")) {
			u32string s;
			Copy(s, " - ");
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
		|| Equals(tag.name, "group")
		) {
			if (element.matches_attribute(choice, "opt")) {
				u32string s(1U, '[');
				append_items(element, s);
			}
			if (element.matches_attribute(choice, "req")) {
				u32string s(1U, '{');
				append_items(element, s);
			}
		}
		if (Equals(tag.name, "note")) {
			u32string s;
			Copy(s, "Note: ");
			append_items(element, s);
		}
		if (Equals(tag.name, "tip")) {
			u32string s;
			Copy(s, "Tip: ");
			append_items(element, s);
		}
		if (Equals(tag.name, "important")) {
			u32string s;
			Copy(s, "Important: ");
			append_items(element, s);
		}

		if (!elements.empty())
			elements.back().has_children = true;
		elements.push_back(element);
	}
	if (tag.closing) {
		if (elements.empty())
			throw "No opening tag.";
		Element & element(elements.back());
		if (element.name != tag.name)
			throw "Mismatched closing tag.";

		// inline suffixes
		if (Equals(tag.name, "manvolnum")) {
			u32string s(1U, ')');
			append_items(element, s);
		}
		if (Equals(tag.name, "quote")) {
			u32string s(1U, 0x2019);
			append_items(element, s);
		}
		if (Equals(tag.name, "term")) {
			u32string s(1U, ',');
			append_items(element, s);
		}
		if (Equals(tag.name, "arg")
		|| Equals(tag.name, "group")
		) {
			if (element.matches_attribute(choice, "opt")) {
				u32string s(1U, ']');
				append_items(element, s);
			}
			if (element.matches_attribute(choice, "req")) {
				u32string s(1U, '}');
				append_items(element, s);
			}
			if (element.matches_attribute(repeat, "rep")) {
				u32string s(1U, 0x2026);
				append_items(element, s);
			}
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
			append_items_pre(element.query_attr(), element.query_colour(), content);
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
		append_items_wrap(element.query_attr(), element.query_colour(), content);
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
	uint32_t character,
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
	FILE * tty,
	unsigned long c,
	bool cursor_application_mode,
	bool calculator_application_mode,
	bool alternate_screen_buffer
) :
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
	columns(c),
	doc(d),
	current_row(0U),
	current_col(0U)
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

const CharacterCell::colour_type TUI::black(Map256Colour(COLOUR_BLACK));

void
TUI::redraw_new (
) {
	TUIDisplayCompositor::coordinate y(current_row), x(current_col);
	// The window includes the cursor position.
	if (window_y > y) window_y = y; else if (window_y + c.query_h() <= y) window_y = y - c.query_h() + 1;
	if (window_x > x) window_x = x; else if (window_x + c.query_w() <= x) window_x = x - c.query_w() + 1;

	erase_new_to_backdrop();

	std::size_t nr(0U);
	for (DisplayDocument::const_iterator rb(doc.begin()), re(doc.end()), ri(rb); re != ri; ++ri, ++nr) {
		if (nr < window_y) continue;
		long row(nr - window_y);
		if (row >= c.query_h()) break;
		const DisplayLine & r(*ri);
		long col(-window_x);
		for (DisplayLine::const_iterator fb(r.begin()), fe(r.end()), fi(fb); fe != fi; ++fi) {
			const DisplayItem & f(*fi);
			const ColourPair pair(ColourPair(Map256Colour(f.colour), black));
			vio.Print(row, col, f.attr, pair, f.text.c_str(), f.text.length());
		}
	}

	c.move_cursor(y - window_y, x - window_x);
	c.set_cursor_state(CursorSprite::BLINK|CursorSprite::VISIBLE, CursorSprite::BOX);
}

void
TUI::ExtendedKey(
	uint_fast16_t k,
	uint_fast8_t /*m*/
) {
	switch (k) {
		case EXTENDED_KEY_LEFT_ARROW:
		case EXTENDED_KEY_PAD_LEFT:
			if (current_col > 0) { --current_col; set_refresh_and_immediate_update_needed(); }
			break;
		case EXTENDED_KEY_RIGHT_ARROW:
		case EXTENDED_KEY_PAD_RIGHT:
			if (current_col + 1 < columns) { ++current_col; set_refresh_and_immediate_update_needed(); }
			break;
		case EXTENDED_KEY_DOWN_ARROW:
		case EXTENDED_KEY_PAD_DOWN:
			if (current_row + 1 < doc.size()) { ++current_row; set_refresh_and_immediate_update_needed(); }
			break;
		case EXTENDED_KEY_UP_ARROW:
		case EXTENDED_KEY_PAD_UP:
			if (current_row > 0) { --current_row; set_refresh_and_immediate_update_needed(); }
			break;
		case EXTENDED_KEY_END:
		case EXTENDED_KEY_PAD_END:
			if (std::size_t s = doc.size()) {
				if (current_row + 1 != s) { current_row = s - 1; set_refresh_and_immediate_update_needed(); }
				break;
			} else 
				[[clang::fallthrough]];
		case EXTENDED_KEY_HOME:
		case EXTENDED_KEY_PAD_HOME:
			if (current_row != 0) { current_row = 0U; set_refresh_and_immediate_update_needed(); }
			break;
		case EXTENDED_KEY_PAGE_DOWN:
		case EXTENDED_KEY_PAD_PAGE_DOWN:
			if (doc.size() && current_row + 1 < doc.size()) {
				unsigned n(c.query_h());
				if (current_row + n < doc.size())
					current_row += n;
				else
					current_row = doc.size() - 1;
				set_refresh_and_immediate_update_needed();
			}
			break;
		case EXTENDED_KEY_PAGE_UP:
		case EXTENDED_KEY_PAD_PAGE_UP:
			if (current_row > 0) {
				unsigned n(c.query_h());
				if (current_row > n)
					current_row -= n;
				else
					current_row = 0;
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

namespace {
	inline
	unsigned long
	get_columns (
		const ProcessEnvironment & envs,
		int fd
	) {
		unsigned long columns(80UL);
		// The COLUMNS environment variable takes precedence, per the Single UNIX Specification ("Environment variables").
		const char * c = envs.query("COLUMNS"), * s(c);
		if (c)
			columns = std::strtoul(c, const_cast<char **>(&c), 0);
		if (c == s || *c) {
			winsize size;
			if (0 <= tcgetwinsz_nointr(fd, size))
				columns = size.ws_col;
			else
				columns = 80UL;
		}
		return columns;
	}
}

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
	bool cursor_application_mode(false);
	bool calculator_application_mode(false);
	bool no_alternate_screen_buffer(false);
	try {
		popt::bool_definition cursor_application_mode_option('\0', "cursor-keypad-application-mode", "Set the cursor keypad to application mode instead of normal mode.", cursor_application_mode);
		popt::bool_definition calculator_application_mode_option('\0', "calculator-keypad-application-mode", "Set the calculator keypad to application mode instead of normal mode.", calculator_application_mode);
		popt::bool_definition no_alternate_screen_buffer_option('\0', "no-alternate-screen-buffer", "Prevent switching to the XTerm alternate screen buffer.", no_alternate_screen_buffer);
		popt::definition * top_table[] = {
			&cursor_application_mode_option,
			&calculator_application_mode_option,
			&no_alternate_screen_buffer_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{file(s)...}");

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

	const char * tty(envs.query("TTY"));
	if (!tty) tty = "/dev/tty";
	FileStar control(std::fopen(tty, "w+"));
	if (!control) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, tty, std::strerror(error));
		throw EXIT_FAILURE;
	}

	const unsigned long columns(get_columns(envs, fileno(control)));

	DisplayDocument doc;

	{
		DocumentParser parser(doc, columns);
		UTF8Decoder decoder(parser);

		if (args.empty()) {
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
			unsigned long long line(1ULL);
			try {
				for (int c = std::fgetc(stdin); EOF != c; c = std::fgetc(stdin)) {
					decoder.Process(c);
					if (LF == c) ++line;
				}
				if (std::ferror(stdin)) throw std::strerror(errno);
			} catch (const char * s) {
				std::fprintf(stderr, "%s: FATAL: %s(%llu): %s\n", prog, "<stdin>", line, s);
				throw EXIT_FAILURE;
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
					std::fprintf(stderr, "%s: FATAL: %s(%llu): %s\n", prog, file, line, s);
					throw EXIT_FAILURE;
				}
			}
		}
	}

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}
	std::vector<struct kevent> ip;

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

	TUIDisplayCompositor compositor(false /* no software cursor */, 24, 80);
	TUI ui(envs, doc, compositor, control, columns, cursor_application_mode, calculator_application_mode, !no_alternate_screen_buffer);

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
					}
					break;
				}
			}
		}
	}

	throw EXIT_SUCCESS;
}
