/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <iostream>
#include <stdint.h>
#include "ECMA48Decoder.h"
#include "ControlCharacters.h"

namespace {
	enum {
		MAXIMUM_SGR_LENGTH  = 64U
	};

	inline
	bool
	IsControl(uint_fast32_t character)
	{
		return (character < SPC) || (character >= 0x80 && character < 0xA0) || (DEL == character);
	}

	inline
	bool
	IsIntermediate(uint_fast32_t character)
	{
		return character >= SPC && character < 0x30;
	}

	inline
	bool
	IsParameter(uint_fast32_t character)
	{
		return character >= 0x30 && character < 0x40;
	}

	inline
	bool
	IsAlways7BitExtension (
		uint_fast32_t character
	) {
		return (CSI - 0x40) == character;
	}
}

ECMA48Decoder::ECMA48ControlSequenceSink::ECMA48ControlSequenceSink(
) :
	args(),
	slen(0U),
	zero_replacement(0U)
{
}

#if 0
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
#endif

void
ECMA48Decoder::ECMA48ControlSequenceSink::ResetControlSequenceArgs()
{
	args.clear();
}

void
ECMA48Decoder::ECMA48ControlSequenceSink::ResetControlString()
{
	slen = 0U;
}

void
ECMA48Decoder::ECMA48ControlSequenceSink::AppendArgDigit(unsigned digit)
{
	if (args.empty()) args.push_back(Sublist());
	if (args.size() > MAXIMUM_SGR_LENGTH) return;
	Sublist & s(args.back());
	if (s.empty()) s.push_back(Number());
	if (s.size() > MAXIMUM_SGR_LENGTH) return;
	Number & n(s.back());
	if (n.isnull) {
		n.value = 0U;
		n.isnull = false;
	}
	n.value = n.value >= (n.max() / 10U) ? n.max() : n.value * 10U + digit;
}

void
ECMA48Decoder::ECMA48ControlSequenceSink::Colon()
{
	if (args.empty()) args.push_back(Sublist());
	if (args.size() > MAXIMUM_SGR_LENGTH) return;
	Sublist & s(args.back());
	if (s.empty()) s.push_back(Number());
	if (s.size() > MAXIMUM_SGR_LENGTH) return;
	s.push_back(Number());
}

void
ECMA48Decoder::ECMA48ControlSequenceSink::SemiColon()
{
	if (args.empty()) args.push_back(Sublist());
	if (args.size() > MAXIMUM_SGR_LENGTH) return;
	args.push_back(Sublist());
}

void
ECMA48Decoder::ECMA48ControlSequenceSink::MinimumOneArg()
{
	if (args.empty()) args.push_back(Sublist());
}

bool
ECMA48Decoder::ECMA48ControlSequenceSink::HasNoSubArgsFrom(std::size_t sub)
{
	if (args.size() <= sub) return true;
	for (Arguments::iterator i(args.begin() + sub); i != args.end(); ++i) {
		Sublist & s(*i);
		if (s.size() > 1U)
			return false;
	}
	return true;
}

void
ECMA48Decoder::ECMA48ControlSequenceSink::CollapseArgsToSubArgs(std::size_t sub)
{
	if (args.size() <= sub) return;
	Arguments::iterator i(args.begin() + sub);
	Sublist & d(*i++);
	while (i != args.end()) {
		Sublist & s(*i);
		if (s.empty())
			d.push_back(Number());
		else
			// Only append the first item.
			d.push_back(s.front());
		i = args.erase(i);
	}
}

void
ECMA48Decoder::ECMA48ControlSequenceSink::AppendControlString(char32_t character)
{
	if (slen < sizeof str/sizeof *str)
		str[slen++] = character;
}

ECMA48Decoder::ECMA48ControlSequenceSink::argument_type
ECMA48Decoder::ECMA48ControlSequenceSink::GetArgThisIfEmpty(std::size_t sub, std::size_t index, argument_type d) const
{
	if (args.size() <= sub) return d;
	const Sublist & s(args[sub]);
	if (s.size() <= index) return d;
	const Number & n(s[index]);
	if (n.isnull) return d;
	return n.value;
}

ECMA48Decoder::ECMA48ControlSequenceSink::argument_type
ECMA48Decoder::ECMA48ControlSequenceSink::GetArgThisIfZeroThisIfEmpty(std::size_t sub, std::size_t index, argument_type dz, argument_type de) const
{
	if (args.size() <= sub) return de;
	const Sublist & s(args[sub]);
	if (s.size() <= index) return de;
	const Number & n(s[index]);
	if (n.isnull) return de;
	return 0U == n.value ? dz : n.value;
}

std::size_t
ECMA48Decoder::ECMA48ControlSequenceSink::QueryArgCount() const
{
	return args.size();
}

std::size_t
ECMA48Decoder::ECMA48ControlSequenceSink::QuerySubArgCount(std::size_t sub) const
{
	if (args.size() <= sub) return 0;
	const Sublist & s(args[sub]);
	return s.size();
}

bool
ECMA48Decoder::ECMA48ControlSequenceSink::QueryArgNull(std::size_t sub, std::size_t index) const
{
	if (args.size() <= sub) return true;
	const Sublist & s(args[sub]);
	if (s.size() <= index) return true;
	const Number & n(s[index]);
	return n.isnull;
}

ECMA48Decoder::ECMA48Decoder(
	ECMA48ControlSequenceSink & s,
	const Options & o
) :
	sink(s),
	state(NORMAL),
	options(o),
	first_private_parameter(NUL),
	saved_intermediate(NUL),
	string_char(NUL)
{
	sink.ResetControlSequenceArgs();
	sink.ResetControlString();
}

void
ECMA48Decoder::AbortSequence(
) {
	// Send the previous introducer character onwards if we are interrupting an escape sequence or control sequence.
	// This is important for ECMA-48 input processing when handling pasted input or when handling a true ESC keypress.
	// We don't worry about the intermediate and parameter characters, though.
	// In neither input nor output ECMA-48 processing is anything interested in preserving them.
	switch (state) {
		case NORMAL: case SHIFT2: case SHIFT3: case SHIFTA: case SHIFTL:
#if 0 // Generates a compiler warning, when we explicitly list impossible states to avoid a different compiler warning.
		default:
#endif
			break;
		case ESCAPE: case ESCAPE_NF:			sink.ControlCharacter(ESC); break;
		case CONTROL1: case CONTROL2:			sink.ControlCharacter(CSI); break;
		case CONTROLSTRING: case CONTROLSTRINGESCAPE:	sink.ControlCharacter(string_char); break;
	}
	state = NORMAL;
}

void
ECMA48Decoder::TerminateSequence(
	char32_t term_char
) {
	switch (state) {
		case NORMAL: case SHIFT2: case SHIFT3: case SHIFTA: case SHIFTL:
#if 0 // Generates a compiler warning, when we explicitly list impossible states to avoid a different compiler warning.
		default:
#endif
			break;
		case ESCAPE: case ESCAPE_NF:			break;
		case CONTROL1: case CONTROL2:			break;
		case CONTROLSTRING: case CONTROLSTRINGESCAPE:	sink.ControlString(string_char, term_char); break;
	}
	state = NORMAL;
}

void
ECMA48Decoder::ResetControlSeqAndStr()
{
	first_private_parameter = NUL;
	saved_intermediate = NUL;
	string_char = NUL;
	sink.ResetControlSequenceArgs();
	sink.ResetControlString();
}

inline
void
ECMA48Decoder::ControlCharacter(char32_t character)
{
	// Starting an escape sequence, a control sequence, or a control string aborts any that is in progress.
	switch (character) {
		case DCS:
		case OSC:
		case PM:
		case APC:
		case SOS:
			if (!options.control_strings) break;	// These are not aborts if control strings are not being recognized.
			[[clang::fallthrough]];
		case CSI:
			AbortSequence();
			break;
		case ESC:
			if (CONTROLSTRING != state) AbortSequence();
			break;
		case ST:
			TerminateSequence(character);
			break;
		default:
			break;
	}
	switch (character) {
		case SSA:
			// Pretend that Start of Selected Area is Shift State A.
			if (options.interix_shift) {
				state = SHIFTA;
				break;
			}
			[[clang::fallthrough]];
		default:	sink.ControlCharacter(character); break;
	// The sink might never see any of these control characters.
		case CAN:	if (options.allow_cancel) state = NORMAL; else sink.ControlCharacter(character); break;
	// The sink will never see any of these control characters, unless a sequence is aborted partway.
		case ESC:	state = CONTROLSTRING == state ? CONTROLSTRINGESCAPE : ESCAPE; saved_intermediate = NUL; break;
		case CSI:	state = CONTROL1; ResetControlSeqAndStr(); break;
		case SS2:	state = SHIFT2; break;
		case SS3:	state = SHIFT3; break;
	// The sink will never see any of these control characters, unless control strings are not being recognized, or a sequence is aborted partway.
		case DCS:
		case OSC:
		case PM:
		case APC:
		case SOS:
			if (options.control_strings) {
				state = CONTROLSTRING;
				ResetControlSeqAndStr();
				string_char = character;
			} else
				sink.ControlCharacter(character);
			break;
		case ST:
			if (options.control_strings) {
				state = NORMAL;
			} else
				sink.ControlCharacter(character);
			break;
	}
}

inline
void
ECMA48Decoder::Escape(char32_t character)
{
	if (IsControl(character))
		ControlCharacter(character);
	else
	if (IsIntermediate(character)) {
		saved_intermediate = character;
		state = ESCAPE_NF;
	} else
	if (IsParameter(character)) {
		// ECMA-35 "private control function" (Fp) escape sequence.
		sink.EscapeSequence(character, saved_intermediate);
		state = NORMAL;
	} else
	if (character >= 0x40 && character <= 0x5f) {
		if (options.allow_7bit_extension || IsAlways7BitExtension(character)) {
			// Do this first, so that it can be overridden by the control character processing.
			state = NORMAL;
			// This is known as ECMA-35 "7-bit code extension" (Fe) and is defined for the entire range.
			ControlCharacter(character + 0x40);
		} else {
			sink.EscapeSequence(character, saved_intermediate);
			state = NORMAL;
		}
	} else
	{
		// ECMA-35 "standardized single control function" (Fs) escape sequence.
		sink.EscapeSequence(character, saved_intermediate);
		state = NORMAL;
	}
}

// See ECMA-35 section 13.2.2 for the meaning of "nF".
inline
void
ECMA48Decoder::Escape_nF(char32_t character)
{
	if (IsControl(character))
		ControlCharacter(character);
	else
	if (IsIntermediate(character)) {
		// Keep the first intermediate, rather than the last.
	} else
	if (IsParameter(character)) {
		// ECMA-35 "reserved for private use" nF escape sequence.
		sink.EscapeSequence(character, saved_intermediate);
		state = NORMAL;
	} else
	{
		// ECMA-35 "registered single control function" nF escape sequence.
		sink.EscapeSequence(character, saved_intermediate);
		state = NORMAL;
	}
}

inline
void
ECMA48Decoder::ControlSequence(char32_t character)
{
	if (IsControl(character))
		ControlCharacter(character);
	else
	if (IsParameter(character)) {
		if (CONTROL1 != state) {
			std::clog << "Out of sequence CSI parameter character : " << character << "\n";
			state = NORMAL;
		} else
		switch (character) {
			// Accumulate digits in arguments.
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				sink.AppendArgDigit(character - '0');
				break;
			// ECMA-48 defines colon as a sub-argument delimiter.
			// It is defined for ISO 8613-3/ITU T.416 SGR 38/48 sequences.
			// It is also used in (our and other people's) various extensions to ECMA-48.
			case ':':
				sink.Colon();
				break;
			// Arguments may be semi-colon separated.
			case ';':
				sink.SemiColon();
				break;
			// Everything else up to U+002F is a private parameter character, per ECMA-48 5.4.1.
			// DEC VTs make use of '<', '=', '>', and '?'.
			default:
				if (NUL == first_private_parameter)
					first_private_parameter = character;
				break;
		}
	} else
	if (IsIntermediate(character) && !(options.rxvt_function_keys && 0x24 == character)) {
		saved_intermediate = character;
		state = CONTROL2;
	} else
	if (options.linux_function_keys && '[' == character && NUL == saved_intermediate && NUL == first_private_parameter) {
		// Pretend that SRS is Shift State L.
		state = SHIFTL;
	} else
	{
		sink.ControlSequence(character, saved_intermediate, first_private_parameter);
		state = NORMAL;
	}
}

inline
void
ECMA48Decoder::ControlString(char32_t character)
{
	// BS, HT, LF, VT, FF, CR, and ST are part of a control string, not standalone control characters.
	if (character >= 0x08 && character < 0x0E)
		sink.AppendControlString(character);
	else
	if (IsControl(character))
		ControlCharacter(character);
	else
		sink.AppendControlString(character);
}

inline
void
ECMA48Decoder::ControlStringEscape(char32_t character)
{
	// BS, HT, LF, VT, FF, CR, and ST are part of a control string, not standalone control characters.
	if (character >= 0x08 && character < 0x0E)
		sink.AppendControlString(character);
	else
	if (IsControl(character))
		ControlCharacter(character);
	else
	if (IsIntermediate(character))
		;	// Ignore intermediate characters.
	else
	if (IsParameter(character))
		// ECMA-35 "private control function" (Fp) escape sequence.
		;	// Ignore inside control strings.
	else
	if (character >= 0x40 && character <= 0x5f) {
		if (options.allow_7bit_extension || IsAlways7BitExtension(character)) {
			// This is known as ECMA-35 "7-bit code extension" (Fe) and is defined for the entire range.
			ControlCharacter(character + 0x40);
		} else {
			// Ignore inside control strings.
		}
	} else
	{
		// ECMA-35 "standardized single control function" (Fs) escape sequence.
		// Ignore inside control strings.
	}
}

void
ECMA48Decoder::Process(
	char32_t character,
	bool decoder_error,
	bool overlong
) {
	switch (state) {
		case NORMAL:
		case SHIFT2:
		case SHIFT3:
		case SHIFTA:
		case SHIFTL:
			if (decoder_error || overlong) {
				sink.PrintableCharacter(decoder_error, 0U, character);
				state = NORMAL;
			} else
			if (IsControl(character)) {
				switch (state) {
					case ESCAPE: case ESCAPE_NF: case CONTROL1: case CONTROL2: case CONTROLSTRING: case CONTROLSTRINGESCAPE: break;
					case NORMAL:	break;
					case SHIFT2:	sink.ControlCharacter(SS2); break;
					case SHIFT3:	sink.ControlCharacter(SS3); break;
					case SHIFTA:	sink.ControlCharacter(SSA); break;
					case SHIFTL:	break;
				}
				// Do this first, so that it can be overridden by the control character processing.
				state = NORMAL;
				ControlCharacter(character);
			} else
			{
				switch (state) {
					case ESCAPE: case ESCAPE_NF: case CONTROL1: case CONTROL2: case CONTROLSTRING: case CONTROLSTRINGESCAPE: break;
					case NORMAL:	sink.PrintableCharacter(false, 1U, character); break;
					case SHIFT2:	sink.PrintableCharacter(false, 2U, character); break;
					case SHIFT3:	sink.PrintableCharacter(false, 3U, character); break;
					case SHIFTA:	sink.PrintableCharacter(false, 10U, character); break;
					case SHIFTL:	sink.PrintableCharacter(false, 12U, character); break;
				}
				state = NORMAL;
			}
			break;
		case ESCAPE:
			if (decoder_error)
				state = NORMAL;
			else
			if (overlong) {
				sink.PrintableCharacter(false, 0U, character);
				state = NORMAL;
			} else
				Escape(character);
			break;
		case ESCAPE_NF:
			if (decoder_error)
				state = NORMAL;
			else if (overlong) {
				sink.PrintableCharacter(false, 0U, character);
				state = NORMAL;
			} else
				Escape_nF(character);
			break;
		case CONTROL1:
		case CONTROL2:
			if (decoder_error)
				state = NORMAL;
			else
			if (overlong) {
				sink.PrintableCharacter(false, 0U, character);
				state = NORMAL;
			} else
				ControlSequence(character);
			break;
		case CONTROLSTRING:
			if (decoder_error)
				state = NORMAL;
			else
			if (overlong) {
				sink.PrintableCharacter(false, 0U, character);
				state = NORMAL;
			} else
				ControlString(character);
			break;
		case CONTROLSTRINGESCAPE:
			if (decoder_error)
				state = NORMAL;
			else
			if (overlong) {
				sink.PrintableCharacter(false, 0U, character);
				state = NORMAL;
			} else
				ControlStringEscape(character);
			break;
	}
}
