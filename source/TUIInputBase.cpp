/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <iostream>
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

inline unsigned int TranslateToDECModifiers(uint_fast8_t n) { return n + 1U; }
inline uint_fast8_t TranslateFromDECModifiers(unsigned int n) { return n - 1U; }
inline unsigned int TranslateFromDECCoordinates(unsigned int n) { return n - 1U; }
inline uint16_t TranslateFromXTermButton(const uint16_t button) {
	switch (button) {
		case 1U:	return 2U;
		case 2U:	return 1U;
		default:	return button;
	}
}

}

/* The base class ***********************************************************
// **************************************************************************
*/

TUIInputBase::TUIInputBase(
	const TerminalCapabilities & c,
	std::istream & i,
	int d
) :
	ECMA48Decoder::ECMA48ControlSequenceSink(),
	caps(c),
	utf8_decoder(*this),
	ecma48_decoder(*this, ECMA48Decoder::Options(false /* no control strings */, false /* no cancel */, true /* 7-bit extensions */, caps.interix_function_keys, caps.rxvt_function_keys, caps.linux_function_keys)),
	in(i),
	in_fd(d)
{
	if (0 <= tcgetattr_nointr(in_fd, original_attr))
		tcsetattr_nointr(in_fd, TCSADRAIN, disable_canonical_software_processing(original_attr));
}

TUIInputBase::~TUIInputBase(
) {
	tcsetattr_nointr(in_fd, TCSADRAIN, original_attr);
}

void
TUIInputBase::HandleInput(
	const char * b,
	std::size_t l
) {
	for (std::size_t i(0); i < l; ++i)
		utf8_decoder.Process(b[i]);
}

int
TUIInputBase::QueryInputFD(
) const {
	return in_fd;
}

void
TUIInputBase::ProcessDecodedUTF8(
	char32_t character,
	bool decoder_error,
	bool overlong
) {
	ecma48_decoder.Process(character, decoder_error, overlong);
}

inline
void
TUIInputBase::UCS3(char32_t character)
{
	for (std::list<EventHandler*>::const_iterator p(handlers.begin()), e(handlers.end()); p != e; ++p)
		if ((*p)->UCS3(character))
			break;
}

inline
void
TUIInputBase::Accelerator(char32_t character)
{
	for (std::list<EventHandler*>::const_iterator p(handlers.begin()), e(handlers.end()); p != e; ++p)
		if ((*p)->Accelerator(character))
			break;
}

inline
void
TUIInputBase::MouseMove(uint_fast16_t n, uint_fast16_t v, uint8_t m)
{
	for (std::list<EventHandler*>::const_iterator p(handlers.begin()), e(handlers.end()); p != e; ++p)
		if ((*p)->MouseMove(n, v, m))
			break;
}

inline
void
TUIInputBase::MouseWheel(uint_fast8_t n, int_fast8_t v, uint_fast8_t m)
{
	for (std::list<EventHandler*>::const_iterator p(handlers.begin()), e(handlers.end()); p != e; ++p)
		if ((*p)->MouseWheel(n, v, m))
			break;
}

inline
void
TUIInputBase::MouseButton(uint_fast8_t n, uint_fast8_t v, uint_fast8_t m)
{
	for (std::list<EventHandler*>::const_iterator p(handlers.begin()), e(handlers.end()); p != e; ++p)
		if ((*p)->MouseButton(n, v, m))
			break;
}

void
TUIInputBase::ExtendedKeyFromControlSequenceArgs(
	uint_fast16_t k,
	uint_fast8_t default_modifier
) {
	if (1U > QueryArgCount()) {
		ExtendedKey(k, default_modifier);
	} else
	{
		// Vanilla CSI sequences for extended keys never have subarguments, so we pre-convert multiple arguments to a single key in our extended form.
		if (HasNoSubArgsFrom(0U))
			CollapseArgsToSubArgs(0U);
		// As an extension we permit multiple keys to be encoded in ISO 8613-3/ITU T.416 rep:mod form.
		for (std::size_t i(0U); i < QueryArgCount(); ++i) {
			// These control sequences are transmitted with the old pre-ECMA-48:1986 non-ZDM semantics.
			const uint_fast8_t m(TranslateFromDECModifiers(GetArgOneIfZeroThisIfEmpty(i,1U, TranslateToDECModifiers(default_modifier))));
			for (unsigned n(GetArgOneIfZeroOrEmpty(i,0U)); n > 0U; --n)
				ExtendedKey(k, m);
		}
	}
}

void
TUIInputBase::ExtendedKeyFromFNKArgs(
) {
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		const unsigned k(GetArgZeroIfEmpty(i,0U));
		const uint_fast8_t m(GetArgZeroIfEmpty(i,1U));
		ExtendedKey(k,m);
	}
}

void
TUIInputBase::ConsumerKeyFromFNKArgs(
) {
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		const unsigned k(GetArgZeroIfEmpty(i,0U));
		const uint_fast8_t m(GetArgZeroIfEmpty(i,1U));
		ConsumerKey(k,m);
	}
}

void
TUIInputBase::FunctionKeyFromFNKArgs(
) {
	// As an extension we permit multiple keys and modifiers to be encoded in ISO 8613-3/ITU T.416 key:mod form.
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		const unsigned k(GetArgZeroIfEmpty(i,0U));
		const uint_fast8_t m(GetArgZeroIfEmpty(i,1U));
		FunctionKey(k,m);
	}
}

void
TUIInputBase::XTermModifiedKey(
	unsigned k,
	uint_fast8_t modifier
) {
	switch (k) {
		case 8U:	ExtendedKey(EXTENDED_KEY_BACKSPACE, modifier); break;
		case 9U:	ExtendedKey(EXTENDED_KEY_TAB, modifier); break;
		case 13U:	ExtendedKey(EXTENDED_KEY_PAD_ENTER, modifier); break;
		case 27U:	ExtendedKey(EXTENDED_KEY_ESCAPE, modifier); break;
		case 33U:	ExtendedKey(EXTENDED_KEY_PAD_EXCLAMATION, modifier); break;
		case 35U:	ExtendedKey(EXTENDED_KEY_PAD_HASH, modifier); break;
//		case 39U:	ExtendedKey(EXTENDED_KEY_PAD_APOSTROPHE, modifier); break;
		case 40U:	ExtendedKey(EXTENDED_KEY_PAD_OPEN_BRACKET, modifier); break;
		case 41U:	ExtendedKey(EXTENDED_KEY_PAD_CLOSE_BRACKET, modifier); break;
		case 43U:	ExtendedKey(EXTENDED_KEY_PAD_PLUS, modifier); break;
		case 44U:	ExtendedKey(EXTENDED_KEY_PAD_COMMA, modifier); break;
		case 45U:	ExtendedKey(EXTENDED_KEY_PAD_MINUS, modifier); break;
		case 46U:	ExtendedKey(EXTENDED_KEY_PAD_DELETE, modifier); break;
		case 48U:	ExtendedKey(EXTENDED_KEY_PAD_INSERT, modifier); break;
		case 49U:	ExtendedKey(EXTENDED_KEY_PAD_END, modifier); break;
		case 50U:	ExtendedKey(EXTENDED_KEY_PAD_DOWN, modifier); break;
		case 51U:	ExtendedKey(EXTENDED_KEY_PAD_PAGE_DOWN, modifier); break;
		case 52U:	ExtendedKey(EXTENDED_KEY_PAD_LEFT, modifier); break;
		case 53U:	ExtendedKey(EXTENDED_KEY_PAD_CENTRE, modifier); break;
		case 54U:	ExtendedKey(EXTENDED_KEY_PAD_RIGHT, modifier); break;
		case 55U:	ExtendedKey(EXTENDED_KEY_PAD_HOME, modifier); break;
		case 56U:	ExtendedKey(EXTENDED_KEY_PAD_UP, modifier); break;
		case 57U:	ExtendedKey(EXTENDED_KEY_PAD_PAGE_UP, modifier); break;
		case 58U:	ExtendedKey(EXTENDED_KEY_PAD_COLON, modifier); break;
//		case 59U:	ExtendedKey(EXTENDED_KEY_PAD_SEMICOLON, modifier); break;
		case 60U:	ExtendedKey(EXTENDED_KEY_PAD_LESS, modifier); break;
		case 61U:	ExtendedKey(EXTENDED_KEY_PAD_EQUALS, modifier); break;
		case 62U:	ExtendedKey(EXTENDED_KEY_PAD_GREATER, modifier); break;
//		case 63U:	ExtendedKey(EXTENDED_KEY_PAD_QUESTION, modifier); break;
	}
}

void
TUIInputBase::FunctionKeyFromDECFNKNumber(
	unsigned k,
	uint_fast8_t modifier
) {
	switch (k) {
		case 1U:	ExtendedKey(caps.linux_editing_keypad ? EXTENDED_KEY_PAD_HOME : EXTENDED_KEY_FIND, modifier); break;
		case 2U:	ExtendedKey(EXTENDED_KEY_INSERT, modifier); break;
		case 3U:	ExtendedKey(EXTENDED_KEY_DELETE, modifier); break;
		case 4U:	ExtendedKey(caps.linux_editing_keypad ? EXTENDED_KEY_PAD_END : EXTENDED_KEY_SELECT, modifier); break;
		case 5U:	ExtendedKey(EXTENDED_KEY_PAGE_UP, modifier); break;
		case 6U:	ExtendedKey(EXTENDED_KEY_PAGE_DOWN, modifier); break;
		case 7U:	ExtendedKey(caps.linux_editing_keypad ? EXTENDED_KEY_FIND : EXTENDED_KEY_HOME, modifier); break;
		case 8U:	ExtendedKey(caps.linux_editing_keypad ? EXTENDED_KEY_SELECT : EXTENDED_KEY_END, modifier); break;
		case 11U:	FunctionKey(1U, modifier); break;
		case 12U:	FunctionKey(2U, modifier); break;
		case 13U:	FunctionKey(3U, modifier); break;
		case 14U:	FunctionKey(4U, modifier); break;
		case 15U:	FunctionKey(5U, modifier); break;
		case 17U:	FunctionKey(6U, modifier); break;
		case 18U:	FunctionKey(7U, modifier); break;
		case 19U:	FunctionKey(8U, modifier); break;
		case 20U:	FunctionKey(9U, modifier); break;
		case 21U:	FunctionKey(10U, modifier); break;
		case 23U:	FunctionKey(11U, modifier); break;
		case 24U:	FunctionKey(12U, modifier); break;
		case 25U:	FunctionKey(13U, modifier); break;
		case 26U:	FunctionKey(14U, modifier); break;
		case 28U:	FunctionKey(15U, modifier); break;
		case 29U:	FunctionKey(16U, modifier); break;
		case 31U:	FunctionKey(17U, modifier); break;
		case 32U:	FunctionKey(18U, modifier); break;
		case 33U:	FunctionKey(19U, modifier); break;
		case 34U:	FunctionKey(20U, modifier); break;
		case 35U:	FunctionKey(21U, modifier); break;
		case 36U:	FunctionKey(22U, modifier); break;
		case 42U:	FunctionKey(23U, modifier); break;	// XTerm extension
		case 43U:	FunctionKey(24U, modifier); break;	// XTerm extension
	}
}

void
TUIInputBase::FunctionKeyFromDECFNKArgs(
) {
	// Vanilla DECFNK never has subarguments, so we pre-convert multiple arguments to a single key in our extended form.
	if (HasNoSubArgsFrom(0U))
		CollapseArgsToSubArgs(0U);
	// As an extension we permit multiple keys to be encoded in ISO 8613-3/ITU T.416 key:mod form.
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		// DECFNK is transmitted with the old pre-ECMA-48:1986 non-ZDM semantics.
		const unsigned k(GetArgOneIfZeroOrEmpty(i,0U));
		const uint_fast8_t m(TranslateFromDECModifiers(GetArgOneIfZeroOrEmpty(i,1U)));
		if (27U == k) {
			// DEC VT function key 14½ is a Harry Potter XTerm extension to DEC VT.
			if (QuerySubArgCount(i) >= 3U)
				XTermModifiedKey(GetArgZeroIfEmpty(i,2U),m);
		} else
			// Other than rxvt, terminals that send DECFNK for F1 to F5 really mean it.
			// They send the SS3 sequences of PF1 to PF5 for F1 to F5.
			FunctionKeyFromDECFNKNumber(k,m);
	}
}

void
TUIInputBase::FunctionKeyFromURxvtArgs(
	uint_fast8_t default_modifier	///< As implied by the final character
) {
	// Vanilla rxvt never has subarguments, so we pre-convert multiple arguments to a single key in our extended form.
	if (HasNoSubArgsFrom(0U))
		CollapseArgsToSubArgs(0U);
	// As an extension we permit multiple keys to be encoded in ISO 8613-3/ITU T.416 key:mod form.
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		const unsigned k(GetArgZeroIfEmpty(i,0U));
		if (QueryArgNull(i,1U)) {
			// Vanilla rxvt encodes the modifiers in the final character.
			const uint_fast8_t m(default_modifier);
			switch (k) {
				// Vanilla rxvt sends DECFNK for F1 to F5 rather than the SS3 sequences for the PF1 to PF5 keys.
				case 11U:	if (0U == m) { ExtendedKey(EXTENDED_KEY_PAD_F1, m); break; } else [[clang::fallthrough]];
				case 12U:	if (0U == m) { ExtendedKey(EXTENDED_KEY_PAD_F2, m); break; } else [[clang::fallthrough]];
				case 13U:	if (0U == m) { ExtendedKey(EXTENDED_KEY_PAD_F3, m); break; } else [[clang::fallthrough]];
				case 14U:	if (0U == m) { ExtendedKey(EXTENDED_KEY_PAD_F4, m); break; } else [[clang::fallthrough]];
				case 15U:	if (0U == m) { ExtendedKey(EXTENDED_KEY_PAD_F5, m); break; } else [[clang::fallthrough]];
				default:	FunctionKeyFromDECFNKNumber(k,m); break;
#if 0 // These are handled by the default case.
				case 17U:	FunctionKey(6U, m); break;
				case 18U:	FunctionKey(7U, m); break;
				case 19U:	FunctionKey(8U, m); break;
				case 20U:	FunctionKey(9U, m); break;
				case 21U:	FunctionKey(10U, m); break;
#endif
				// Vanilla rxvt crazily encodes level 2 shift in the function key number, assuming only 10 function keys.
				case 23U:	FunctionKey(1U, m | INPUT_MODIFIER_LEVEL2); break;
				case 24U:	FunctionKey(2U, m | INPUT_MODIFIER_LEVEL2); break;
				case 25U:	FunctionKey(3U, m | INPUT_MODIFIER_LEVEL2); break;
				case 26U:	FunctionKey(4U, m | INPUT_MODIFIER_LEVEL2); break;
				case 28U:	FunctionKey(5U, m | INPUT_MODIFIER_LEVEL2); break;
				case 29U:	FunctionKey(6U, m | INPUT_MODIFIER_LEVEL2); break;
				case 31U:	FunctionKey(7U, m | INPUT_MODIFIER_LEVEL2); break;
				case 32U:	FunctionKey(8U, m | INPUT_MODIFIER_LEVEL2); break;
				case 33U:	FunctionKey(9U, m | INPUT_MODIFIER_LEVEL2); break;
				case 34U:	FunctionKey(10U, m | INPUT_MODIFIER_LEVEL2); break;
			}
		} else
		{
			// Patched rxvt sends an explicit modifier subargument, generating DECFNK.
			// It sends the SS3 sequences of PF1 to PF5 for F1 to F5.
			const uint_fast8_t m(TranslateFromDECModifiers(GetArgOneIfZeroThisIfEmpty(i,1U,TranslateToDECModifiers(default_modifier))));
			FunctionKeyFromDECFNKNumber(k,m);
		}
	}
}

void
TUIInputBase::FunctionKeyFromSCOFNK(
	unsigned k
) {
	// The caller has guaranteed that XTerm's extensions for PF1 to PF5 with modifiers has not reached here.
	// Vanilla CSI sequences for SCO function keys never have arguments.
	// They encode the modifiers in the key number.
	uint_fast8_t m(0U);
	// our extensions
	if (k > 192U) {
		m |= INPUT_MODIFIER_SUPER;
		k -= 192U;
	}
	if (k > 96U) {
		m |= INPUT_MODIFIER_GROUP2;
		k -= 96U;
	}
	if (k > 48U) {
		m |= INPUT_MODIFIER_LEVEL3;
		k -= 48U;
	}
	// SCO
	if (k > 24U) {
		m |= INPUT_MODIFIER_CONTROL;
		k -= 24U;
	}
	if (k > 12U) {
		m |= INPUT_MODIFIER_LEVEL2;
		k -= 12U;
	}
	FunctionKey(k,m);
}

void
TUIInputBase::MouseFromXTerm1006Report(
	bool press
) {
	// Vanilla mouse reports never have subarguments, so we pre-convert multiple arguments to a single report in our extended form.
	if (HasNoSubArgsFrom(0U))
		CollapseArgsToSubArgs(0U);
	// As an extension we permit multiple reports to be encoded in ISO 8613-3/ITU T.416 flags:col:row form.
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		const unsigned flags(GetArgZeroIfEmpty(i, 0U));
		const unsigned col(TranslateFromDECCoordinates(GetArgOneIfZeroOrEmpty(i, 1U)));
		const unsigned row(TranslateFromDECCoordinates(GetArgOneIfZeroOrEmpty(i, 2U)));

		uint_fast8_t modifiers(0);
		if (flags & 4U)
			modifiers |= INPUT_MODIFIER_LEVEL2;
		if (flags & 8U)
			modifiers |= INPUT_MODIFIER_SUPER;
		if (flags & 16U)
			modifiers |= INPUT_MODIFIER_CONTROL;

		MouseMove(row,col,modifiers);

		if (!(flags & 32U)) {
			if (flags & 64U) {
				// Terminals don't send wheel release events; vim gets confused by them when they are encoded this way.
				// However, we allow for receiving them, just in case.
				const uint_fast16_t wheel(flags & 3U);
				if (0U == wheel) {
					if (press)
						MouseWheel(0U,-1,modifiers);
				} else
				if (1U == wheel) {
					if (press)
						MouseWheel(0U,+1,modifiers);
				}
			} else
			{
				const uint_fast16_t button(flags & 3U);
				if (3U != button)
					MouseButton(TranslateFromXTermButton(button),press,modifiers);
			}
		}
	}
}

void
TUIInputBase::MouseFromDECLocatorReport(
) {
	// Vanilla locator reports never have subarguments, so we pre-convert multiple arguments to a single report in our extended form.
	if (HasNoSubArgsFrom(0U))
		CollapseArgsToSubArgs(0U);
	// As an extension we permit multiple reports to be encoded in ISO 8613-3/ITU T.416 event:buttons:row:col form.
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		const unsigned event(GetArgZeroIfEmpty(i, 0U));
		const unsigned buttons(GetArgZeroIfEmpty(i, 1U));
		const unsigned row(TranslateFromDECCoordinates(GetArgOneIfZeroOrEmpty(i, 2U)));
		const unsigned col(TranslateFromDECCoordinates(GetArgOneIfZeroOrEmpty(i, 3U)));

		static_cast<void>(buttons);

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
}

void
TUIInputBase::PrintableCharacter(
	bool error,
	unsigned short shift_level,
	char32_t character
) {
	if (12U == shift_level) {
		if ('A' <= character && character <= 'Z')
			FunctionKey(character - 'A' + 1U,0);
		else
		{
		}
	} else
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
		{
		}
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
			case 'I':	ExtendedKey(EXTENDED_KEY_TAB,0); break;
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
TUIInputBase::ControlCharacter(char32_t character)
{
	UCS3(character);
}

void
TUIInputBase::EscapeSequence(
	char32_t character,
	char32_t /*first_intermediate*/
) {
	Accelerator(character);
}

void
TUIInputBase::ControlSequence(
	char32_t character,
	char32_t last_intermediate,
	char32_t first_private_parameter
) {
	// Enact the action.
	if (NUL == last_intermediate) {
		if (NUL == first_private_parameter) {
			// XTerm's Backtab, and extensions for Enter and PF1 to PF5 with modifiers, use CSI; and overlap SCO function keys.
			// SCOFNK sequences never have modifiers, so we detect the lack of subarguments.
			// For best results, simply avoid SCO function keys.
			if ((caps.sco_function_keys || caps.teken_function_keys) && (1U > QueryArgCount())) {
				static const char other[9] = "@[<]^_'{";
				// The SCOFNK system has no F0 ('L').
				if ('L' == character)
					;
				else
				// teken does not use SCOFNK for F1 ('M') to F12 ('X'); only genuine SCO Unix Multiscreen does.
				if (caps.sco_function_keys
				&& ('L' <  character && character <= 'P')
				) {
					FunctionKeyFromSCOFNK(character - 'L');
					goto skip_dec;
				} else
				// teken realigns with SCO at Level2+F1 ('Y')
				if ('Y' <= character && character <= 'Z') {
					FunctionKeyFromSCOFNK(character - 'L');
					goto skip_dec;
				} else
				if ('a' <= character && character <= 'z') {
					FunctionKeyFromSCOFNK(character - 'a' + 15U);
					goto skip_dec;
				} else
				if (const char * p = 0x20 < character && character < 0x80 ? std::strchr(other, static_cast<char>(character)) : nullptr) {
					FunctionKeyFromSCOFNK(p - other + 41U);
					goto skip_dec;
				} else
				{
				}
			}
			if (caps.rxvt_function_keys) switch (character) {
				case '~':	FunctionKeyFromURxvtArgs(0U); goto skip_dec;
				case '$':	FunctionKeyFromURxvtArgs(INPUT_MODIFIER_LEVEL2); goto skip_dec;
				case '^':	FunctionKeyFromURxvtArgs(INPUT_MODIFIER_CONTROL); goto skip_dec;
				case '@':	FunctionKeyFromURxvtArgs(INPUT_MODIFIER_CONTROL|INPUT_MODIFIER_LEVEL2); goto skip_dec;
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
				case '~':	FunctionKeyFromDECFNKArgs(); break;
			}
skip_dec: 		;
		} else
		if ('<' == first_private_parameter) switch (character) {
			case 'M':	MouseFromXTerm1006Report(true); break;
			case 'm':	MouseFromXTerm1006Report(false); break;
		} else
		{
		}
	} else
	if ('&' == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
/* DECLRP */		case 'w':	MouseFromDECLocatorReport(); break;
		} else
		{
		}
	} else
	if (' ' == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
			case 'W':	FunctionKeyFromFNKArgs(); break;
		} else
		if ('?' == first_private_parameter) switch (character) {
			case 'W':	ExtendedKeyFromFNKArgs(); break;
		} else
		if ('=' == first_private_parameter) switch (character) {
			case 'W':	ConsumerKeyFromFNKArgs(); break;
		} else
		{
		}
	} else
	{
	}
}

void
TUIInputBase::ControlString(
	char32_t /*start_char*/,
	char32_t /*term_char*/
) {
}

void
TUIInputBase::ConsumerKey(uint_fast16_t k, uint_fast8_t m)
{
	for (std::list<EventHandler*>::const_iterator p(handlers.begin()), e(handlers.end()); p != e; ++p)
		if ((*p)->ConsumerKey(k, m))
			break;
}

void
TUIInputBase::ExtendedKey(uint_fast16_t k, uint_fast8_t m)
{
	for (std::list<EventHandler*>::const_iterator p(handlers.begin()), e(handlers.end()); p != e; ++p)
		if ((*p)->ExtendedKey(k, m))
			break;
}

void
TUIInputBase::FunctionKey(uint_fast16_t k, uint_fast8_t m)
{
	for (std::list<EventHandler*>::const_iterator p(handlers.begin()), e(handlers.end()); p != e; ++p)
		if ((*p)->FunctionKey(k, m))
			break;
}

/* The handler classes ******************************************************
// **************************************************************************
*/

bool TUIInputBase::EventHandler::ConsumerKey(uint_fast16_t, uint_fast8_t) { return false; }
bool TUIInputBase::EventHandler::ExtendedKey(uint_fast16_t, uint_fast8_t) { return false; }
bool TUIInputBase::EventHandler::FunctionKey(uint_fast16_t, uint_fast8_t) { return false; }
bool TUIInputBase::EventHandler::UCS3(char32_t) { return false; }
bool TUIInputBase::EventHandler::Accelerator(char32_t) { return false; }
bool TUIInputBase::EventHandler::MouseMove(uint_fast16_t, uint_fast16_t, uint8_t) { return false; }
bool TUIInputBase::EventHandler::MouseWheel(uint_fast8_t, int_fast8_t, uint_fast8_t) { return false; }
bool TUIInputBase::EventHandler::MouseButton(uint_fast8_t, uint_fast8_t, uint_fast8_t) { return false; }

// Seat of the class
TUIInputBase::EventHandler::EventHandler(TUIInputBase & i) : input(i) { input.handlers.insert(input.handlers.end(), this); }
TUIInputBase::EventHandler::~EventHandler() { input.handlers.remove(this); }

bool
TUIInputBase::CalculatorKeypadToPrintables::ExtendedKey(
	uint_fast16_t k,
	uint_fast8_t m
) {
	switch (k) {
		default:				return EventHandler::ExtendedKey(k, m);
		case EXTENDED_KEY_PAD_SLASH:		GenerateUCS3('/'); return true;
		case EXTENDED_KEY_PAD_ASTERISK:		GenerateUCS3('*'); return true;
		case EXTENDED_KEY_PAD_MINUS:		GenerateUCS3('-'); return true;
		case EXTENDED_KEY_PAD_PLUS:		GenerateUCS3('+'); return true;
		case EXTENDED_KEY_PAD_EQUALS_AS400:
		case EXTENDED_KEY_PAD_EQUALS:		GenerateUCS3('='); return true;
		case EXTENDED_KEY_PAD_COMMA:		GenerateUCS3(','); return true;
		case EXTENDED_KEY_PAD_000:		GenerateUCS3('0'); [[clang::fallthrough]];
		case EXTENDED_KEY_PAD_00:		GenerateUCS3('0'); GenerateUCS3('0'); return true;
		case EXTENDED_KEY_PAD_THOUSANDS_SEP:	GenerateUCS3(','); return true;
		case EXTENDED_KEY_PAD_DECIMAL_SEP:
		case EXTENDED_KEY_PAD_DECIMAL:		GenerateUCS3('.'); return true;
		case EXTENDED_KEY_PAD_CURRENCY_UNIT:	GenerateUCS3(0x0000A4); return true;
		case EXTENDED_KEY_PAD_CURRENCY_SUB:	GenerateUCS3(0x0000A2); return true;
		case EXTENDED_KEY_PAD_OPEN_BRACKET:	GenerateUCS3('['); return true;
		case EXTENDED_KEY_PAD_OPEN_BRACE:	GenerateUCS3('{'); return true;
		case EXTENDED_KEY_PAD_CLOSE_BRACKET:	GenerateUCS3(']'); return true;
		case EXTENDED_KEY_PAD_CLOSE_BRACE:	GenerateUCS3('}'); return true;
		case EXTENDED_KEY_PAD_A:		GenerateUCS3('A'); return true;
		case EXTENDED_KEY_PAD_B:		GenerateUCS3('B'); return true;
		case EXTENDED_KEY_PAD_C:		GenerateUCS3('C'); return true;
		case EXTENDED_KEY_PAD_D:		GenerateUCS3('D'); return true;
		case EXTENDED_KEY_PAD_E:		GenerateUCS3('E'); return true;
		case EXTENDED_KEY_PAD_F:		GenerateUCS3('F'); return true;
		case EXTENDED_KEY_PAD_XOR:
		case EXTENDED_KEY_PAD_CARET:		GenerateUCS3('^'); return true;
		case EXTENDED_KEY_PAD_PERCENT:		GenerateUCS3('%'); return true;
		case EXTENDED_KEY_PAD_LESS:		GenerateUCS3('<'); return true;
		case EXTENDED_KEY_PAD_GREATER:		GenerateUCS3('<'); return true;
		case EXTENDED_KEY_PAD_ANDAND:		GenerateUCS3('&'); [[clang::fallthrough]];
		case EXTENDED_KEY_PAD_AND:		GenerateUCS3('&'); return true;
		case EXTENDED_KEY_PAD_OROR:		GenerateUCS3('|'); [[clang::fallthrough]];
		case EXTENDED_KEY_PAD_OR:		GenerateUCS3('|'); return true;
		case EXTENDED_KEY_PAD_COLON:		GenerateUCS3(':'); return true;
		case EXTENDED_KEY_PAD_HASH:		GenerateUCS3('#'); return true;
		case EXTENDED_KEY_PAD_SPACE:		GenerateUCS3(SPC); return true;
		case EXTENDED_KEY_PAD_AT:		GenerateUCS3('@'); return true;
		case EXTENDED_KEY_PAD_EXCLAMATION:	GenerateUCS3('!'); return true;
	}
}

// Seat of the class
TUIInputBase::CalculatorKeypadToPrintables::CalculatorKeypadToPrintables(TUIInputBase & i) : EventHandler(i) {}
TUIInputBase::CalculatorKeypadToPrintables::~CalculatorKeypadToPrintables() {}

bool
TUIInputBase::WheelToKeyboard::MouseWheel(
	uint_fast8_t wheel,
	int_fast8_t value,
	uint_fast8_t modifiers
) {
	switch (wheel) {
		default:	return EventHandler::MouseWheel(wheel, value, modifiers);
		case 0U:
			while (value < 0) {
				GenerateExtendedKey(EXTENDED_KEY_UP_ARROW, 0U);
				++value;
			}
			while (0 < value) {
				GenerateExtendedKey(EXTENDED_KEY_DOWN_ARROW, 0U);
				--value;
			}
			return true;
		case 1U:
			while (value < 0) {
				GenerateExtendedKey(EXTENDED_KEY_LEFT_ARROW, 0U);
				++value;
			}
			while (0 < value) {
				GenerateExtendedKey(EXTENDED_KEY_RIGHT_ARROW, 0U);
				--value;
			}
			return true;
	}
}

// Seat of the class
TUIInputBase::WheelToKeyboard::WheelToKeyboard(TUIInputBase & i) : EventHandler(i) {}
TUIInputBase::WheelToKeyboard::~WheelToKeyboard() {}

bool
TUIInputBase::GamingToCursorKeypad::UCS3(
	char32_t character
) {
	switch (character) {
		default:	return EventHandler::UCS3(character);
		case 'W': case 'w':	GenerateExtendedKey(EXTENDED_KEY_UP_ARROW, 0U); return true;
		case 'S': case 's':	GenerateExtendedKey(EXTENDED_KEY_DOWN_ARROW, 0U); return true;
		case 'A': case 'a':	GenerateExtendedKey(EXTENDED_KEY_LEFT_ARROW, 0U); return true;
		case 'D': case 'd':	GenerateExtendedKey(EXTENDED_KEY_RIGHT_ARROW, 0U); return true;
	}
}

// Seat of the class
TUIInputBase::GamingToCursorKeypad::GamingToCursorKeypad(TUIInputBase & i) : EventHandler(i) {}
TUIInputBase::GamingToCursorKeypad::~GamingToCursorKeypad() {}

bool
TUIInputBase::CalculatorToEditingKeypad::ExtendedKey(
	uint_fast16_t k,
	uint_fast8_t m
) {
	switch (k) {
		default:	return EventHandler::ExtendedKey(k,m);
		// We explicitly do not translate EXTENDED_KEY_PAD_ENTER into EXTENDED_KEY_RETURN_OR_ENTER.
		case EXTENDED_KEY_PAD_INSERT:		GenerateExtendedKey(EXTENDED_KEY_INSERT,m); return true;
		case EXTENDED_KEY_PAD_DELETE:		GenerateExtendedKey(EXTENDED_KEY_DELETE,m); return true;
		case EXTENDED_KEY_PAD_LEFT:		GenerateExtendedKey(EXTENDED_KEY_LEFT_ARROW,m); return true;
		case EXTENDED_KEY_PAD_RIGHT:		GenerateExtendedKey(EXTENDED_KEY_RIGHT_ARROW,m); return true;
		case EXTENDED_KEY_PAD_DOWN:		GenerateExtendedKey(EXTENDED_KEY_DOWN_ARROW,m); return true;
		case EXTENDED_KEY_PAD_UP:		GenerateExtendedKey(EXTENDED_KEY_UP_ARROW,m); return true;
		case EXTENDED_KEY_PAD_END:		GenerateExtendedKey(EXTENDED_KEY_END,m); return true;
		case EXTENDED_KEY_PAD_HOME:		GenerateExtendedKey(EXTENDED_KEY_HOME,m); return true;
		case EXTENDED_KEY_PAD_CENTRE:		GenerateExtendedKey(EXTENDED_KEY_CENTRE,m); return true;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	GenerateExtendedKey(EXTENDED_KEY_PAGE_DOWN,m); return true;
		case EXTENDED_KEY_PAD_PAGE_UP:		GenerateExtendedKey(EXTENDED_KEY_PAGE_UP,m); return true;
		case EXTENDED_KEY_PAD_TAB:		GenerateExtendedKey(EXTENDED_KEY_TAB,m); return true;
		case EXTENDED_KEY_PAD_BACKSPACE:	GenerateExtendedKey(EXTENDED_KEY_BACKSPACE,m); return true;
		case EXTENDED_KEY_PAD_CLEAR_ENTRY:	GenerateExtendedKey(EXTENDED_KEY_CLEAR,m); return true;
		case EXTENDED_KEY_PAD_CLEAR:		GenerateExtendedKey(EXTENDED_KEY_CANCEL,m); return true;
	}
}

// Seat of the class
TUIInputBase::CalculatorToEditingKeypad::CalculatorToEditingKeypad(TUIInputBase & i) : EventHandler(i) {}
TUIInputBase::CalculatorToEditingKeypad::~CalculatorToEditingKeypad() {}

bool
TUIInputBase::ConsumerKeysToExtendedKeys::ConsumerKey(
	uint_fast16_t k,
	uint_fast8_t m
) {
	switch (k) {
		default:	return EventHandler::ConsumerKey(k,m);
		case CONSUMER_KEY_CLOSE:	GenerateExtendedKey(EXTENDED_KEY_CLOSE, m); return true;
		case CONSUMER_KEY_EXIT:		GenerateExtendedKey(EXTENDED_KEY_EXIT, m); return true;
		case CONSUMER_KEY_REFRESH:	GenerateExtendedKey(EXTENDED_KEY_REFRESH, m); return true;
	}
}

// Seat of the class
TUIInputBase::ConsumerKeysToExtendedKeys::ConsumerKeysToExtendedKeys(TUIInputBase & i) : EventHandler(i) {}
TUIInputBase::ConsumerKeysToExtendedKeys::~ConsumerKeysToExtendedKeys() {}

bool
TUIInputBase::CUAToExtendedKeys::UCS3(
	char32_t character
) {
	switch (character) {
		default:	return EventHandler::UCS3(character);
		// This is from Microsoft Windows and is not strictly a CUA thing.
		case DC1:	// Control+Q
			GenerateExtendedKey(EXTENDED_KEY_EXIT, 0);
			return true;
		// This is from Microsoft Windows and is not strictly a CUA thing.
		case DC3:	// Control+S
			GenerateExtendedKey(EXTENDED_KEY_SAVE, 0);
			return true;
		// This is from Microsoft Windows and is not strictly a CUA thing.
		case ETB:	// Control+Q
			GenerateExtendedKey(EXTENDED_KEY_CLOSE, 0);
			return true;
		case CAN:	// Control+X
			GenerateExtendedKey(EXTENDED_KEY_CANCEL, 0);
			return true;
	}
}

bool
TUIInputBase::CUAToExtendedKeys::Accelerator(
	char32_t character
) {
	switch (character) {
		default:	return EventHandler::Accelerator(character);
		case 'W': case 'w':	GenerateExtendedKey(EXTENDED_KEY_CLOSE, 0U); return true;
		case 'Q': case 'q':	GenerateExtendedKey(EXTENDED_KEY_EXIT, 0U); return true;
	}
}

bool
TUIInputBase::CUAToExtendedKeys::FunctionKey(
	uint_fast16_t k,
	uint_fast8_t m
) {
	switch (k) {
		default:	return EventHandler::FunctionKey(k, m);
		case 1U:	GenerateExtendedKey(EXTENDED_KEY_HELP, m); return true;
		// In CUA F4 was save and exit, not just exit.
		case 4U:	GenerateExtendedKey(EXTENDED_KEY_SAVE, m); [[clang::fallthrough]];
		case 3U:	GenerateExtendedKey(EXTENDED_KEY_EXIT, m); return true;
		// This is from Microsoft Windows and is not strictly a CUA thing.
		case 5U:	GenerateExtendedKey(EXTENDED_KEY_REFRESH, m); return true;
		case 10U:	GenerateExtendedKey(EXTENDED_KEY_MENU, m); return true;
	}
}

bool
TUIInputBase::CUAToExtendedKeys::ExtendedKey(
	uint_fast16_t k,
	uint_fast8_t m
) {
	switch (k) {
		default:	return EventHandler::ExtendedKey(k, m);
		case EXTENDED_KEY_PAD_F1:	GenerateExtendedKey(EXTENDED_KEY_HELP, m); return true;
		// In CUA F4 was save and exit, not just exit.
		case EXTENDED_KEY_PAD_F4:	GenerateExtendedKey(EXTENDED_KEY_SAVE, m); [[clang::fallthrough]];
		case EXTENDED_KEY_PAD_F3:	GenerateExtendedKey(EXTENDED_KEY_EXIT, m); return true;
		// This is from Microsoft Windows and is not strictly a CUA thing.
		case EXTENDED_KEY_PAD_F5:	GenerateExtendedKey(EXTENDED_KEY_REFRESH, m); return false;
	}
}

// Seat of the class
TUIInputBase::CUAToExtendedKeys::CUAToExtendedKeys(TUIInputBase & i) : EventHandler(i) {}
TUIInputBase::CUAToExtendedKeys::~CUAToExtendedKeys() {}

bool
TUIInputBase::ControlCharactersToExtendedKeys::UCS3(
	char32_t character
) {
	switch (character) {
		default:	return EventHandler::UCS3(character);
		case IND:	GenerateExtendedKey(EXTENDED_KEY_DOWN_ARROW,0U); return true;
		case RI:	GenerateExtendedKey(EXTENDED_KEY_UP_ARROW,0U); return true;
		/// We have sent the right DECBKM and XTerm private mode sequences to ensure that these are the case.
		/// @{
		case BS:	GenerateExtendedKey(EXTENDED_KEY_BACKSPACE,0); return true;
		case NEL:
		case LF:	GenerateExtendedKey(EXTENDED_KEY_RETURN_OR_ENTER,INPUT_MODIFIER_CONTROL); return true;
		case CR:	GenerateExtendedKey(EXTENDED_KEY_RETURN_OR_ENTER,0U); return true;
		case DEL:	GenerateExtendedKey(EXTENDED_KEY_BACKSPACE,INPUT_MODIFIER_LEVEL2); return true;
		case TAB:	GenerateExtendedKey(EXTENDED_KEY_TAB,0U); return true;
		/// @}
	}
}

// Seat of the class
TUIInputBase::ControlCharactersToExtendedKeys::ControlCharactersToExtendedKeys(TUIInputBase & i) : EventHandler(i) {}
TUIInputBase::ControlCharactersToExtendedKeys::~ControlCharactersToExtendedKeys() {}

bool
TUIInputBase::VIMToExtendedKeys::UCS3(
	char32_t character
) {
	switch (character) {
		default:	return ControlCharactersToExtendedKeys::UCS3(character);
		case '^':	GenerateExtendedKey(EXTENDED_KEY_HOME, 0U); return true;
		case '$':	GenerateExtendedKey(EXTENDED_KEY_END, 0U); return true;
		case '/':	GenerateExtendedKey(EXTENDED_KEY_FIND, 0U); return true;
		case 'J': case 'j':	GenerateExtendedKey(EXTENDED_KEY_UP_ARROW, 0U); return true;
		case 'K': case 'k':	GenerateExtendedKey(EXTENDED_KEY_DOWN_ARROW, 0U); return true;
		case 'H': case 'h':	GenerateExtendedKey(EXTENDED_KEY_LEFT_ARROW, 0U); return true;
		case 'L': case 'l':	GenerateExtendedKey(EXTENDED_KEY_RIGHT_ARROW, 0U); return true;
		case ETX:	// Control+C
		case FS:	// Control+\ .
			GenerateExtendedKey(EXTENDED_KEY_CANCEL, 0);
			return true;
		case EM:	// Control+Y
		case SUB:	// Control+Z
			GenerateExtendedKey(EXTENDED_KEY_STOP, 0U);
			return true;
		case NAK:	// Control+U
		case STX:	// Control+B
			GenerateExtendedKey(EXTENDED_KEY_PAGE_UP, 0U);
			return true;
		case EOT:	// Control+D
		case ACK:	// Control+F
			GenerateExtendedKey(EXTENDED_KEY_PAGE_DOWN, 0U);
			return true;
		case FF:	// Control+L
		case DC2:	// Control+R
			GenerateExtendedKey(EXTENDED_KEY_REFRESH, 0U);
			return true;
	}
}

// Seat of the class
TUIInputBase::VIMToExtendedKeys::VIMToExtendedKeys(TUIInputBase & i) : ControlCharactersToExtendedKeys(i) {}
TUIInputBase::VIMToExtendedKeys::~VIMToExtendedKeys() {}

bool
TUIInputBase::LessToExtendedKeys::UCS3(
	char32_t character
) {
	switch (character) {
		default:	return VIMToExtendedKeys::UCS3(character);
		case DLE:	// Control+P
			GenerateExtendedKey(EXTENDED_KEY_UP_ARROW, 0U);
			return true;
		case SO:	// Control+N
			GenerateExtendedKey(EXTENDED_KEY_DOWN_ARROW, 0U);
			return true;
		case SPC:	GenerateExtendedKey(EXTENDED_KEY_PAD_SPACE, 0U); return true;
		case '?':	GenerateExtendedKey(EXTENDED_KEY_HELP, 0U); return true;
		case '-':	GenerateExtendedKey(EXTENDED_KEY_PAD_MINUS, 0); return true;
		case '+':	GenerateExtendedKey(EXTENDED_KEY_PAD_PLUS, 0); return true;
		case 'Q': case 'q':	GenerateExtendedKey(EXTENDED_KEY_EXIT, 0U); return true;
	}
}

// Seat of the class
TUIInputBase::LessToExtendedKeys::LessToExtendedKeys(TUIInputBase & i) : VIMToExtendedKeys(i) {}
TUIInputBase::LessToExtendedKeys::~LessToExtendedKeys() {}

bool
TUIInputBase::EMACSToCursorKeypad::UCS3(
	char32_t character
) {
	switch (character) {
		default:	return ControlCharactersToExtendedKeys::UCS3(character);
		case SOH:	// Control+A
			GenerateExtendedKey(EXTENDED_KEY_HOME, INPUT_MODIFIER_CONTROL);
			return true;
		case ENQ:	// Control+E
			GenerateExtendedKey(EXTENDED_KEY_END, INPUT_MODIFIER_CONTROL);
			return true;
	}
}

// Seat of the class
TUIInputBase::EMACSToCursorKeypad::EMACSToCursorKeypad(TUIInputBase & i) : ControlCharactersToExtendedKeys(i) {}
TUIInputBase::EMACSToCursorKeypad::~EMACSToCursorKeypad() {}

bool
TUIInputBase::LineDisciplineToExtendedKeys::UCS3(
	char32_t character
) {
	switch (character) {
		default:	return ControlCharactersToExtendedKeys::UCS3(character);
		case ETX:	// Control+C
		case FS:	// Control+\ .
			GenerateExtendedKey(EXTENDED_KEY_CANCEL, 0);
			return true;
		case EOT:	// Control+D
			GenerateExtendedKey(EXTENDED_KEY_EXIT, 0);
			return true;
		case EM:	// Control+Y
		case SUB:	// Control+Z
			GenerateExtendedKey(EXTENDED_KEY_STOP, 0);
			return true;
		case NAK:	// Control+U
			GenerateExtendedKey(EXTENDED_KEY_BACKSPACE, INPUT_MODIFIER_CONTROL);
			return true;
		case FF:	// Control+L
		case DC2:	// Control+R
			GenerateExtendedKey(EXTENDED_KEY_REFRESH, 0U);
			return true;
	}
}

// Seat of the class
TUIInputBase::LineDisciplineToExtendedKeys::LineDisciplineToExtendedKeys(TUIInputBase & i) : ControlCharactersToExtendedKeys(i) {}
TUIInputBase::LineDisciplineToExtendedKeys::~LineDisciplineToExtendedKeys() {}
