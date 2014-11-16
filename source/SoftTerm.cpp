/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <stdint.h>
#include "CharacterCell.h"
#include "SoftTerm.h"

#include <iostream>	// for debugging

/* Constructor and destructor ***********************************************
// **************************************************************************
*/

SoftTerm::SoftTerm(SoftTerm::ScreenBuffer & s, SoftTerm::KeyboardBuffer & k) :
	screen(s),
	keyboard(k),
	x(0U), 
	y(0U),
	saved_x(0U), 
	saved_y(0U),
	w(80U), 
	h(25U),
	top_margin(0U),
	bottom_margin(24U),
	argc(0U),
	intermediate('\0'),
	state(NORMAL),
	scrolling(true),
	seen_arg_digit(false),
	overstrike(true),
	automatic_right_margin(true),
	advance_pending(false),
	attributes(0),
	foreground(255,255,255),
	background(0,0,0),
	cursor_type(CursorSprite::BOX),
	cursor_attributes(CursorSprite::VISIBLE|CursorSprite::BLINK)
{
	Resize(80U, 25U);
	ClearAllTabstops();
	UpdateCursorType();
	ProcessControlCharacter(FF);
}

SoftTerm::~SoftTerm()
{
	ProcessControlCharacter(FF);
}

/* Top-level control functions **********************************************
// **************************************************************************
*/

void 
SoftTerm::Resize(coordinate columns, coordinate rows)
{
	if (columns) w = columns;
	if (rows) h = rows;
	screen.SetSize(w, h);
	keyboard.ReportSize(w, h);
	top_margin = 0U;
	bottom_margin = h - 1U;
	if (x >= columns) x = columns - 1U;
	if (y >= rows) y = rows - 1U;
	UpdateCursorPos();
}

/* Control sequence arguments ***********************************************
// **************************************************************************
*/

static inline
SoftTerm::coordinate
OneIfZero (
	const SoftTerm::coordinate & v
) {
	return 0U == v ? 1U : v;
}

void 
SoftTerm::ResetArgs()
{
	argc = 0U;
	seen_arg_digit = false;
	args[argc] = 0;
	intermediate = '\0';
}

void 
SoftTerm::FinishArg(unsigned int d)
{
	if (!seen_arg_digit)
		args[argc] = d;
	if (argc >= sizeof args/sizeof *args - 1)
		for (size_t i(1U); i < argc; ++i)
			args[i - 1U] = args[i];
	else
		++argc;
	seen_arg_digit = false;
	args[argc] = 0;
}

SoftTerm::coordinate 
SoftTerm::SumArgs()
{
	coordinate s(0U);
	for (std::size_t i(0U); i < argc; ++i)
		s += args[i];
	return s;
}

/* Editing ******************************************************************
// **************************************************************************
*/

void 
SoftTerm::ClearDisplay(uint32_t c)
{
	const ScreenBuffer::coordinate l(ScreenBuffer::coordinate(w) * h);
	screen.WriteNCells(0, l, CharacterCell(c, attributes, foreground, background));
}

void 
SoftTerm::ClearLine()
{
	const ScreenBuffer::coordinate s(ScreenBuffer::coordinate(w) * y);
	screen.WriteNCells(s, w, CharacterCell(' ', attributes, foreground, background));
}

void 
SoftTerm::ClearToEOD()
{
	const ScreenBuffer::coordinate s(ScreenBuffer::coordinate(w) * y + x);
	const ScreenBuffer::coordinate l(ScreenBuffer::coordinate(w) * (h - 1U - y) + (w - x));
	screen.WriteNCells(s, l, CharacterCell(' ', attributes, foreground, background));
}

void 
SoftTerm::ClearToEOL()
{
	const ScreenBuffer::coordinate s(ScreenBuffer::coordinate(w) * y + x);
	screen.WriteNCells(s, w - x, CharacterCell(' ', attributes, foreground, background));
}

void 
SoftTerm::ClearFromBOD()
{
	const ScreenBuffer::coordinate l(ScreenBuffer::coordinate(w) * y + x);
	screen.WriteNCells(0, l, CharacterCell(' ', attributes, foreground, background));
}

void 
SoftTerm::ClearFromBOL()
{
	const ScreenBuffer::coordinate s(ScreenBuffer::coordinate(w) * y);
	screen.WriteNCells(s, x, CharacterCell(' ', attributes, foreground, background));
}

void 
SoftTerm::EraseCharacters(coordinate n) 
{
	const ScreenBuffer::coordinate s(ScreenBuffer::coordinate(w) * y + x);
	if (x + n >= w) n = w - 1U - x;
	screen.WriteNCells(s, n, CharacterCell(' ', attributes, foreground, background));
}

void 
SoftTerm::DeleteCharacters(coordinate n) 
{
	const ScreenBuffer::coordinate s(ScreenBuffer::coordinate(w) * y + x);
	const ScreenBuffer::coordinate e(ScreenBuffer::coordinate(w) * y + w);
	screen.ScrollUp(s, e, n, CharacterCell(' ', attributes, foreground, background));
}

void 
SoftTerm::InsertCharacters(coordinate n) 
{
	const ScreenBuffer::coordinate s(ScreenBuffer::coordinate(w) * y + x);
	const ScreenBuffer::coordinate e(ScreenBuffer::coordinate(w) * y + w);
	screen.ScrollDown(s, e, n, CharacterCell(' ', attributes, foreground, background));
}

void 
SoftTerm::DeleteLines(coordinate n) 
{
	const ScreenBuffer::coordinate s(ScreenBuffer::coordinate(w) * y);
	const ScreenBuffer::coordinate e(ScreenBuffer::coordinate(w) * bottom_margin + w);
	const ScreenBuffer::coordinate l(ScreenBuffer::coordinate(w) * n);
	screen.ScrollUp(s, e, l, CharacterCell(' ', attributes, foreground, background));
}

void 
SoftTerm::InsertLines(coordinate n) 
{
	const ScreenBuffer::coordinate s(ScreenBuffer::coordinate(w) * y);
	const ScreenBuffer::coordinate e(ScreenBuffer::coordinate(w) * bottom_margin + w);
	const ScreenBuffer::coordinate l(ScreenBuffer::coordinate(w) * n);
	screen.ScrollDown(s, e, l, CharacterCell(' ', attributes, foreground, background));
}

void 
SoftTerm::EraseInDisplay()
{
	for (std::size_t i(0U); i < argc; ++i) {
		switch (args[i]) {
			case 0:	ClearToEOD(); break;
			case 1:	ClearFromBOD(); break;
			case 2:	ClearDisplay(); break;
		}
	}
}

void 
SoftTerm::EraseInLine()
{
	for (std::size_t i(0U); i < argc; ++i) {
		switch (args[i]) {
			case 0:	ClearToEOL(); break;
			case 1:	ClearFromBOL(); break;
			case 2:	ClearLine(); break;
		}
	}
}

/* Tabulation ***************************************************************
// **************************************************************************
*/

void 
SoftTerm::TabClear()
{
	for (std::size_t i(0U); i < argc; ++i) {
		switch (args[i]) {
			case 0:	SetTabstopAt(x, false); break;
			case 3:	ClearAllTabstops(); break;
		}
	}
}

void 
SoftTerm::HorizontalTab(coordinate n)
{
	advance_pending = false;
	if (x + 1U < w && n) {
		do {
			if (IsTabstopAt(++x)) {
				if (!n) break;
				--n;
			}
		} while (x + 1U < w && n);
		UpdateCursorPos();
	}
}

void 
SoftTerm::BackwardsHorizontalTab(coordinate n)
{
	advance_pending = false;
	if (x > 0U && n) {
		do {
			if (IsTabstopAt(--x)) {
				if (!n) break;
				--n;
			}
		} while (x > 0U && n);
		UpdateCursorPos();
	}
}

void 
SoftTerm::SetTabstop() 
{
	SetTabstopAt(x, true);
}

void 
SoftTerm::ClearAllTabstops()
{
	for (unsigned short p(0U); p < 256U; ++p)
		SetTabstopAt(p, false);
}

/* Cursor motion ************************************************************
// **************************************************************************
*/

void 
SoftTerm::UpdateCursorPos()
{
	screen.SetCursorPos(x, y);
}

void 
SoftTerm::UpdateCursorType()
{
	screen.SetCursorType(cursor_type, cursor_attributes);
}

void 
SoftTerm::SaveCursor()
{
	saved_x = x;
	saved_y = y;
}

void 
SoftTerm::RestoreCursor()
{
	advance_pending = false;
	x = saved_x;
	y = saved_y;
	UpdateCursorPos();
}

bool 
SoftTerm::WillWrap() 
{
	return x >= w - 1U && automatic_right_margin;
}

void 
SoftTerm::Advance() 
{
	advance_pending = false;
	if (x < w - 1U) {
		++x;
		UpdateCursorPos();
	} else if (automatic_right_margin) {
		x = 0;
		if (y < h - 1U)
			++y;
		else if (scrolling)
			ScrollUp(1U);
		UpdateCursorPos();
	}
}

void 
SoftTerm::Index()
{
	advance_pending = false;
	coordinate n(1U);
	if (y > bottom_margin) {
		n += y - bottom_margin;
		y = bottom_margin;
		UpdateCursorPos();
	}
	if (y < bottom_margin) {
		const coordinate l(bottom_margin - y);
		if (l >= n) {
			y += n;
			n = 0U;
		} else {
			n -= l;
			y = bottom_margin;
		}
		UpdateCursorPos();
	}
	if (n > 0U) {
		if (scrolling)
			ScrollUp(n);
	}
}

void 
SoftTerm::ReverseIndex()
{
	advance_pending = false;
	coordinate n(1U);
	if (y < top_margin) {
		n += top_margin - y;
		y = top_margin;
		UpdateCursorPos();
	}
	if (y > top_margin) {
		if (y >= n) {
			y -= n;
			n = 0U;
		} else {
			n -= y;
			y = top_margin;
		}
		UpdateCursorPos();
	}
	if (n > 0U) {
		if (scrolling)
			ScrollDown(n);
	}
}

void 
SoftTerm::GotoX(coordinate n)
{
	advance_pending = false;
	x = n;
	UpdateCursorPos();
}

void 
SoftTerm::GotoY(coordinate n)
{
	advance_pending = false;
	y = n;
	UpdateCursorPos();
}

void 
SoftTerm::Home()
{
	advance_pending = false;
	x = y = 0U;
	UpdateCursorPos();
}

void 
SoftTerm::CarriageReturn()
{
	advance_pending = false;
	x = 0U;
	UpdateCursorPos();
}

void 
SoftTerm::GotoYX()
{
	advance_pending = false;
	y = argc > 0U ? OneIfZero(args[0U]) - 1U : 0U;
	x = argc > 1U ? OneIfZero(args[1U]) - 1U : 0U;
	UpdateCursorPos();
}

void 
SoftTerm::SetScrollMargins()
{
	coordinate new_top_margin(argc > 0U ? OneIfZero(args[0U]) - 1U : 0U);
	coordinate new_bottom_margin(argc > 1U && args[1U] > 0U ? args[1U] - 1U : h - 1U);
	if (new_top_margin < new_bottom_margin) {
		top_margin = new_top_margin;
		bottom_margin = new_bottom_margin;
		x = y = 0U;
		UpdateCursorPos();
	}
}

void 
SoftTerm::CursorUp(coordinate n)
{
	advance_pending = false;
	if (y < top_margin) {
		n += top_margin - y;
		y = top_margin;
		UpdateCursorPos();
	}
	if (y > top_margin) {
		if (y >= n) {
			y -= n;
			n = 0U;
		} else {
			n -= y;
			y = top_margin;
		}
		UpdateCursorPos();
	}
}

void 
SoftTerm::CursorDown(coordinate n)
{
	advance_pending = false;
	if (y > bottom_margin) {
		n += y - bottom_margin;
		y = bottom_margin;
		UpdateCursorPos();
	}
	if (y < bottom_margin) {
		const coordinate l(bottom_margin - y);
		if (l >= n) {
			y += n;
			n = 0U;
		} else {
			n -= l;
			y = bottom_margin;
		}
		UpdateCursorPos();
	}
}

void 
SoftTerm::CursorLeft(coordinate n)
{
	advance_pending = false;
	if (x > 0U) {
		if (x >= n) {
			x -= n;
			n = 0U;
		} else {
			n -= x;
			x = 0U;
		}
		UpdateCursorPos();
	}
}

void 
SoftTerm::CursorRight(coordinate n)
{
	advance_pending = false;
	if (x < w - 1U) {
		const coordinate l(w - 1U - x);
		if (l >= n) {
			x += n;
			n = 0U;
		} else {
			n -= l;
			x = w - 1U;
		}
		UpdateCursorPos();
	}
}

void 
SoftTerm::ScrollDown(coordinate c)
{
	const ScreenBuffer::coordinate s(ScreenBuffer::coordinate(w) * top_margin);
	const ScreenBuffer::coordinate e(ScreenBuffer::coordinate(w) * bottom_margin + w);
	const ScreenBuffer::coordinate l(ScreenBuffer::coordinate(w) * c);
	screen.ScrollDown(s, e, l, CharacterCell(' ', attributes, foreground, background));
}

void 
SoftTerm::ScrollUp(coordinate c)
{
	const ScreenBuffer::coordinate s(ScreenBuffer::coordinate(w) * top_margin);
	const ScreenBuffer::coordinate e(ScreenBuffer::coordinate(w) * (bottom_margin + 1U));
	const ScreenBuffer::coordinate l(ScreenBuffer::coordinate(w) * c);
	screen.ScrollUp(s, e, l, CharacterCell(' ', attributes, foreground, background));
}

/* Colours, modes, and attributes *******************************************
// **************************************************************************
*/

static 
CharacterCell::colour_type
Map256Colour (
	uint8_t c
) {
	if (c < 16U) {
		const uint8_t h((c & 8U)? 255U : 127U), b(c & 4U), g(c & 2U), r(c & 1U);
		return CharacterCell::colour_type(r ? h : 0U,g ? h : 0U,b ? h : 0U);
	} else if (c < 232U) {
		c -= 16U;
		uint8_t b(c % 6U), g((c / 6U) % 6U), r(c / 36U);
		if (r >= 4U) r += r - 3U;
		if (g >= 4U) g += g - 3U;
		if (b >= 4U) b += b - 3U;
		return CharacterCell::colour_type(r * 32U,g * 32U,b * 32U);
	} else {
		c -= 232U;
		if (c >= 16U) c += c - 15U;
		return CharacterCell::colour_type(c * 8U,c * 8U,c * 8U);
	}
}

void 
SoftTerm::SetAttributes()
{
	if (3 == argc && 38U == args[0] && 5U == args[1]) {
		foreground = Map256Colour(args[2] % 256U);
	} else
	if (3 == argc && 48U == args[0] && 5U == args[1]) {
		background = Map256Colour(args[2] % 256U);
	} else
	if (5 == argc && 38U == args[0] && 2U == args[1]) {
		foreground = CharacterCell::colour_type(args[2] % 256U,args[3] % 256U,args[4] % 256U);
	} else
	if (5 == argc && 48U == args[0] && 2U == args[1]) {
		background = CharacterCell::colour_type(args[2] % 256U,args[3] % 256U,args[4] % 256U);
	} else
	for (std::size_t i(0U); i < argc; ++i)
		SetAttribute (args[i]);
}

void 
SoftTerm::SetModes(bool flag)
{
	for (std::size_t i(0U); i < argc; ++i)
		switch (intermediate) {
			case '\0':	SetMode (args[i], flag); break;
			case '?':	SetPrivateMode (args[i], flag); break;
			default:	break;
		}
}

void 
SoftTerm::SetAttribute(unsigned int a)
{
	switch (a) {
		case 0U:	
			attributes = 0U; 
			foreground = CharacterCell::colour_type(255,255,255); 
			background = CharacterCell::colour_type(0,0,0); 
			break;
		case 1U:	attributes |= CharacterCell::BOLD; break;
		case 2U:	attributes |= CharacterCell::FAINT; break;
		case 3U:	attributes |= CharacterCell::ITALIC; break;
		case 4U:	attributes |= CharacterCell::UNDERLINE; break;
		case 5U:	attributes |= CharacterCell::BLINK; break;
		case 7U:	attributes |= CharacterCell::INVERSE; break;
		case 8U:	attributes |= CharacterCell::INVISIBLE; break;
		case 9U:	attributes |= CharacterCell::STRIKETHROUGH; break;
		case 22U:	attributes = 0U; break;
		case 23U:	attributes &= ~CharacterCell::ITALIC; break;
		case 24U:	attributes &= ~CharacterCell::UNDERLINE; break;
		case 25U:	attributes &= ~CharacterCell::BLINK; break;
		case 27U:	attributes &= ~CharacterCell::INVERSE; break;
		case 28U:	attributes &= ~CharacterCell::INVISIBLE; break;
		case 29U:	attributes &= ~CharacterCell::STRIKETHROUGH; break;
		case 30U:	foreground = CharacterCell::colour_type(0,0,0); break;
		case 31U:	foreground = CharacterCell::colour_type(255,0,0); break;
		case 32U:	foreground = CharacterCell::colour_type(0,255,0); break;
		case 33U:	foreground = CharacterCell::colour_type(255,255,0); break;
		case 34U:	foreground = CharacterCell::colour_type(0,0,255); break;
		case 35U:	foreground = CharacterCell::colour_type(255,0,255); break;
		case 36U:	foreground = CharacterCell::colour_type(0,255,255); break;
		case 37U:	foreground = CharacterCell::colour_type(255,255,255); break;
		case 39U:	foreground = CharacterCell::colour_type(255,255,255); break; // our chosen default is white
		case 40U:	background = CharacterCell::colour_type(0,0,0); break;
		case 41U:	background = CharacterCell::colour_type(255,0,0); break;
		case 42U:	background = CharacterCell::colour_type(0,255,0); break;
		case 43U:	background = CharacterCell::colour_type(255,255,0); break;
		case 44U:	background = CharacterCell::colour_type(0,0,255); break;
		case 45U:	background = CharacterCell::colour_type(255,0,255); break;
		case 46U:	background = CharacterCell::colour_type(0,255,255); break;
		case 47U:	background = CharacterCell::colour_type(255,255,255); break;
		case 49U:	background = CharacterCell::colour_type(0,0,0); break; // our chosen default is black
		default:
			std::clog << "Unknown attribute : " << a << "\n";
			break;
	}
}

void 
SoftTerm::SaveAttributes()
{
	saved_attributes = attributes;
	saved_foreground = foreground;
	saved_background = background;
}

void 
SoftTerm::RestoreAttributes()
{
	attributes = saved_attributes;
	foreground = saved_foreground;
	background = saved_background;
}

void 
SoftTerm::SetMode(unsigned int a, bool f)
{
	switch (a) {
		case 4U:	overstrike = !f; break;
		default:
			std::clog << "Unknown mode : " << a << "\n";
			break;
	}
}

void 
SoftTerm::SetPrivateMode(unsigned int a, bool f)
{
	switch (a) {
		case 3U:
			Resize(f ? 132U : 80U, 25U);
			ProcessControlCharacter(FF);
			break;
		case 7U:	automatic_right_margin = f; break;
		case 12U:
			if (f) 
				cursor_attributes |= CursorSprite::BLINK;
			else
				cursor_attributes &= ~CursorSprite::BLINK;
			UpdateCursorType();
			break;
		case 25U:
			if (f) 
				cursor_attributes |= CursorSprite::VISIBLE;
			else
				cursor_attributes &= ~CursorSprite::VISIBLE;
			UpdateCursorType();
			break;
		default:
			std::clog << "Unknown private mode : " << a << "\n";
			break;
	}
}

void 
SoftTerm::SendDeviceAttributes()
{
	for (std::size_t i(0U); i < argc; ++i)
		switch (intermediate) {
			case '\0':	SendDeviceAttribute (args[i]); break;
			case '>':	SendPrivateDeviceAttribute (args[i]); break;
			default:	break;
		}
}

static 
const char 
vt220_device_attribute[] = 
	"\x1B" "["	// CSI equivalent that more softwares will recognize
	"?"		// DEC private
	"62" 		// VT220
	";" 
	"1" 		// ... with 132-column support
	";" 
	"22" 		// ... with ANSI colour capability
	"c";

void 
SoftTerm::SendDeviceAttribute(unsigned int a)
{
	switch (a) {
		case 0U:	keyboard.WriteLatin1Characters(sizeof vt220_device_attribute - 1, vt220_device_attribute); break;
		default:
			std::clog << "Unknown device attribute request : " << a << "\n";
			break;
	}
}

static 
const char 
vt220_device_private_attribute[] = 
	"\x1B" "["	// CSI equivalent that more softwares will recognize
	">"		// DEC private
	"1" 		// VT220
	";" 
	"0" 		// firmware version number
	";" 
	"0" 		// ROM cartridge registration number
	"c";

void 
SoftTerm::SendPrivateDeviceAttribute(unsigned int a)
{
	switch (a) {
		case 0U:	keyboard.WriteLatin1Characters(sizeof vt220_device_private_attribute - 1, vt220_device_private_attribute); break;
		default:
			std::clog << "Unknown device attribute request : " << a << "\n";
			break;
	}
}

/* Top-level output functions ***********************************************
// **************************************************************************
*/

inline
bool 
SoftTerm::IsControl(uint32_t character)
{
	return (character < 0x20) || (character >= 0x80 && character < 0xA0);
}

inline
bool 
SoftTerm::IsIntermediate(uint32_t character)
{
	return character >= 0x20 && character < 0x30;
}

void 
SoftTerm::Write(uint32_t character, bool decoder_error, bool overlong)
{
	switch (state) {
		case NORMAL:
			if (decoder_error || overlong)
				Print(character);
			else if (IsControl(character))
				ProcessControlCharacter(character);
			else
				Print(character); 
			break;
		case ESCAPE1:
			if (decoder_error)
				state = NORMAL;
			else if (overlong) {
				state = NORMAL;
				Print(character);
			// Handling control characters even in the middle of ESC or CSI sequences is a quirk of the Linux VT system that we duplicate.
			} else if (IsControl(character))
				ProcessControlCharacter(character);
			else
				Escape1(character);
			break;
		case ESCAPE2:
			if (decoder_error)
				state = NORMAL;
			else if (overlong) {
				state = NORMAL;
				Print(character);
			// Handling control characters even in the middle of ESC or CSI sequences is a quirk of the Linux VT system that we duplicate.
			} else if (IsControl(character))
				ProcessControlCharacter(character);
			else
				Escape2(character);
			break;
		case CONTROL:
			if (decoder_error || overlong)
				state = NORMAL;
			else if (overlong) {
				state = NORMAL;
				Print(character);
			// Handling control characters even in the middle of ESC or CSI sequences is a quirk of the Linux VT system that we duplicate.
			} else if (IsControl(character))
				ProcessControlCharacter(character);
			else
				ControlSequence(character);
			break;
	}
}

void 
SoftTerm::ProcessControlCharacter(uint32_t character)
{
	switch (character) {
		case NUL:	break;
		case FF:	Home(); ClearDisplay(); break;
		case BEL:	/* TODO: bell */ std::clog << "TODO: bell\n"; break;
		case CR:	CarriageReturn(); break;
		case NEL:	CarriageReturn(); Index(); break;
		case IND: case LF: case VT:
				Index(); break;
		case RI:
				ReverseIndex(); break;
		case TAB:	HorizontalTab(1U); break;
		case BS:	CursorLeft(1U); break;
		case DEL:	DeleteCharacters(1U); break;
		case HTS:	SetTabstop(); break;
		case ESC:	intermediate = '\0'; state = ESCAPE1; break;
		case CSI:	state = CONTROL; ResetArgs(); break;
		case CAN:	state = NORMAL; break;
		case SS2: case SS3: case DCS: case SOS: case ST: case OSC: case PM: case APC:
				break;	// explicitly unsupported control characters
		default:	break;
	}
}

void 
SoftTerm::Escape1(uint32_t character)
{
	if (character >= 0x40 && character <= 0x5f) {
		state = NORMAL;		// Do this first, so that it can be overridden.
		// This is known as "7-bit code extension" and is defined for the entire range.
		ProcessControlCharacter(character + 0x40); 
	} else if (IsIntermediate(character)) {
		intermediate = static_cast<char>(character);
		state = ESCAPE2;
	} else switch (character) {
		default:	state = NORMAL; break;
		case '6':	ReverseIndex(); state = NORMAL; break;
		case '7':	SaveCursor(); SaveAttributes(); state = NORMAL; break;
		case '8':	RestoreCursor(); RestoreAttributes(); state = NORMAL; break;
		case '9':	Index(); state = NORMAL; break;
		case 'c':	/* TODO: RIS */ std::clog << "TODO: RIS\n"; state = NORMAL; break;
	}
}

void 
SoftTerm::Escape2(uint32_t character)
{
	if (IsIntermediate(character)) {
		intermediate = static_cast<char>(character);
	} else {
		switch (intermediate) {
			default:	break;
			case '#':
				switch (character) {
					default:	break;
					case '8':	ClearDisplay('E'); break;
				}
				break;
		}
		state = NORMAL;
	}
}

void 
SoftTerm::ControlSequence(uint32_t character)
{
	switch (character) {
		// Accumulate digits in arguments.
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			seen_arg_digit = true;
			args[argc] = args[argc] * 10U + (character - '0');
			return;
		// There is a limited range of intermediate characters that we accept.
		case '?': case '!': case '>':
			if (0 == argc && !seen_arg_digit) {
				intermediate = static_cast<char>(character);
				return;
			}
			break;
		// Arguments may be semi-colon separated.
		case ';':
			FinishArg(0U); 
			return;

	}
	// Finish the final argument, using the relevant defaults.
	switch (character) {
		case 'c':
		case 'g':
		case 'm':
		case 'J':
		case 'K':
			FinishArg(0U); 
			break;
		case 'H':
		case 'f':
			FinishArg(1U); 
			if (argc < 2U) FinishArg(1U); 
			break;
		case 'r':
			FinishArg(argc < 1U ? 1U : h); 
			FinishArg(argc < 2U ? h : 0U); 
			break;
		default:	
			FinishArg(1U); 
			break;
	}
	// Enact the action.
	switch (character) {
		case '@':	InsertCharacters(OneIfZero(SumArgs())); break;
		case 'A':	CursorUp(OneIfZero(SumArgs())); break;
		case 'B':	CursorDown(OneIfZero(SumArgs())); break;
		case 'C':	CursorRight(OneIfZero(SumArgs())); break;
		case 'D':	CursorLeft(OneIfZero(SumArgs())); break;
		case 'E':	x = 0; CursorDown(OneIfZero(SumArgs())); break;
		case 'F':	x = 0; CursorUp(OneIfZero(SumArgs())); break;
		case 'G':	GotoX(OneIfZero(SumArgs())); break;
		case 'H':	GotoYX(); break;
		case 'J':	EraseInDisplay(); break;
		case 'K':	EraseInLine(); break;
		case 'L':	InsertLines(OneIfZero(SumArgs())); break;
		case 'M':	DeleteLines(OneIfZero(SumArgs())); break;
		case 'P':	DeleteCharacters(OneIfZero(SumArgs())); break;
		case 'X':	EraseCharacters(OneIfZero(SumArgs())); break;
		case 'S':	ScrollUp(OneIfZero(SumArgs())); break;
		case 'T':	ScrollDown(OneIfZero(SumArgs())); break;
		case '`':	GotoX(OneIfZero(SumArgs())); break;
		case 'a':	CursorRight(OneIfZero(SumArgs())); break;
		case 'd':	GotoY(OneIfZero(SumArgs())); break;
		case 'e':	CursorDown(OneIfZero(SumArgs())); break;
		case 'f':	GotoYX(); break;
		case 'g':	TabClear(); break;
		case 'm':	SetAttributes(); break;
		case 'r':	SetScrollMargins(); break;
		case 's':	SaveCursor(); break;
		case 'u':	RestoreCursor(); break;
		case 'x':	/* TODO: Set colours */ std::clog << "TODO: Set colours\n"; break;
		case 'Z':	BackwardsHorizontalTab(OneIfZero(SumArgs())); break;
		case 'I':	HorizontalTab(OneIfZero(SumArgs())); break;
		case 'c':	SendDeviceAttributes(); break;
		case 'l':	SetModes(false); break;
		case 'h':	SetModes(true); break;
		default:	
			std::clog << "Unknown CSI terminator : " << character << "\n";
			break;
	}
	state = NORMAL; 
}

static inline
bool
is_combining (
	uint32_t /*character*/
) {
	return false;	// FIXME: This isn't right.
}

void 
SoftTerm::Print(uint32_t character)
{
	if (advance_pending) {
 		if (WillWrap()) Advance();
 		advance_pending = false;
	}
	/// FIXME: \bug
	/// This needs a lot more attention.
	/// Combining characters don't combine and we don't handle bidirectional printing.
	const ScreenBuffer::coordinate s(ScreenBuffer::coordinate(w) * y + x);
	if (is_combining(character)) {
		screen.WriteNCells(s, 1U, CharacterCell(character, attributes, foreground, background));
	} else {
		if (!overstrike) InsertCharacters(1U);
		screen.WriteNCells(s, 1U, CharacterCell(character, attributes, foreground, background));
		advance_pending = WillWrap();
		if (!advance_pending) Advance();
	}
}
