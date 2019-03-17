/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <set>
#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <cerrno>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#include "ttyutils.h"
#include "ECMA48Output.h"
#include "TerminalCapabilities.h"
#include "ProcessEnvironment.h"

typedef std::set<unsigned long> sgr_params;

static inline
void
add (
	sgr_params & s,
	bool v,
	const popt::bool_string_definition & o,
	unsigned long on,
	unsigned long off
) {
	if (!o.is_set()) return;
	s.insert(v ? on : off);
	s.erase(v ? off : on);

}

namespace {
	typedef std::set<unsigned long> tabstops;

	struct colour_definition : public popt::integral_definition {
	public:
		colour_definition(char s, const char * l, const char * d) : integral_definition(s, l, a, d), v(), def(false) {}
		virtual ~colour_definition();
		const CharacterCell::colour_type & value() const { return v; }
		bool is_default() const { return def; }
	protected:
                static const char a[];
		virtual void action(popt::processor &, const char *);
		CharacterCell::colour_type v;
		bool def;
	};
	struct dec_locator_definition : public popt::integral_definition {
	public:
		dec_locator_definition(char s, const char * l, const char * d) : integral_definition(s, l, a, d), enabled(false), press(false), release(false) {}
		virtual ~dec_locator_definition();
		bool is_enabled() const { return enabled; }
		bool want_press() const { return press; }
		bool want_release() const { return release; }
	protected:
                static const char a[];
		virtual void action(popt::processor &, const char *);
		bool enabled, press, release;
	};
	struct xterm_mouse_definition : public popt::integral_definition {
	public:
		xterm_mouse_definition(char s, const char * l, const char * d) : integral_definition(s, l, a, d), v() {}
		virtual ~xterm_mouse_definition();
		unsigned value() const { return v; }
	protected:
                static const char a[];
		virtual void action(popt::processor &, const char *);
		unsigned v;
	};
	struct cursor_shape_definition : public popt::integral_definition {
	public:
		cursor_shape_definition(char s, const char * l, const char * d) : integral_definition(s, l, a, d), v(), def(false) {}
		virtual ~cursor_shape_definition();
		const CursorSprite::glyph_type & value() const { return v; }
		bool is_default() const { return def; }
	protected:
                static const char a[];
		virtual void action(popt::processor &, const char *);
		CursorSprite::glyph_type v;
		bool def;
	};
	struct underline_type_definition : public popt::integral_definition {
	public:
		underline_type_definition(char s, const char * l, const char * d) : integral_definition(s, l, a, d), v() {}
		virtual ~underline_type_definition();
		unsigned value() const { return v; }
	protected:
                static const char a[];
		virtual void action(popt::processor &, const char *);
		unsigned v;
	};
	struct erase_display_definition : public popt::integral_definition {
	public:
		erase_display_definition(char s, const char * l, const char * d) : integral_definition(s, l, a, d), v() {}
		virtual ~erase_display_definition();
		unsigned value() const { return v; }
	protected:
                static const char a[];
		virtual void action(popt::processor &, const char *);
		unsigned v;
	};
	struct tabstop_definition : public popt::integral_definition {
	public:
		tabstop_definition(char s, const char * l, const char * d) : integral_definition(s, l, a, d), v() {}
		virtual ~tabstop_definition();
		const tabstops & value() const { return v; }
	protected:
                static const char a[];
		virtual void action(popt::processor &, const char *);
		tabstops v;
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

const char colour_definition::a[] = "colour";
colour_definition::~colour_definition() {}
void colour_definition::action(popt::processor & /*proc*/, const char * text)
{
	if (0 == std::strcmp(text, "default")) {
		def = set = true;
		return;
	}
	def = false;
	const char * end(text);
	if ('#'== *text) {
		unsigned long rgb(std::strtoul(text + 1, const_cast<char **>(&end), 16));
		if (!*end && end != text + 1) {
			v = MapTrueColour((rgb >> 16) & 0xFF,(rgb >> 8) & 0xFF,(rgb >> 0) & 0xFF);
			set = true;
			return;
		}
	}
	unsigned long index(std::strtoul(text, const_cast<char **>(&end), 0));
	if (!*end && end != text) {
		v = Map256Colour(index);
		set = true;
		return;
	}
	for (const struct colourname * b(colournames), * e(colournames + sizeof colournames/sizeof *colournames), * p(b); e != p; ++p) {
		if (0 == std::strcmp(text, p->name)) {
			v = Map256Colour(p->index);
			set = true;
			return;
		}
	}
	throw popt::error(text, "colour specification is not {default|[bright] {red|green|blue|cyan|magenta|black|white|grey}|#<hexRGB>|<index>}");
}

const char dec_locator_definition::a[] = "mode";
dec_locator_definition::~dec_locator_definition() {}
void dec_locator_definition::action(popt::processor & /*proc*/, const char * text)
{
	if (0 == std::strcmp(text, "off")) {
		enabled = press = release = false;
		set = true;
	} else
	if (0 == std::strcmp(text, "press")) {
		release = false;
		enabled = press = set = true;
	} else
	if (0 == std::strcmp(text, "release")) {
		press = false;
		enabled = release = set = true;
	} else
	if (0 == std::strcmp(text, "all")) {
		enabled = press = release = set = true;
	} else
		throw popt::error(text, "DEC locator specification is not {off|press|release|all}");
}

const char xterm_mouse_definition::a[] = "mode";
xterm_mouse_definition::~xterm_mouse_definition() {}
void xterm_mouse_definition::action(popt::processor & /*proc*/, const char * text)
{
	if (0 == std::strcmp(text, "off")) {
		v = 0U;
		set = true;
	} else
	if (0 == std::strcmp(text, "click")) {
		v = 1U;
		set = true;
	} else
	if (0 == std::strcmp(text, "drag")) {
		v = 2U;
		set = true;
	} else
	if (0 == std::strcmp(text, "all")) {
		v = 3U;
		set = true;
	} else
		throw popt::error(text, "XTerm mouse specification is not {off|click|drag|all}");
}

const char cursor_shape_definition::a[] = "shape";
cursor_shape_definition::~cursor_shape_definition() {}
void cursor_shape_definition::action(popt::processor & /*proc*/, const char * text)
{
	if (0 == std::strcmp(text, "default")) {
		def = set = true;
		return;
	}
	def = false;
	if (0 == std::strcmp(text, "block")) {
		v = CursorSprite::BLOCK;
		set = true;
	} else
	if (0 == std::strcmp(text, "underline")) {
		v = CursorSprite::UNDERLINE;
		set = true;
	} else
	if (0 == std::strcmp(text, "star")) {
		v = CursorSprite::STAR;
		set = true;
	} else
	if (0 == std::strcmp(text, "box")) {
		v = CursorSprite::BOX;
		set = true;
	} else
	if (0 == std::strcmp(text, "bar")) {
		v = CursorSprite::BAR;
		set = true;
	} else
		throw popt::error(text, "cursor shape specification is not {default|block|underline|star|box|bar}");
}

const char underline_type_definition::a[] = "type";
underline_type_definition::~underline_type_definition() {}
void underline_type_definition::action(popt::processor & /*proc*/, const char * text)
{
	if (0 == std::strcmp(text, "single")) {
		v = 1U;
		set = true;
	} else
	if (0 == std::strcmp(text, "double")) {
		v = 2U;
		set = true;
	} else
	if (0 == std::strcmp(text, "curly")) {
		v = 3U;
		set = true;
	} else
	if (0 == std::strcmp(text, "dotted")) {
		v = 4U;
		set = true;
	} else
	if (0 == std::strcmp(text, "dashed")) {
		v = 5U;
		set = true;
	} else
		throw popt::error(text, "underline type specification is not {single|double|curly|dotted|dashed}");
}

const char erase_display_definition::a[] = "type";
erase_display_definition::~erase_display_definition() {}
void erase_display_definition::action(popt::processor & /*proc*/, const char * text)
{
	if (0 == std::strcmp(text, "rest")) {
		v = 0U;
		set = true;
	} else
	if (0 == std::strcmp(text, "all")) {
		v = 2U;
		set = true;
	} else
	if (0 == std::strcmp(text, "scrollback")) {
		v = 3U;		// xterm/PuTTY/Linux kernel extension
		set = true;
	} else
		throw popt::error(text, "erase type specification is not {rest|all|scrollback}");
}

const char tabstop_definition::a[] = "tabstops";
tabstop_definition::~tabstop_definition() {}
void tabstop_definition::action(popt::processor & /*proc*/, const char * text)
{
	v.clear();
	while (*text) {
		const char * end(text);
		const unsigned long n(std::strtoul(text, const_cast<char **>(&end), 0));
		if (end == text)
			throw popt::error(text, "tabstop specification is not a number");
		if (1U > n)
			throw popt::error(text, "tabstop column is less than 1");
		v.insert(n);
		if (',' != *end) {
			text = end;
			break;
		}
		text = end + 1;
	}
	if (*text)
		throw popt::error(text, "tabstop specification missing a comma");
	set = true;
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_control_sequence [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	TerminalCapabilities caps(envs);
	ECMA48Output o(caps, stdout, false /* C1 is not 7-bit aliased */);
	sgr_params sgr;
	bool bold, faint, italic, underline, blink, reverse, invisible, strikethrough;
	popt::bool_string_definition bold_option('\0', "bold", "Switch boldface on/off.", bold);
	popt::bool_string_definition faint_option('\0', "faint", "Switch faint on/off.", faint);
	popt::bool_string_definition italic_option('\0', "italic", "Switch italic on/off.", italic);
	popt::bool_string_definition underline_option('\0', "underline", "Switch underline on/off.", underline);
	popt::bool_string_definition blink_option('\0', "blink", "Switch blink on/off.", blink);
	popt::bool_string_definition reverse_option('\0', "reverse", "Switch reverse on/off.", reverse);
	popt::bool_string_definition invisible_option('\0', "invisible", "Switch invisible on/off.", invisible);
	popt::bool_string_definition strikethrough_option('\0', "strikethrough", "Switch strikethrough on/off.", strikethrough);
	bool cursor, cursorappkeys, calcappkeys, altbuffer, backspace_mode, delete_mode, bce, awm, scnm, irm, square, colm;
	popt::bool_string_definition cursor_option('\0', "cursor", "Switch cursor on/off.", cursor);
	popt::bool_string_definition cursorappkeys_option('\0', "appcursorkeys", "Switch cursor keypad application mode on/off.", cursorappkeys);
	popt::bool_string_definition calcappkeys_option('\0', "appcalckeys", "Switch calculator keypad application mode on/off.", calcappkeys);
	popt::bool_string_definition altbuffer_option('\0', "altbuffer", "Switch the XTerm alternate screen buffer on/off.", altbuffer);
	popt::bool_string_definition backspace_mode_option('\0', "backspace-is-bs", "Switch Backspace key is BS on/off.", backspace_mode);
	popt::bool_string_definition delete_mode_option('\0', "delete-is-del", "Switch Delete key is DEL on/off.", delete_mode);
	popt::bool_string_definition bce_option('\0', "bce", "Switch background colour erase on/off.", bce);
	popt::bool_string_definition awm_option('\0', "linewrap", "Switch automatic right margin on/off.", awm);
	popt::bool_string_definition scnm_option('\0', "inversescreen", "Switch inverse screen mode on/off.", scnm);
	popt::bool_string_definition irm_option('\0', "insert", "Switch insert (not overstrike) on/off.", irm);
	popt::bool_string_definition square_option('\0', "square", "Switch square (not obling) mode on/off.", square);
	popt::bool_string_definition colm_option('\0', "132-columns", "Switch 132-columns mode on/off.", colm);
	unsigned long regtabs, rows, columns;
	popt::unsigned_number_definition regtabs_option('\0', "regtabs", "interval", "Set regular tabs at the given interval.", regtabs, 0);
	popt::unsigned_number_definition rows_option('\0', "rows", "count", "Set the terminal height.", rows, 0);
	popt::unsigned_number_definition columns_option('\0', "columns", "count", "Set the terminal width.", columns, 0);
	bool reset(false), softreset(false), showtabs(false), notabs(false);
	colour_definition foreground_option('\0', "foreground", "Set the foreground colour.");
	colour_definition background_option('\0', "background", "Set the background colour.");
	dec_locator_definition dec_locator_option('\0', "dec-locator-reports", "Disable or set the type of DEC Locator reports.");
	xterm_mouse_definition xterm_mouse_option('\0', "xterm-mouse-reports", "Disable or set the type of XTerm mouse reports.");
	cursor_shape_definition cursor_shape_option('\0', "cursor-shape", "Set the shape of the cursor.");
	underline_type_definition underline_type_option('\0', "underline-type", "Set the type of underlining.");
	erase_display_definition erase_display_option('\0', "clear", "Erase the display.");
	tabstop_definition settabs_option('\0', "settabs", "Set tabstops at these columns.");
	tabstop_definition clrtabs_option('\0', "clrtabs", "Clear tabstops at these columns.");
	try {
		popt::bool_definition c1_7bit_option('7', "7bit", "Use 7-bit C1 characters.", o.c1_7bit);
		popt::bool_definition c1_8bit_option('8', "8bit", "Use 8-bit C1 characters instead of UTF-8.", o.c1_8bit);
		popt::bool_definition permit_fake_truecolour_option('\0', "permit-fake-truecolour", "Permit using 24-bit colour when the terminal is known to fake this ability.", TerminalCapabilities::permit_fake_truecolour);
		popt::bool_definition reset_option('\0', "reset", "Issue reset sequence.", reset);
		popt::bool_definition softreset_option('\0', "soft-reset", "Issue DEC soft reset sequence.", softreset);
		popt::bool_definition showtabs_option('\0', "showtabs", "Display all tabstops and a ruler.", showtabs);
		popt::bool_definition notabs_option('\0', "notabs", "Clear all tabstops.", notabs);
		popt::definition * ECMA48_table[] = {
			&reset_option,
			&softreset_option,
			&bold_option,
			&faint_option,
			&italic_option,
			&underline_option,
			&blink_option,
			&reverse_option,
			&invisible_option,
			&strikethrough_option,
			&foreground_option,
			&background_option,
			&showtabs_option,
			&notabs_option,
			&regtabs_option,
			&settabs_option,
			&clrtabs_option,
			&underline_type_option,
			&erase_display_option,
			&irm_option,
			&square_option,
		};
		popt::table_definition ECMA48_table_option(sizeof ECMA48_table/sizeof *ECMA48_table, ECMA48_table, "Standard ECMA-48 and ISO 8613-6 control sequences");
		popt::definition * DEC_table[] = {
			&rows_option,
			&columns_option,
			&cursor_option,
			&cursorappkeys_option,
			&calcappkeys_option,
			&altbuffer_option,
			&backspace_mode_option,
			&delete_mode_option,
			&bce_option,
			&awm_option,
			&scnm_option,
			&colm_option,
			&dec_locator_option,
			&xterm_mouse_option,
			&cursor_shape_option
		};
		popt::table_definition DEC_table_option(sizeof DEC_table/sizeof *DEC_table, DEC_table, "Control sequences private to DEC VTs and compatibles");
		popt::definition * top_table[] = {
			&c1_7bit_option,
			&c1_8bit_option,
			&permit_fake_truecolour_option,
			&ECMA48_table_option,
			&DEC_table_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	add(sgr, bold, bold_option, 1, 22);
	add(sgr, faint, faint_option, 2, 22);
	add(sgr, italic, italic_option, 3, 23);
	add(sgr, underline, underline_option, 4, 24);
	add(sgr, blink, blink_option, 5, 25);
	add(sgr, reverse, reverse_option, 7, 27);
	add(sgr, invisible, invisible_option, 8, 28);
	add(sgr, strikethrough, strikethrough_option, 9, 29);

	if (reset) {
		o.print_control_character(ESC);
		o.UTF8('c');
	}
	if (softreset) {
		if (caps.use_DECSTR)
			o.DECSTR();
	}
	if (caps.use_DECPrivateMode) {
		if (altbuffer_option.is_set())
			o.XTermAlternateScreenBuffer(altbuffer);
	}
	if (!sgr.empty()) {
		o.csi();
		for (sgr_params::const_iterator b(sgr.begin()), e(sgr.end()), p(b); e != p; ++p) {
			if (p != b) o.UTF8(';');
			std::fprintf(stdout, "%lu", *p);
			if (4U == *p && underline_type_option.is_set())
				std::fprintf(stdout, ":%u", underline_type_option.value());
		}
		o.UTF8('m');
	}
	if (foreground_option.is_set()) {
		if (foreground_option.is_default())
			o.SGRColour(true);
		else
			o.SGRColour(true, foreground_option.value());
	}
	if (background_option.is_set()) {
		if (background_option.is_default())
			o.SGRColour(false);
		else
			o.SGRColour(false, background_option.value());
	}
	if (erase_display_option.is_set()) {
		o.ED(erase_display_option.value());
		if (2U == erase_display_option.value())
			o.CUP();
	}
	if (cursor_shape_option.is_set()) {
		if (cursor_shape_option.is_default())
			o.SCUSR();
		else
			o.SCUSR(0, cursor_shape_option.value());
	}
	if (irm_option.is_set())
		o.IRM(irm);
	if (caps.use_DECPrivateMode) {
		if (square_option.is_set())
			o.SquareMode(square);
		if (colm_option.is_set())
			o.DECCOLM(colm);
		if (cursor_option.is_set())
			o.DECTCEM(cursor);
		if (cursorappkeys_option.is_set())
			o.DECCKM(cursorappkeys);
		if (calcappkeys_option.is_set())
			o.DECNKM(calcappkeys);
		if (backspace_mode_option.is_set())
			o.DECBKM(backspace_mode);
		if (delete_mode_option.is_set())
			o.XTermDeleteIsDEL(delete_mode);
		if (bce_option.is_set())
			o.DECECM(!bce);
		if (awm_option.is_set())
			o.DECAWM(awm);
		if (scnm_option.is_set())
			o.DECSCNM(scnm);
		if (dec_locator_option.is_set() && caps.use_DECLocator) {
			if (dec_locator_option.is_enabled()) {
				o.DECELR(true);
				o.DECSLE(true, dec_locator_option.want_press());
				o.DECSLE(false, dec_locator_option.want_release());
			} else
			{
				o.DECELR(false);
				o.DECSLE();
			}
		}
		if (xterm_mouse_option.is_set() && caps.has_XTerm1006Mouse) {
			switch (xterm_mouse_option.value()) {
				default:
				case 0U:	o.XTermSendNoMouseEvents(); break;
				case 1U:	o.XTermSendClickMouseEvents(); break;
				case 2U:	o.XTermSendClickDragMouseEvents(); break;
				case 3U:	o.XTermSendAnyMouseEvents(); break;
			}
		}
	}
	if (caps.has_DTTerm_DECSLPP_extensions && rows_option.is_set() && columns_option.is_set()) {
		o.DTTermResize(rows, columns);
		o.DECSTBM(0U, 0U);
		if (caps.use_DECSLRM) {
			if (caps.use_DECPrivateMode)
				o.DECSLRMM(true);
			o.DECSLRM(0U, 0U);
			if (caps.use_DECPrivateMode)
				o.DECSLRMM(false);
		}
	} else {
		if (rows_option.is_set()) {
			if (caps.use_DECSNLS)
				o.DECSNLS(rows);
			else {
				if (caps.has_DTTerm_DECSLPP_extensions) {
					if (rows < 25U) rows = 25U;
				}
				o.DECSLPP(rows);
			}
			o.DECSTBM(0U, 0U);
		}
		if (columns_option.is_set()) {
			if (caps.use_DECSCPP) {
				o.DECSCPP(columns);
				if (caps.use_DECSLRM) {
					if (caps.use_DECPrivateMode)
						o.DECSLRMM(true);
					o.DECSLRM(0U, 0U);
					if (caps.use_DECPrivateMode)
						o.DECSLRMM(false);
				}
			}
		}
	}
	if (regtabs_option.is_set()||notabs) {
		if (caps.reset_sets_tabs && (reset || softreset) && regtabs_option.is_set() && 8U == regtabs)
			/* This work is already done. */;
		else
		if (caps.use_CTC)
			o.CTC(5U);
		else
			o.TBC(3U);
	}
	if (regtabs_option.is_set()||showtabs||settabs_option.is_set()||clrtabs_option.is_set()) {
		// Fallback width per the util-linux setterm program.
		if (!columns_option.is_set()) columns = 160UL;
		// The COLUMNS environment variable takes precedence, per the Single UNIX Specification ("Environment variables").
		const char * c = envs.query("COLUMNS"), * s(c);
		if (c)
			columns = std::strtoul(c, const_cast<char **>(&c), 0);
		if (c == s || *c) {
			winsize size;
			if (0 <= tcgetwinsz_nointr(STDOUT_FILENO, size))
				columns = size.ws_col;
		}
		o.print_control_character(CR);
		if (regtabs_option.is_set()) {
			if (caps.reset_sets_tabs && (reset || softreset) && 8U == regtabs)
				/* This work is already done. */;
			else
			if (caps.use_DECST8C && 8U == regtabs)
				o.DECST8C();
			else {
				// Each new tabstop is set AFTER n more columns.
				for (unsigned long i(regtabs); i < columns; i += regtabs) {
					o.print_control_characters(' ', regtabs);
					if (caps.use_CTC)
						o.CTC(0U);
					else
						o.print_control_character(HTS);
				}
				o.print_control_character(CR);
			}
		}
		if (settabs_option.is_set()) {
			const tabstops & l(settabs_option.value());
			for (tabstops::const_iterator b(l.begin()), e(l.end()), p(b); e != p; ++p) {
				unsigned long n(*p);
				if (n > columns) continue;
				if (caps.use_HPA)
					o.HPA(n);
				else
					o.CHA(n);
				if (caps.use_CTC)
					o.CTC(0U);
				else
					o.print_control_character(HTS);
			}
			o.print_control_character(CR);
		}
		if (clrtabs_option.is_set()) {
			const tabstops & l(clrtabs_option.value());
			for (tabstops::const_iterator b(l.begin()), e(l.end()), p(b); e != p; ++p) {
				unsigned long n(*p);
				if (n > columns) continue;
				if (caps.use_HPA)
					o.HPA(n);
				else
					o.CHA(n);
				if (caps.use_CTC)
					o.CTC(0U);
				else
					o.TBC(0U);
			}
			o.print_control_character(CR);
		}
		if (showtabs) {
			for (unsigned i(0U); i < columns; ++i) {
				const unsigned u((i + 1U) % 10U);
				o.UTF8(5U == u ? '+' : 0U == u ? '0' + (((i + 1U) % 100U) / 10U) : '-');
			}
			o.newline();
			for (unsigned i(0U); i < columns; ++i) {
				o.print_control_character(TAB);
				o.UTF8('T');
				o.print_control_character(BS);
			}
			o.newline();
		}
	}

	throw EXIT_SUCCESS;
}
