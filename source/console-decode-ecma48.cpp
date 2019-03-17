/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>
#include <stdint.h>
#include <cctype>
#include <cerrno>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "UTF8Decoder.h"
#include "ECMA48Decoder.h"
#include "FileDescriptorOwner.h"
#include "InputMessage.h"
#include "TerminalCapabilities.h"
#include "ProcessEnvironment.h"
#include "ControlCharacters.h"

namespace {
class Decoder :
	public TerminalCapabilities,
	public UTF8Decoder::UCS32CharacterSink,
	public ECMA48Decoder::ECMA48ControlSequenceSink
{
public:
	Decoder(const char *, const ProcessEnvironment &, bool, bool, bool);
	~Decoder();
	bool process (const char * name, int fd);
protected:
	UTF8Decoder utf8_decoder;
	ECMA48Decoder ecma48_decoder;
	const char * const prog;
	const bool input, no_7bit;

	virtual void ProcessDecodedUTF8(uint32_t character, bool decoder_error, bool overlong);
	virtual void PrintableCharacter(bool, unsigned short, uint_fast32_t);
	virtual void ControlCharacter(uint_fast32_t);
	virtual void EscapeSequence(uint_fast32_t, char);
	virtual void ControlSequence(uint_fast32_t, char, char);
	virtual void ControlString(uint_fast32_t);

	void out(const char *);
	void csi(const char *);
	void csi_sumargs(const char *);
	void csi_fnk(const char *);
	void csi_unknown(uint_fast32_t, char, char);
	void plainchar(uint_fast32_t, char);
	void dec_modifier(unsigned);
	void keychord(const char *, const char *, unsigned);
	void fkey(const char *, unsigned, unsigned);
	void fkey(const char *, unsigned);
	void fnk(const char *, unsigned);
	void iso2022(const char *, uint_fast32_t);
};
}

Decoder::Decoder(
	const char * p,
	const ProcessEnvironment & envs,
	bool i,
	bool permit_cancel,
	bool permit_7bit_extensions
) : 
	TerminalCapabilities(envs),
	utf8_decoder(*this),
	ecma48_decoder(*this, false /* no control strings */, permit_cancel, permit_7bit_extensions, interix_function_keys, rxvt_function_keys),
	prog(p),
	input(i),
	no_7bit(!permit_7bit_extensions)
{
}

Decoder::~Decoder(
) {
	ecma48_decoder.AbortSequence();
}

void 
Decoder::ProcessDecodedUTF8(
	uint32_t character,
	bool decoder_error,
	bool overlong
) {
	ecma48_decoder.Process(character, decoder_error, overlong);
}

void
Decoder::out (
	const char * name
) {
	std::fprintf(stdout, "%s\n", name);
}

void
Decoder::csi (
	const char * name
) {
	std::fprintf(stdout, "%s ", name);
	for (std::size_t i(0U); i < argc; ++i)
		std::fprintf(stdout, "%s%u", i?";":"", args[i]);
	std::fprintf(stdout, "\n");
}

void
Decoder::csi_sumargs (
	const char * name
) {
	if (argc) {
		unsigned s(0U);
		for (std::size_t i(0U); i < argc; ++i)
			s += args[i];
		if (0U == s) s = 1U;
		std::fprintf(stdout, "%s %u\n", name, s);
	} else
		std::fprintf(stdout, "%s\n", name);
}

void
Decoder::csi_unknown (
	uint_fast32_t character,
	char last_intermediate,
	char first_private_parameter
) {
	std::fprintf(stdout, "unknown CSI ");
	if (first_private_parameter) plainchar(first_private_parameter, ' ');
	for (std::size_t i(0U); i < argc; ++i)
		std::fprintf(stdout, "%s%u", i?";":"", args[i]);
	if (argc) std::fprintf(stdout, " ");
	if (last_intermediate) plainchar(last_intermediate, ' ');
	plainchar(character, '\n');
}

void
Decoder::plainchar(
	uint_fast32_t character,
	char suffix
) {
	if (' ' <= character && character < DEL)
		std::fprintf(stdout, "'%c'%c", static_cast<char>(character), suffix);
	else
		std::fprintf(stdout, "U+%08" PRIxFAST32 "%c", character, suffix);
}

inline
void
Decoder::dec_modifier(
	unsigned mod
) {
	if (mod & INPUT_MODIFIER_CONTROL) std::fprintf(stdout, "Control+");
	if (mod & INPUT_MODIFIER_LEVEL2) std::fprintf(stdout, "Level2+");
	if (mod & INPUT_MODIFIER_LEVEL3) std::fprintf(stdout, "Level3+");
}

void
Decoder::keychord(
	const char * prefix,
	const char * name,
	unsigned mod
) {
	std::fprintf(stdout, "%s ", prefix);
	dec_modifier(mod);
	std::fprintf(stdout, "%s\n", name);
}

void
Decoder::fkey(
	const char * prefix,
	unsigned num,
	unsigned mod
) {
	char buf[64];
	std::snprintf(buf, sizeof buf, "F%u", num);
	keychord(prefix, buf, mod);
}

void
Decoder::fkey(
	const char * prefix,
	unsigned num
) {
	const unsigned scomod((num - 1U) / 12U);
	const unsigned mod(
		(scomod & 1U ? INPUT_MODIFIER_LEVEL2 : 0U) |
		(scomod & 2U ? INPUT_MODIFIER_CONTROL : 0U) |
		(scomod & 4U ? INPUT_MODIFIER_LEVEL3 : 0U) |
		0U
	);
	fkey(prefix, (num - 1U) % 12U + 1U, mod);
}

void
Decoder::fnk(
	const char * prefix,
	unsigned default_modifier
) {
	if (argc > 0U) {
		const uint_fast8_t m(argc < 2U ? default_modifier : 0U < args[1U] ? args[1U] - 1U : 0U);
		switch (args[0U]) {
			default:	std::fprintf(stdout, "%s FNK %u;%u\n", prefix, args[0], m + 1U); break;
			case 1U:	keychord(prefix,linux_editing_keypad ? "HOME" : "FIND", m); break;
			case 2U:	keychord(prefix,"INSERT",m); break;
			case 3U:	keychord(prefix,"DELETE",m); break;
			case 4U:	keychord(prefix,linux_editing_keypad ? "END" : "SELECT", m); break;
			case 5U:	keychord(prefix,"PAGE_UP",m); break;
			case 6U:	keychord(prefix,"PAGE_DOWN",m); break;
			case 7U:	keychord(prefix,linux_editing_keypad ? "FIND" : "HOME", m); break;
			case 8U:	keychord(prefix,linux_editing_keypad ? "SELECT" : "END", m); break;
			case 11U:	fkey(prefix,1U,m); break;
			case 12U:	fkey(prefix,2U,m); break;
			case 13U:	fkey(prefix,3U,m); break;
			case 14U:	fkey(prefix,4U,m); break;
			case 15U:	fkey(prefix,5U,m); break;
			case 17U:	fkey(prefix,6U,m); break;
			case 18U:	fkey(prefix,7U,m); break;
			case 19U:	fkey(prefix,8U,m); break;
			case 20U:	fkey(prefix,9U,m); break;
			case 21U:	fkey(prefix,10U,m); break;
			case 23U:	fkey(prefix,11U,m); break;
			case 24U:	fkey(prefix,12U,m); break;
			case 25U:	fkey(prefix,13U,m); break;
			case 26U:	fkey(prefix,14U,m); break;
			case 28U:	fkey(prefix,15U,m); break;
			case 29U:	fkey(prefix,16U,m); break;
			case 31U:	fkey(prefix,17U,m); break;
			case 32U:	fkey(prefix,18U,m); break;
			case 33U:	fkey(prefix,19U,m); break;
			case 34U:	fkey(prefix,20U,m); break;
			case 35U:	fkey(prefix,21U,m); break;
			case 36U:	fkey(prefix,22U,m); break;
		}
	} else
		std::fprintf(stdout, "%s FNK %u\n", prefix, args[0]);
}

void
Decoder::csi_fnk (
	const char * name
) {
	if (1U < argc) {
		const uint_fast8_t mod(0U < args[1U] ? args[1U] - 1U : 0U);
		dec_modifier(mod);
	}
	if (0U < argc) {
		const unsigned num(0U < args[0U] ? args[0U] : 1U);
		std::fprintf(stdout, "%s %u\n", name, num);
	} else
		std::fprintf(stdout, "%s\n", name);
}

void
Decoder::iso2022(
	const char * name,
	uint_fast32_t character
) {
	std::fprintf(stdout, "%s ", name);
	plainchar(character, '\n');
}

void
Decoder::PrintableCharacter(
	bool error,
	unsigned short shift_level,
	uint_fast32_t character
) {
	if (error)
		std::fprintf(stdout, "(error) ");
	if (10U == shift_level) {
		// The Interix system has no F0 ('0') and omits 'l' for some reason.
		if ('0' <  character && character <= '9')
			fkey("Interix", character - '0');
		else
		if ('A' <= character && character <= 'Z')
			fkey("Interix", character - 'A' + 10U);
		else
		if ('a' <= character && character <= 'k')
			fkey("Interix", character - 'a' + 36U);
		else
		if ('m' <= character && character <= 'z')
			fkey("Interix", character - 'm' + 47U);
		else {
			std::fprintf(stdout, "SSA");
			plainchar(character, '\n');
		}
	} else
	if (3U == shift_level) {
		if (rxvt_function_keys) switch (character) {
			case 'a':	keychord("rxvt", "KEY_PAD_UP", INPUT_MODIFIER_CONTROL); goto skip_dec;
			case 'b':	keychord("rxvt", "KEY_PAD_DOWN", INPUT_MODIFIER_CONTROL); goto skip_dec;
			case 'c':	keychord("rxvt", "KEY_PAD_RIGHT", INPUT_MODIFIER_CONTROL); goto skip_dec;
			case 'd':	keychord("rxvt", "KEY_PAD_LEFT", INPUT_MODIFIER_CONTROL); goto skip_dec;
		}
		switch (character) {
			default:	std::fprintf(stdout, "SS%hu ", shift_level); plainchar(character, '\n'); break;
			case 'j':	keychord("DEC", "KEY_PAD_ASTERISK", 0U); break;
			case 'k':	keychord("DEC", "KEY_PAD_PLUS", 0U); break;
			case 'l':	keychord("DEC", "KEY_PAD_COMMA", 0U); break;
			case 'm':	keychord("DEC", "KEY_PAD_MINUS", 0U); break;
			case 'n':	keychord("DEC", "KEY_PAD_DELETE", 0U); break;
			case 'o':	keychord("DEC", "KEY_PAD_SLASH", 0U); break;
			case 'p':	keychord("DEC", "KEY_PAD_INSERT", 0U); break;
			case 'q':	keychord("DEC", "KEY_PAD_END", 0U); break;
			case 'r':	keychord("DEC", "KEY_PAD_DOWN", 0U); break;
			case 's':	keychord("DEC", "KEY_PAD_PAGE_DOWN", 0U); break;
			case 't':	keychord("DEC", "KEY_PAD_LEFT", 0U); break;
			case 'u':	keychord("DEC", "KEY_PAD_CENTRE", 0U); break;
			case 'v':	keychord("DEC", "KEY_PAD_RIGHT", 0U); break;
			case 'w':	keychord("DEC", "KEY_PAD_HOME", 0U); break;
			case 'x':	keychord("DEC", "KEY_PAD_UP", 0U); break;
			case 'y':	keychord("DEC", "KEY_PAD_PAGE_UP", 0U); break;
			case 'M':	keychord("DEC", "KEY_PAD_ENTER", 0U); break;
			case 'P':	keychord("DEC", "KEY_PAD_F1", 0U); break;
			case 'Q':	keychord("DEC", "KEY_PAD_F2", 0U); break;
			case 'R':	keychord("DEC", "KEY_PAD_F3", 0U); break;
			case 'S':	keychord("DEC", "KEY_PAD_F4", 0U); break;
			case 'T':	keychord("DEC", "KEY_PAD_F5", 0U); break;
			case 'X':	keychord("DEC", "KEY_PAD_EQUALS", 0U); break;
		}
skip_dec:		;
	} else
	if (1U < shift_level) {
		std::fprintf(stdout, "SS%hu ", shift_level);
		plainchar(character, '\n');
	} else
	{
		plainchar(character, '\n');
	}
}

void
Decoder::ControlCharacter(
	uint_fast32_t character
) {
	switch (character) {
		default:	plainchar(character, '\n'); break;
		case NUL:	out("NUL"); break;
		case BEL:	out("BEL"); break;
		case CR:	out("CR"); break;
		case NEL:	out("NEL"); break;
		case IND:	out("IND"); break;
		case LF:	out("LF"); break;
		case VT:	out("VT"); break;
		case FF:	out("FF"); break;
		case RI:	out("RI"); break;
		case TAB:	out("TAB"); break;
		case BS:	out("BS"); break;
		case DEL:	out("DEL"); break;
		case HTS:	out("HTS"); break;
		case SSA:	out("SSA"); break;
		// These are dealt with by the ECMA-48 decoder itself, but we will print them if they ever reach us.
		case ESC:	out("ESC"); break;
		case CSI:	out("CSI"); break;
		case SS2:	out("SS2"); break;
		case SS3:	out("SS3"); break;
		case CAN:	out("CAN"); break;
		case DCS:	out("DCS"); break;
		case OSC:	out("OSC"); break;
		case PM:	out("PM"); break;
		case APC:	out("APC"); break;
		case SOS:	out("SOS"); break;
		case ST:	out("ST"); break;
	}
}

void
Decoder::EscapeSequence(
	uint_fast32_t character,
	char last_intermediate
) {
	switch (last_intermediate) {
		default:	std::fprintf(stdout, "ESC "); plainchar(last_intermediate, ' '); plainchar(character, '\n'); break;
		case NUL:
			if (no_7bit && input) {
				std::fprintf(stdout, "Meta "); plainchar(character, '\n');
			} else
			switch (character) {
				default:	std::fprintf(stdout, "ESC "); plainchar(character, '\n'); break;
				case '1':	out("DECGON"); break;
				case '2':	out("DECGOFF"); break;
				case '3':	out("DECVTS"); break;
				case '4':	out("DECCAVT"); break;
				case '5':	out("DECXMT"); break;
				case '6':	out("DECBI"); break;
				case '7':	out("DECSC"); break;
				case '8':	out("DECRC"); break;
				case '9':	out("DECFI"); break;
				case '<':	out("DECANSI"); break;
				case '=':	out("DECKPAM"); break;
				case '>':	out("DECKPNM"); break;
				case '`':	out("DMI"); break;
				case 'a':	out("INT"); break;
				case 'b':	out("EMI"); break;
				case 'c':	out("RIS"); break;
				case 'k':	out("NAPLPS"); break;
				case 'l':	out("NAPLPS"); break;
				case 'm':	out("NAPLPS"); break;
				case 'n':	out("LS1"); break;
				case 'o':	out("LS2"); break;
				case '|':	out("LS3R"); break;
				case '}':	out("LS2R"); break;
				case '~':	out("LS1R"); break;
			}
			break;
		case ' ':
			switch (character) {
				default:	std::fprintf(stdout, "ESC "); plainchar(last_intermediate, ' '); plainchar(character, '\n'); break;
				case 'F':	out("S7C1T"); break;
				case 'G':	out("S8C1T"); break;
			}
			break;
		case '#':
			switch (character) {
				default:	std::fprintf(stdout, "ESC "); plainchar(last_intermediate, ' '); plainchar(character, '\n'); break;
				case '8':	out("DECALN"); break;
			}
			break;
		case '!':	iso2022("CZD", character); break;
		case '"':	iso2022("C1D", character); break;
		case '%':	iso2022("DOCS", character); break;
		case '/':	iso2022("DOCS", character); break;
		case '&':	iso2022("IRR", character); break;
		case '(':	iso2022("GZD4", character); break;
		case ')':	iso2022("G1D4", character); break;
		case '*':	iso2022("G2D4", character); break;
		case '+':	iso2022("G3D4", character); break;
		case '-':	iso2022("G1D6", character); break;
		case '.':	iso2022("G2D6", character); break;
		case '$':	iso2022("GZDM", character); break;
	}
}

void
Decoder::ControlSequence(
	uint_fast32_t character,
	char last_intermediate,
	char first_private_parameter
) {
	// Finish the final argument, using the relevant defaults.
	FinishArg(0U); 

	// Enact the action.
	if (NUL == last_intermediate) {
		if (NUL == first_private_parameter) {
			if (sco_function_keys) {
				static const char other[9] = "@[\\]^_`{";
				// The SCO system has no F0 ('L').
				if ('L' <  character && character <= 'Z') {
					fkey("SCO", character - 'L');
					goto skip_dec;
				} else
				if ('a' <= character && character <= 'z') {
					fkey("SCO", character - 'a' + 15U);
					goto skip_dec;
				} else
				if (const char * p = 0x20 < character && character < 0x80 ? std::strchr(other, static_cast<char>(character)) : 0) {
					fkey("SCO", p - other + 41U);
					goto skip_dec;
				} else
					;
			}
			if (rxvt_function_keys) switch (character) {
				case '$':	fnk("rxvt", INPUT_MODIFIER_LEVEL2); goto skip_dec;
				case '^':	fnk("rxvt", INPUT_MODIFIER_CONTROL); goto skip_dec;
				case '@':	fnk("rxvt", INPUT_MODIFIER_LEVEL2|INPUT_MODIFIER_CONTROL); goto skip_dec;
				case 'a':	keychord("rxvt", "UP", INPUT_MODIFIER_LEVEL2); goto skip_dec;
				case 'b':	keychord("rxvt", "DOWN", INPUT_MODIFIER_LEVEL2); goto skip_dec;
				case 'c':	keychord("rxvt", "RIGHT", INPUT_MODIFIER_LEVEL2); goto skip_dec;
				case 'd':	keychord("rxvt", "LEFT", INPUT_MODIFIER_LEVEL2); goto skip_dec;
			}
			switch (character) {
	// ---- ECMA-defined final characters ----
				case '@':	csi_sumargs("ICH"); break;
				case 'A':	input ? csi_fnk("CUU") : csi_sumargs("CUU"); break;
				case 'B':	input ? csi_fnk("CUD") : csi_sumargs("CUD"); break;
				case 'C':	input ? csi_fnk("CUF") : csi_sumargs("CUF"); break;
				case 'D':	input ? csi_fnk("CUB") : csi_sumargs("CUB"); break;
				case 'E':	input ? csi_fnk("CNL") : csi_sumargs("CNL"); break;
				case 'F':	input ? csi_fnk("CPL") : csi_sumargs("CPL"); break;
				case 'G':	input ? csi_fnk("CHA") : csi("CHA"); break;
				case 'H':	input ? csi_fnk("CUP") : csi("CUP"); break;
				case 'I':	csi_sumargs("CHT"); break;
				case 'J':	csi("ED"); break;
				case 'K':	csi("EL"); break;
				case 'L':	csi_sumargs("IL"); break;
				case 'M':	csi_sumargs("DL"); break;
				case 'N':	csi("EF"); break;
				case 'O':	csi("EA"); break;
				case 'P':	csi_sumargs("DCH"); break;
				case 'Q':	csi("SEE"); break;
				case 'R':	csi("CPR"); break;
				case 'S':	csi_sumargs("SU"); break;
				case 'T':	csi_sumargs("SD"); break;
				case 'U':	csi("NP"); break;
				case 'V':	csi("PP"); break;
				case 'W':	csi("CTC"); break;
				case 'X':	csi_sumargs("ECH"); break;
				case 'Y':	csi_sumargs("CVT"); break;
				case 'Z':	input ? csi_fnk("CBT") : csi_sumargs("CBT"); break;
				case '[':	csi("SRS"); break;
				case '\\':	csi("PTX"); break;
				case ']':	csi("SDS"); break;
				case '^':	csi("SIMD"); break;
				case '`':	csi_sumargs("HPA"); break;
				case 'a':	csi_sumargs("HPR"); break;
				case 'b':	csi("REP"); break;
				case 'c':	csi("DA"); break;
				case 'd':	csi_sumargs("VPA"); break;
				case 'e':	csi_sumargs("VPR"); break;
				case 'f':	csi("HVP"); break;
				case 'g':	csi("TBC"); break;
				case 'h':	csi("SM"); break;
				case 'i':	csi("MC"); break;
				case 'j':	csi_sumargs("HPB"); break;
				case 'k':	csi_sumargs("VPB"); break;
				case 'l':	csi("RM"); break;
				case 'm':	csi("SGR"); break;
				case 'n':	csi("DSR"); break;
				case 'o':	csi("DAQ"); break;
	// ---- ECMA private-use final characters begin here. ----
				case 'p':	csi("DECSTR"); break;
				case 'q':	csi("DECLL"); break;
				case 'r':	csi("DECSTBM"); break;
				case 's':	if (argc > 0) csi("DECSLRM"); else csi("SCOSC"); break;
				case 't':	csi("DECSLPP"); break;
				case 'u':	csi("SCORC"); break;
				case 'v':	csi("DECSVST"); break;
				case 'w':	csi("DECSHORP"); break;
				case 'x':	csi("SCOSGR"); break;
				case 'y':	csi("DECTST"); break;
				case 'z':	csi("DECSVERP"); break;
				case '|':	csi("DECTTC"); break;
				case '}':	csi("DECPRO"); break;
				case '~':	fnk("DEC",0U); break;
				default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
			}
skip_dec: 		;
		} else
		if ('?' == first_private_parameter) switch (character) {
			case 'c':	break;
			case 'h':	csi("DECSM"); break;
			case 'l':	csi("DECRM"); break;
			case 'n':	csi("DECDSR"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
		if ('>' == first_private_parameter) switch (character) {
			case 'c':	csi("DECDA2"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
		if ('=' == first_private_parameter) switch (character) {
			case 'C':	break;
			case 'S':	break;
			case 'c':	csi("DECDA3"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
		if ('<' == first_private_parameter) switch (character) {
			case 'M':	csi("true SGRmouse"); break;
			case 'm':	csi("false SGRmouse"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
			csi_unknown(character, last_intermediate, first_private_parameter);
	} else
	if ('$' == last_intermediate) switch (character) {
		case '|':	csi("DECSCPP"); break;
		default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
	} else
	if ('*' == last_intermediate) switch (character) {
		case '|':	csi("DECSNLS"); break;
		default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
	} else
	if (' ' == last_intermediate) switch (character) {
		case 'q':	csi("DECSCUSR"); break;
		case '@':	csi_sumargs("SL"); break;
		case 'A':	csi_sumargs("SR"); break;
		case 'B':	csi("GSM"); break;
		case 'C':	csi("GSS"); break;
		case 'D':	csi("FNT"); break;
		case 'E':	csi("TSS"); break;
		case 'F':	csi("JFY"); break;
		case 'G':	csi("SPI"); break;
		case 'H':	csi("QUAD"); break;
		default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
	} else
	if ('!' == last_intermediate) switch (character) {
		case 'p':	csi("DECSTR"); break;
		default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
	} else
	if ('\'' == last_intermediate) switch (character) {
		case 'w':	csi("DECEFR"); break;
		case '{':	csi("DECSLE"); break;
		case '|':	csi("DECRQLP"); break;
		case 'z':	csi("DECELR"); break;
		default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
	} else
	if ('&' == last_intermediate) switch (character) {
		case 'w':	csi("DECLRP"); break;
	} else
		csi_unknown(character, last_intermediate, first_private_parameter);
}

void
Decoder::ControlString(uint_fast32_t character)
{
	switch (character) {
		default:	std::fprintf(stdout, "unknown control string "); plainchar(character, ' '); break;
		case DCS:	std::fprintf(stdout, "DCS "); break;
		case OSC:	std::fprintf(stdout, "OSC "); break;
		case PM:	std::fprintf(stdout, "PM "); break;
		case APC:	std::fprintf(stdout, "APC "); break;
		case SOS:	std::fprintf(stdout, "SOS "); break;
	}
	for (std::size_t s(0U); s < slen; ++s)
		plainchar(str[s], ' ');
	std::fprintf(stdout, "\n");
}

bool 
Decoder::process (
	const char * name,
	int fd
) {
	char buf[32768];
	for (;;) {
		const int rd(read(fd, buf, sizeof buf));
		if (0 > rd) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
			return false;
		} else if (0 == rd)
			return true;
		for (int i(0); i < rd; ++i)
			utf8_decoder.Process(buf[i]);
		std::fflush(stdout);
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_decode_ecma48 [[gnu::noreturn]] (
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool input(false);
	bool cannot(false);
	bool no_7bit(false);
	try {
		popt::bool_definition input_option('i', "input", "Treat ambiguous I/O sequences as input rather than as output.", input);
		popt::bool_definition cannot_option('\0', "no-cancel", "Disable CAN cancellation.", cannot);
		popt::bool_definition no_7bit_option('\0', "no-7bit", "Disable 7-bit code extensions.", no_7bit);
		popt::definition * top_table[] = {
			&input_option,
			&cannot_option,
			&no_7bit_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "[file(s)...]");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}
	Decoder decoder(prog, envs, input, !cannot, !no_7bit);
	if (args.empty()) {
		if (!decoder.process("<stdin>", STDIN_FILENO))
			throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
	} else {
		for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
			const char * name(*i);
			const FileDescriptorOwner fd(open_read_at(AT_FDCWD, name));
			if (0 > fd.get()) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);	// Bernstein daemontools compatibility
			}
			if (!decoder.process(name, fd.get()))
				throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
		}
	}
	throw EXIT_SUCCESS;
}
