/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _XOPEN_SOURCE_EXTENDED
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "ttyutils.h"
#include "utils.h"
#include "TerminalCapabilities.h"
#include "InputMessage.h"
#include "ControlCharacters.h"
#include "TUIInputBase.h"

namespace {

inline
unsigned int
OneIfZero (
	const unsigned int v
) {
	return 0U == v ? 1U : v;
}

inline uint_fast8_t TranslateDECModifiers(unsigned int n) { return n - 1U; }
inline unsigned int TranslateDECCoordinates(unsigned int n) { return n - 1U; }

}

/* The base class ***********************************************************
// **************************************************************************
*/

TUIInputBase::TUIInputBase(
	const TerminalCapabilities & c,
	FILE * f
) :
	caps(c),
	utf8_decoder(*this),
	ecma48_decoder(*this, false /* no control strings */, false /* no cancel */, false /* no 7-bit extensions */, caps.interix_function_keys, caps.rxvt_function_keys),
	in(f)
{
	if (0 <= tcgetattr_nointr(fileno(in), original_attr))
		tcsetattr_nointr(fileno(in), TCSADRAIN, make_raw(original_attr));
}

TUIInputBase::~TUIInputBase(
) {
	tcsetattr_nointr(fileno(in), TCSADRAIN, original_attr);
}

void
TUIInputBase::HandleInput(
	const char * b,
	std::size_t l
) {
	for (std::size_t i(0); i < l; ++i)
		utf8_decoder.Process(b[i]);
}

void 
TUIInputBase::ProcessDecodedUTF8(
	uint32_t character,
	bool decoder_error,
	bool overlong
) {
	ecma48_decoder.Process(character, decoder_error, overlong);
}

void
TUIInputBase::ExtendedKeyFromControlSequenceArgs(
	uint_fast16_t k,
	uint_fast8_t default_modifier
) {
	const uint_fast8_t m(TranslateDECModifiers(OneIfZero(argc > 1U ? args[1U] : default_modifier + 1U)));
	for (unsigned n(OneIfZero(argc > 0U ? args[0U] : 0U)); n > 0U; --n)
		ExtendedKey(k, m);
}

void
TUIInputBase::FunctionKeyFromDECFNKArgs(
	uint_fast8_t default_modifier
) {
	if (argc < 1U) return;
	const unsigned k(OneIfZero(args[0U]));
	const uint_fast8_t m(TranslateDECModifiers(OneIfZero(argc > 1U ? args[1U] : default_modifier + 1U)));
	switch (k) {
		case 1U:	ExtendedKey(caps.linux_editing_keypad ? EXTENDED_KEY_PAD_HOME : EXTENDED_KEY_FIND, m); break;
		case 2U:	ExtendedKey(EXTENDED_KEY_INSERT,m); break;
		case 3U:	ExtendedKey(EXTENDED_KEY_DELETE,m); break;
		case 4U:	ExtendedKey(caps.linux_editing_keypad ? EXTENDED_KEY_PAD_END : EXTENDED_KEY_SELECT, m); break;
		case 5U:	ExtendedKey(EXTENDED_KEY_PAGE_UP,m); break;
		case 6U:	ExtendedKey(EXTENDED_KEY_PAGE_DOWN,m); break;
		case 7U:	ExtendedKey(caps.linux_editing_keypad ? EXTENDED_KEY_FIND : EXTENDED_KEY_HOME, m); break;
		case 8U:	ExtendedKey(caps.linux_editing_keypad ? EXTENDED_KEY_SELECT : EXTENDED_KEY_END, m); break;
		case 11U:	FunctionKey(1U,m); break;
		case 12U:	FunctionKey(2U,m); break;
		case 13U:	FunctionKey(3U,m); break;
		case 14U:	FunctionKey(4U,m); break;
		case 15U:	FunctionKey(5U,m); break;
		case 17U:	FunctionKey(6U,m); break;
		case 18U:	FunctionKey(7U,m); break;
		case 19U:	FunctionKey(8U,m); break;
		case 20U:	FunctionKey(9U,m); break;
		case 21U:	FunctionKey(10U,m); break;
		case 23U:	FunctionKey(11U,m); break;
		case 24U:	FunctionKey(12U,m); break;
		case 25U:	FunctionKey(13U,m); break;
		case 26U:	FunctionKey(14U,m); break;
		case 28U:	FunctionKey(15U,m); break;
		case 29U:	FunctionKey(16U,m); break;
		case 31U:	FunctionKey(17U,m); break;
		case 32U:	FunctionKey(18U,m); break;
		case 33U:	FunctionKey(19U,m); break;
		case 34U:	FunctionKey(20U,m); break;
		case 35U:	FunctionKey(21U,m); break;
		case 36U:	FunctionKey(22U,m); break;
	}
}

void
TUIInputBase::MouseFromXTerm1006Report(
	bool press
) {
	const unsigned flags(argc > 0U ? args[0U]: 0U);
	const unsigned col(TranslateDECCoordinates(OneIfZero(argc > 1U ? args[1U] : 0U)));
	const unsigned row(TranslateDECCoordinates(OneIfZero(argc > 2U ? args[2U] : 0U)));

	uint_fast8_t modifiers(0);
	if (flags & 4U)
		modifiers |= INPUT_MODIFIER_LEVEL2;
	if (flags & 8U)
		modifiers |= INPUT_MODIFIER_SUPER;
	if (flags & 16U)
		modifiers |= INPUT_MODIFIER_CONTROL;

	MouseMove(row,col,modifiers);

	if (!(flags & 32U)) {
		const uint_fast16_t button = (flags & 3U);
		if (flags & 64U) {
			// vim gets confused by wheel release events encoded this way, so a terminal should never send them.
			// However, we account for receiving them, just in case.
			if (press)
				MouseWheel(button / 2U,button & 1U ? +1 : -1,modifiers);
		} else
			MouseButton(button,press,modifiers);
	}
}

void
TUIInputBase::MouseFromDECLocatorReport(
) {
	const unsigned event(argc > 0U ? args[0U]: 0U);
	const unsigned buttons(argc > 1U ? args[1U]: 0U);
	const unsigned row(TranslateDECCoordinates(OneIfZero(argc > 2U ? args[2U] : 0U)));
	const unsigned col(TranslateDECCoordinates(OneIfZero(argc > 3U ? args[3U] : 0U)));

	uint_fast8_t modifiers(0);

	MouseMove(row,col,modifiers);

	if (event > 1U && event < 10U) {
		const unsigned decoded(event - 2U);
		MouseButton(decoded / 2U,!(decoded & 1U),modifiers);
	} else
	if (event > 11U && event < 20U) {
		// This is an extension to the DEC protocol.
		const unsigned decoded(event - 12U);
		MouseWheel(decoded / 2U,decoded & 1U ? +1 : -1,modifiers);
	}
}

void
TUIInputBase::PrintableCharacter(
	bool error,
	unsigned short shift_level,
	uint_fast32_t character
) {
	if (10U == shift_level) {
		// The Interix system has no F0 ('0') and omits 'l' for some reason.
		if ('0' <  character && character <= '9')
			FunctionKey(character - '0',0);
		else
		if ('A' <= character && character <= 'Z')
			FunctionKey(character - 'A' + 10U,0);
		else
		if ('a' <= character && character <= 'k')
			FunctionKey(character - 'a' + 36U,0);
		else
		if ('m' <= character && character <= 'z')
			FunctionKey(character - 'm' + 47U,0);
		else
			;
	} else
	if (3U == shift_level) {
		if (caps.rxvt_function_keys) switch (character) {
			case 'a':	ExtendedKey(EXTENDED_KEY_PAD_UP,INPUT_MODIFIER_CONTROL); goto skip_dec;
			case 'b':	ExtendedKey(EXTENDED_KEY_PAD_DOWN,INPUT_MODIFIER_CONTROL); goto skip_dec;
			case 'c':	ExtendedKey(EXTENDED_KEY_PAD_RIGHT,INPUT_MODIFIER_CONTROL); goto skip_dec;
			case 'd':	ExtendedKey(EXTENDED_KEY_PAD_LEFT,INPUT_MODIFIER_CONTROL); goto skip_dec;
		}
		switch (character) {
			case 'j':	ExtendedKey(EXTENDED_KEY_PAD_ASTERISK,0); break;
			case 'k':	ExtendedKey(EXTENDED_KEY_PAD_PLUS,0); break;
			case 'l':	ExtendedKey(EXTENDED_KEY_PAD_COMMA,0); break;
			case 'm':	ExtendedKey(EXTENDED_KEY_PAD_MINUS,0); break;
			case 'n':	ExtendedKey(EXTENDED_KEY_PAD_DELETE,0); break;
			case 'o':	ExtendedKey(EXTENDED_KEY_PAD_SLASH,0); break;
			case 'p':	ExtendedKey(EXTENDED_KEY_PAD_INSERT,0); break;
			case 'q':	ExtendedKey(EXTENDED_KEY_PAD_END,0); break;
			case 'r':	ExtendedKey(EXTENDED_KEY_PAD_DOWN,0); break;
			case 's':	ExtendedKey(EXTENDED_KEY_PAD_PAGE_DOWN,0); break;
			case 't':	ExtendedKey(EXTENDED_KEY_PAD_LEFT,0); break;
			case 'u':	ExtendedKey(EXTENDED_KEY_PAD_CENTRE,0); break;
			case 'v':	ExtendedKey(EXTENDED_KEY_PAD_RIGHT,0); break;
			case 'w':	ExtendedKey(EXTENDED_KEY_PAD_HOME,0); break;
			case 'x':	ExtendedKey(EXTENDED_KEY_PAD_UP,0); break;
			case 'y':	ExtendedKey(EXTENDED_KEY_PAD_PAGE_UP,0); break;
			case 'A':	ExtendedKey(EXTENDED_KEY_UP_ARROW,0); break;
			case 'B':	ExtendedKey(EXTENDED_KEY_DOWN_ARROW,0); break;
			case 'C':	ExtendedKey(EXTENDED_KEY_RIGHT_ARROW,0); break;
			case 'D':	ExtendedKey(EXTENDED_KEY_LEFT_ARROW,0); break;
			case 'E':	ExtendedKey(EXTENDED_KEY_CENTRE,0); break;
			case 'F':	ExtendedKey(EXTENDED_KEY_END,0); break;
			case 'H':	ExtendedKey(EXTENDED_KEY_HOME,0); break;
			case 'I':	ExtendedKey(EXTENDED_KEY_PAD_TAB,0); break;
			case 'M':	ExtendedKey(EXTENDED_KEY_PAD_ENTER,0); break;
			case 'P':	ExtendedKey(EXTENDED_KEY_PAD_F1,0); break;
			case 'Q':	ExtendedKey(EXTENDED_KEY_PAD_F2,0); break;
			case 'R':	ExtendedKey(EXTENDED_KEY_PAD_F3,0); break;
			case 'S':	ExtendedKey(EXTENDED_KEY_PAD_F4,0); break;
			case 'T':	ExtendedKey(EXTENDED_KEY_PAD_F5,0); break;
			case 'X':	ExtendedKey(EXTENDED_KEY_PAD_EQUALS,0); break;
			case 'Z':	ExtendedKey(EXTENDED_KEY_BACKTAB,0); break;
		}
skip_dec:		;
	} else
	if (1U < shift_level) {
		// Do nothing.
	} else
	if (!error)
	{
		UCS3(character);
	}
}

void
TUIInputBase::ControlCharacter(uint_fast32_t character)
{
	switch (character) {
		default:		UCS3(character); break;
		// We have sent the right DECBKM and XTerm private mode sequences to ensure that these are the case.
		case 0x00000008:	ExtendedKey(EXTENDED_KEY_BACKSPACE,0); break;
		case 0x0000000A:	ExtendedKey(EXTENDED_KEY_RETURN_OR_ENTER,INPUT_MODIFIER_CONTROL); break;
		case 0x0000000D:	ExtendedKey(EXTENDED_KEY_RETURN_OR_ENTER,0); break;
		case 0x0000007F:	ExtendedKey(EXTENDED_KEY_BACKSPACE,INPUT_MODIFIER_CONTROL); break;
	}
}

void
TUIInputBase::EscapeSequence(
	uint_fast32_t character,
	char /*last_intermediate*/
) {
	Accelerator(character);
}

void
TUIInputBase::ControlSequence(
	uint_fast32_t character,
	char last_intermediate,
	char first_private_parameter
) {
	// Finish the final argument, using the relevant defaults.
	FinishArg(0U); 

	// Enact the action.
	if (NUL == last_intermediate) {
		if (NUL == first_private_parameter) {
			if (caps.sco_function_keys) {
				static const char other[9] = "@[<]^_'{";
				// The SCO system has no F0 ('L').
				if ('L' <  character && character <= 'Z') {
					FunctionKey(character - 'L', 0);
					goto skip_dec;
				} else
				if ('a' <= character && character <= 'z') {
					FunctionKey(character - 'a' + 15U, 0);
					goto skip_dec;
				} else
				if (const char * p = 0x20 < character && character < 0x80 ? std::strchr(other, static_cast<char>(character)) : 0) {
					FunctionKey(p - other + 41U, 0);
					goto skip_dec;
				} else
					;
			}
			if (caps.rxvt_function_keys) switch (character) {
				case '$':	FunctionKeyFromDECFNKArgs(INPUT_MODIFIER_LEVEL2); goto skip_dec;
				case '^':	FunctionKeyFromDECFNKArgs(INPUT_MODIFIER_CONTROL); goto skip_dec;
				case '@':	FunctionKeyFromDECFNKArgs(INPUT_MODIFIER_LEVEL2|INPUT_MODIFIER_CONTROL); goto skip_dec;
				case 'a':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_UP_ARROW, INPUT_MODIFIER_LEVEL2); goto skip_dec;
				case 'b':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_DOWN_ARROW, INPUT_MODIFIER_LEVEL2); goto skip_dec;
				case 'c':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_RIGHT_ARROW, INPUT_MODIFIER_LEVEL2); goto skip_dec;
				case 'd':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_LEFT_ARROW, INPUT_MODIFIER_LEVEL2); goto skip_dec;
			}
			switch (character) {
				case 'A':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_UP_ARROW); break;
				case 'B':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_DOWN_ARROW); break;
				case 'C':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_RIGHT_ARROW); break;
				case 'D':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_LEFT_ARROW); break;
				case 'E':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_CENTRE); break;
				case 'F':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_END); break;
				case 'G':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAGE_DOWN); break;
				case 'H':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_HOME); break;
				case 'I':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAGE_UP); break;
				case 'L':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_INSERT); break;
				case 'M':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAD_ENTER); break;
				case 'P':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAD_F1); break;
				case 'Q':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAD_F2); break;
				case 'R':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAD_F3); break;
				case 'S':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAD_F4); break;
				case 'T':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_PAD_F5); break;
				case 'Z':	ExtendedKeyFromControlSequenceArgs(EXTENDED_KEY_BACKTAB); break;
				case '~':	FunctionKeyFromDECFNKArgs(0U); break;
			}
skip_dec: 		;
		} else
		if ('<' == first_private_parameter) switch (character) {
			case 'M':	MouseFromXTerm1006Report(true); break;
			case 'm':	MouseFromXTerm1006Report(false); break;
		} else
			;
	} else
	if ('&' == last_intermediate) switch (character) {
/* DECLRP */	case 'w':	MouseFromDECLocatorReport(); break;
	} else
		;
}

void
TUIInputBase::ControlString(uint_fast32_t /*character*/)
{
}
