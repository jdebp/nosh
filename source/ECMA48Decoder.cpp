/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <iostream>
#include <stdint.h>
#include "ECMA48Decoder.h"
#include "ControlCharacters.h"

ECMA48Decoder::ECMA48ControlSequenceSink::ECMA48ControlSequenceSink() :
	argc(0U),
	seen_arg_digit(false),
	first_private_parameter(NUL),
	last_intermediate(NUL),
	slen(0U)
{
	args[argc] = 0;
}

void 
ECMA48Decoder::ECMA48ControlSequenceSink::FinishArg(unsigned int d)
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

void 
ECMA48Decoder::ECMA48ControlSequenceSink::ResetControlSeqOrStr()
{
	argc = 0U;
	seen_arg_digit = false;
	args[argc] = 0U;
	first_private_parameter = NUL;
	last_intermediate = NUL;
	slen = 0U;
}

ECMA48Decoder::ECMA48Decoder(
	ECMA48ControlSequenceSink & s,
	bool cs,
	bool is,
	bool rx
) :
	sink(s),
	state(NORMAL),
	control_strings(cs),
	interix_shift(is),
	rxvt_function_keys(rx)
{
	sink.ResetControlSeqOrStr();
}

inline
bool 
ECMA48Decoder::IsControl(uint_fast32_t character)
{
	return (character < 0x20) || (character >= 0x80 && character < 0xA0) || (DEL == character);
}

inline
bool 
ECMA48Decoder::IsInStringControl(uint_fast32_t character)
{	
	// BS, HT, LF, VT, FF, and CR are part of a control string, not standalone control characters.
	return !(character >= 0x08 && character < 0x0E) && IsControl(character);
}

inline
bool 
ECMA48Decoder::IsIntermediate(uint_fast32_t character)
{
	return character >= 0x20 && character < 0x30;
}

inline
bool 
ECMA48Decoder::IsParameter(uint_fast32_t character)
{
	return character >= 0x30 && character < 0x40;
}

void
ECMA48Decoder::AbortSequence(
) {
	// Send the previous introducer character onwards if we are interrupting an escape sequence or control sequence.
	// This is important for ECMA-48 input processing when handling pasted input or when handling a true ESC keypress.
	// We don't worry about the intermediate and parameter characters, though.
	// In neither input nor output ECMA-48 processing is anything interested in preserving them.
	switch (state) {
		default:			break;
		case ESCAPE1: case ESCAPE2:	sink.ControlCharacter(ESC); break;
		case CONTROL1: case CONTROL2:	sink.ControlCharacter(CSI); break;
	}
	state = NORMAL;
}

inline
void 
ECMA48Decoder::ControlCharacter(uint_fast32_t character)
{
	// Starting an escape sequence, a control sequence, or a control string aborts any that is in progress.
	switch (character) {
		case DCS:
		case OSC:
		case PM:
		case APC:
		case SOS:
		case ST:
			if (!control_strings) break;	// These are not aborts if control strings are not being recognized.
			[[clang::fallthrough]];
		case ESC:
		case CSI:
			AbortSequence();
			break;
		default:
			break;
	}
	switch (character) {
		case SSA:
			// Pretend that Start of Selected Area is Shift State A.
			if (interix_shift) {
				state = SHIFTA;
				break;
			}
			[[clang::fallthrough]];
		default:	sink.ControlCharacter(character); break;
	// The sink will never see any of these control characters.
		case ESC:	state = ESCAPE1; sink.last_intermediate = NUL; break;
		case CSI:	state = CONTROL1; sink.ResetControlSeqOrStr(); break;
		case SS2:	state = SHIFT2; break;
		case SS3:	state = SHIFT3; break;
		case CAN:	state = NORMAL; break;
	// The sink will never see any of these control characters, even if control strings are not being recognized.
		case DCS:	if (control_strings) { state = DSTRING; sink.ResetControlSeqOrStr(); } break;
		case OSC:	if (control_strings) { state = OSTRING; sink.ResetControlSeqOrStr(); } break;
		case PM:	if (control_strings) { state = PSTRING; sink.ResetControlSeqOrStr(); } break;
		case APC:	if (control_strings) { state = ASTRING; sink.ResetControlSeqOrStr(); } break;
		case SOS:	if (control_strings) { state = SSTRING; sink.ResetControlSeqOrStr(); } break;
		case ST:	if (control_strings) { state = NORMAL; } break;
	}
}

inline
void 
ECMA48Decoder::Escape1(uint_fast32_t character)
{
	if (IsControl(character))
		ControlCharacter(character);
	else
	if (IsIntermediate(character)) {
		sink.last_intermediate = static_cast<char>(character);
		state = ESCAPE2;
	} else
	if (IsParameter(character)) {
		// ECMA-48 does not define this.
		state = ESCAPE2;
	} else
	if (character >= 0x40 && character <= 0x5f) {
		// Do this first, so that it can be overridden by the control character processing.
		state = NORMAL;
		// This is known as "7-bit code extension" and is defined for the entire range.
		ControlCharacter(character + 0x40); 
	} else
	{
		sink.EscapeSequence(character);
		state = NORMAL;
	}
}

inline
void 
ECMA48Decoder::Escape2(uint_fast32_t character)
{
	if (IsControl(character))
		ControlCharacter(character);
	else
	if (IsIntermediate(character))
		sink.last_intermediate = static_cast<char>(character);
	else
	if (IsParameter(character)) {
		// ECMA-48 does not define this.
	} else
	{
		sink.EscapeSequence(character);
		state = NORMAL;
	}
}

inline
void 
ECMA48Decoder::ControlSequence(uint_fast32_t character)
{
	if (IsControl(character)) {
		ControlCharacter(character);
	} else
	if (IsParameter(character)) {
		if (CONTROL1 != state) {
			std::clog << "Out of sequence CSI parameter character : " << character << "\n";
			state = NORMAL; 
		} else
		switch (character) {
			// Accumulate digits in arguments.
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				sink.seen_arg_digit = true;
				sink.args[sink.argc] = sink.args[sink.argc] > 999U ? 10000U : sink.args[sink.argc] * 10U + (character - '0');
				break;
			// ECMA-48 defines colon as a sub-argument delimiter.
			// No-one uses it; albeit that it is defined for ISO 8613-3 SGR 38/48 sequences.
			case ':':
				sink.seen_arg_digit = false;
				sink.args[sink.argc] = 0U;
				break;
			// Arguments may be semi-colon separated.
			case ';':
				sink.FinishArg(0U); 
				break;
			// Everything else up to U+002F is a private parameter character, per ECMA-48 5.4.1.
			// DEC VTs make use of '<', '=', '>', and '?'.
			default:
				if (NUL == sink.first_private_parameter)
					sink.first_private_parameter = static_cast<char>(character);
				break;
		}
	} else
	if (IsIntermediate(character) && !(rxvt_function_keys && 0x24 == character)) {
		sink.last_intermediate = static_cast<char>(character);
		state = CONTROL2;
	} else
	{
		sink.ControlSequence(character);
		state = NORMAL; 
	}
}

inline
void 
ECMA48Decoder::ControlString(uint_fast32_t character)
{
	if (IsInStringControl(character)) {
		ControlCharacter(character);
		return;
	} else
	if (ST != character) {
		if (sink.slen < sizeof sink.str/sizeof *sink.str) {
			sink.str[sink.slen++] = character;
		}
		return;
	}

	switch (state) {
		default:	break;
		case DSTRING:	sink.ControlString(DCS); break;
		case OSTRING:	sink.ControlString(OSC); break;
		case PSTRING:	sink.ControlString(PM); break;
		case ASTRING:	sink.ControlString(APC); break;
		case SSTRING:	sink.ControlString(SOS); break;
	}
	state = NORMAL; 
}

void
ECMA48Decoder::Process(
	uint_fast32_t character,
	bool decoder_error,
	bool overlong
) {
	switch (state) {
		case NORMAL:
		case SHIFT2:
		case SHIFT3:
		case SHIFTA:
			if (decoder_error || overlong)
				sink.PrintableCharacter(decoder_error, 0U, character);
			else if (IsControl(character))
				ControlCharacter(character);
			else switch (state) {
				case NORMAL:	sink.PrintableCharacter(false, 1U, character); break;
				case SHIFT2:	sink.PrintableCharacter(false, 2U, character); break;
				case SHIFT3:	sink.PrintableCharacter(false, 3U, character); break;
				case SHIFTA:	sink.PrintableCharacter(false, 10U, character); break;
			}
			break;
		case ESCAPE1:
			if (decoder_error)
				state = NORMAL;
			else if (overlong) {
				state = NORMAL;
				sink.PrintableCharacter(false, 0U, character);
			} else
				Escape1(character);
			break;
		case ESCAPE2:
			if (decoder_error)
				state = NORMAL;
			else if (overlong) {
				state = NORMAL;
				sink.PrintableCharacter(false, 0U, character);
			} else
				Escape2(character);
			break;
		case CONTROL1:
		case CONTROL2:
			if (decoder_error)
				state = NORMAL;
			else if (overlong) {
				state = NORMAL;
				sink.PrintableCharacter(false, 0U, character);
			} else
				ControlSequence(character);
			break;
		case DSTRING:
		case OSTRING:
		case PSTRING:
		case ASTRING:
		case SSTRING:
			if (decoder_error)
				state = NORMAL;
			else if (overlong) {
				state = NORMAL;
				sink.PrintableCharacter(false, 0U, character);
			} else
				ControlString(character);
			break;
	}
}
