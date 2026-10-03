/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#define _BSD_SOURCE 1
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
#include "UnicodeClassification.h"
#include "UTF8Decoder.h"
#include "u32string.h"
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
	Decoder(const char *, const ProcessEnvironment &, bool, bool, bool, bool);
	~Decoder();
	bool process (const char * name, int fd);
protected:
	UTF8Decoder utf8_decoder;
	ECMA48Decoder ecma48_decoder;
	const char * const prog;
	const ProcessEnvironment & envs;
	const bool input, no_7bit;

	virtual void ProcessDecodedUTF8(char32_t character, bool decoder_error, bool overlong);
	virtual void PrintableCharacter(bool, unsigned short, char32_t);
	virtual void ControlCharacter(char32_t);
	virtual void EscapeSequence(char32_t, char32_t);
	virtual void ControlSequence(char32_t, char32_t, char32_t);
	virtual void ControlString(char32_t, char32_t);

	void out(const char *);
	void csi(const char *);
	void csi_one_arg(const char *);
	void csi_fnk(const char *);
	void csi_unknown(char32_t, char, char);
	void plainchar(char32_t, char);
	void dec_modifier(unsigned);
	void keychord(const char *, const char *, unsigned);
	void keychord(const char *, unsigned);
	void fkey(const char *, unsigned, unsigned);
	void fkey(const char *, unsigned);
	void xterm_fnk(const char *, unsigned, unsigned, unsigned);
	void decfnk(const char *, unsigned);
	void scofnk(unsigned);
	void fnk();
	void usb_extended();
	void usb_consumer();
	void iso2022(const char *, char32_t);
};

inline unsigned int TranslateFromDECModifiers(unsigned int n) { return n - 1U; }
inline unsigned int TranslateToDECModifiers(uint_fast8_t n) { return n + 1U; }
//inline unsigned int TranslateDECCoordinates(unsigned int n) { return n - 1U; }

inline
bool
IsControl(char32_t character)
{
	return (character < SPC) || (character >= 0x80 && character < 0xA0) || (DEL == character);
}

}

Decoder::Decoder(
	const char * p,
	const ProcessEnvironment & e,
	bool i,
	bool permit_cancel,
	bool permit_7bit_extensions,
	bool permit_control_strings
) :
	TerminalCapabilities(e),
	ECMA48Decoder::ECMA48ControlSequenceSink(),
	utf8_decoder(*this),
	ecma48_decoder(*this, ECMA48Decoder::Options(permit_control_strings, permit_cancel, permit_7bit_extensions, interix_function_keys, rxvt_function_keys, linux_function_keys)),
	prog(p),
	envs(e),
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
	char32_t character,
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
	std::fprintf(stdout, "%s", name);
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		std::fputc(i ? ';' : ' ', stdout);
		for (std::size_t j(0U); j < QuerySubArgCount(i); ++j) {
			if (j) std::fputc(':', stdout);
			if (!QueryArgNull(i,j))
				std::fprintf(stdout, "%u", GetArgZeroIfEmpty(i,j));
		}
	}
	std::fputc('\n', stdout);
}

void
Decoder::csi_one_arg (
	const char * name
) {
	std::fprintf(stdout, "%s", name);
	if (QueryArgCount())
		std::fprintf(stdout, " %u", GetArgZDIfZeroOneIfEmpty(0U));
	std::fputc('\n', stdout);
}

void
Decoder::csi_unknown (
	char32_t character,
	char last_intermediate,
	char first_private_parameter
) {
	std::fprintf(stdout, "unknown CSI ");
	if (first_private_parameter) plainchar(first_private_parameter, ' ');
	for (std::size_t i(0U); i < QueryArgCount(); ++i)
		std::fprintf(stdout, "%s%u", i?";":"", GetArgZeroIfEmpty(i));
	if (QueryArgCount()) std::fprintf(stdout, " ");
	if (last_intermediate) plainchar(last_intermediate, ' ');
	plainchar(character, '\n');
}

void
Decoder::plainchar(
	char32_t character,
	char suffix
) {
	std::fprintf(stdout, "U+%08" PRIx32, character);
	if (!IsControl(character)
	&&  !UnicodeCategorization::IsOtherFormat(character)
	&&  !UnicodeCategorization::IsOtherSurrogate(character)
	&&  !UnicodeCategorization::IsMarkNonSpacing(character)
	&&  !UnicodeCategorization::IsMarkEnclosing(character)
	) {
		std::string s;
		ConvertToUTF8(s, u32string(&character, 1U));
		std::fprintf(stdout, " '%s'", s.c_str());
	}
	std::fputc(suffix, stdout);
}

inline
void
Decoder::dec_modifier(
	unsigned mod
) {
	// our extensions
	if (mod & INPUT_MODIFIER_SUPER) std::fprintf(stdout, "Super+");
	if (mod & INPUT_MODIFIER_GROUP2) std::fprintf(stdout, "Group2+");
	// DEC
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
Decoder::keychord(
	const char * name,
	unsigned mod
) {
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
Decoder::xterm_fnk(
	const char * prefix,
	unsigned f,
	unsigned k,
	unsigned m
) {
	std::fputs("XTerm ", stdout);
	switch (k) {
		default:	std::fprintf(stdout, "%s FNK %u;%u;%u\n", prefix, f, TranslateToDECModifiers(m), k); break;
		case 9U:	keychord(prefix,"KEY_PAD_TAB",m); break;
		case 13U:	keychord(prefix,"KEY_PAD_ENTER",m); break;
		case 27U:	keychord(prefix,"KEY_ESC",m); break;
		case 33U:	keychord(prefix,"KEY_PAD_EXCLAMATION",m); break;
		case 35U:	keychord(prefix,"KEY_PAD_HASH",m); break;
		case 39U:	keychord(prefix,"KEY_PAD_APOSTROPHE",m); break;
		case 40U:	keychord(prefix,"KEY_PAD_OPEN_BRACKET",m); break;
		case 41U:	keychord(prefix,"KEY_PAD_CLOSE_BRACKET",m); break;
		case 42U:	keychord(prefix,"KEY_PAD_ASTERISK",m); break;
		case 43U:	keychord(prefix,"KEY_PAD_PLUS",m); break;
		case 44U:	keychord(prefix,"KEY_PAD_COMMA",m); break;
		case 45U:	keychord(prefix,"KEY_PAD_MINUS",m); break;
		case 46U:	keychord(prefix,"KEY_PAD_DELETE",m); break;
		case 47U:	keychord(prefix,"KEY_PAD_SLASH",m); break;
		case 48U:	keychord(prefix,"KEY_PAD_INSERT",m); break;
		case 49U:	keychord(prefix,"KEY_PAD_END",m); break;
		case 50U:	keychord(prefix,"KEY_PAD_DOWN",m); break;
		case 51U:	keychord(prefix,"KEY_PAD_PAGE_DOWN",m); break;
		case 52U:	keychord(prefix,"KEY_PAD_LEFT",m); break;
		case 53U:	keychord(prefix,"KEY_PAD_CENTRE",m); break;
		case 54U:	keychord(prefix,"KEY_PAD_RIGHT",m); break;
		case 55U:	keychord(prefix,"KEY_PAD_HOME",m); break;
		case 56U:	keychord(prefix,"KEY_PAD_UP",m); break;
		case 57U:	keychord(prefix,"KEY_PAD_PAGE_UP",m); break;
		case 58U:	keychord(prefix,"KEY_PAD_COLON",m); break;
		case 59U:	keychord(prefix,"KEY_PAD_SEMICOLON",m); break;
		case 60U:	keychord(prefix,"KEY_PAD_LESS",m); break;
		case 61U:	keychord(prefix,"KEY_PAD_EQUALS",m); break;
		case 62U:	keychord(prefix,"KEY_PAD_GREATER",m); break;
		case 63U:	keychord(prefix,"KEY_PAD_QUESTION",m); break;
	}
}

void
Decoder::decfnk(
	const char * prefix,
	unsigned default_modifier
) {
	if (1U > QueryArgCount()) {
		std::fprintf(stdout, "%s FNK\n", prefix);
		return;
	}
	// Vanilla DECFNK never has subarguments, so we pre-convert multiple arguments to a single key in our extended form.
	if (HasNoSubArgsFrom(0U))
		CollapseArgsToSubArgs(0U);
	// As an extension we permit multiple keys to be encoded in ISO 8613-3/ITU T.416 key:mod form.
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		const uint_fast8_t m(TranslateFromDECModifiers(GetArgOneIfZeroThisIfEmpty(i,1U,TranslateToDECModifiers(default_modifier))));
		const unsigned k(GetArgZeroIfEmpty(i,0U));
		switch (k) {
			default:	std::fprintf(stdout, "%s FNK %u;%u\n", prefix, k, TranslateToDECModifiers(m)); break;
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
			case 42U:	fkey(prefix,23U,m); break;	// XTerm extension to DEC VT
			case 43U:	fkey(prefix,24U,m); break;	// XTerm extension to DEC VT
			case 27U:	// DEC VT function key 14½ is a Harry Potter XTerm extension to DEC VT.
			{
				if (QuerySubArgCount(i) < 3U)
					std::fprintf(stdout, "%s FNK %u;%u\n", prefix, k, m);
				else
					xterm_fnk(prefix,k,GetArgZeroIfEmpty(i,2U),m);
				break;
			}
		}
	}
}

void
Decoder::scofnk(
	unsigned k
) {
	// The caller has guaranteed that XTerm's extensions for PF1 to PF5 with modifiers has not reached here.
	// Vanilla CSI sequences for SCO function keys never have arguments.
	// They encode the modifiers in the key number.
	uint_fast8_t m(0U);
	if (k > 48U) {
		m |= INPUT_MODIFIER_LEVEL3;
		k -= 48U;
	}
	if (k > 24U) {
		m |= INPUT_MODIFIER_CONTROL;
		k -= 24U;
	}
	if (k > 12U) {
		m |= INPUT_MODIFIER_LEVEL2;
		k -= 12U;
	}
	fkey("SCO",k,m);
}

void
Decoder::fnk(
) {
	if (1U > QueryArgCount()) {
		std::fprintf(stdout, "FNK\n");
		return;
	}
	// Vanilla FNK never has subarguments, so we pre-convert multiple arguments to a single key in our extended form.
	if (HasNoSubArgsFrom(0U))
		CollapseArgsToSubArgs(0U);
	// As an extension we permit multiple keys and modifiers to be encoded in ISO 8613-3/ITU T.416 key:mod form.
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		const uint_fast8_t m(GetArgZeroIfEmpty(i,1U));
		const unsigned k(GetArgZeroIfEmpty(i,0U));
		fkey("ECMA48",k,m);
	}
}

void
Decoder::usb_extended(
) {
	if (1U > QueryArgCount()) {
		std::fprintf(stdout, "%s\n", "ExtendedUSB");
		return;
	}
	// We expect (possibly multiple) keys and modifiers to be encoded in ISO 8613-3/ITU T.416 key:mod form.
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		const uint_fast8_t m(GetArgZeroIfEmpty(i,1U));
		const unsigned k(GetArgZeroIfEmpty(i,0U));
		switch (k) {
			case EXTENDED_KEY_MUTE:		keychord("Mute",m); break;
			case EXTENDED_KEY_VOLUME_UP:	keychord("Volume+",m); break;
			case EXTENDED_KEY_VOLUME_DOWN:	keychord("Volume-",m); break;
			default:
			{
				char name[64];
				std::snprintf(name, sizeof name, "%u", k);
				keychord("ExtendedUSB",name,m);
				break;
			}
		}
	}
}

void
Decoder::usb_consumer(
) {
	if (1U > QueryArgCount()) {
		std::fprintf(stdout, "%s\n", "ConsumerUSB");
		return;
	}
	// We expect (possibly multiple) keys and modifiers to be encoded in ISO 8613-3/ITU T.416 key:mod form.
	for (std::size_t i(0U); i < QueryArgCount(); ++i) {
		const uint_fast8_t m(GetArgZeroIfEmpty(i,1U));
		const unsigned k(GetArgZeroIfEmpty(i,0U));
		switch (k) {
			case CONSUMER_KEY_PLAY:			keychord("Play", m); break;
			case CONSUMER_KEY_PAUSE:		keychord("Pause", m); break;
			case CONSUMER_KEY_RECORD:		keychord("Record", m); break;
			case CONSUMER_KEY_FAST_FORWARD:		keychord("Fast Forward", m); break;
			case CONSUMER_KEY_REWIND:		keychord("Rewind", m); break;
			case CONSUMER_KEY_NEXT_TRACK:		keychord("Next Track", m); break;
			case CONSUMER_KEY_PREV_TRACK:		keychord("Prev Track", m); break;
			case CONSUMER_KEY_STOP_PLAYING:		keychord("Stop Playing", m); break;
			case CONSUMER_KEY_EJECT:		keychord("Eject", m); break;
			case CONSUMER_KEY_RANDOM_PLAY:		keychord("Shuffle", m); break;
			case CONSUMER_KEY_REPEAT:		keychord("Repeat", m); break;
			case CONSUMER_KEY_TRACK_NORMAL:		keychord("Track Normal", m); break;
			case CONSUMER_KEY_FRAME_FORWARD:	keychord("Frame Forward", m); break;
			case CONSUMER_KEY_FRAME_BACK:		keychord("Frame Backward", m); break;
			case CONSUMER_KEY_STOP_EJECT:		keychord("Stop/Eject", m); break;
			case CONSUMER_KEY_PLAY_PAUSE:		keychord("Play/Pause", m); break;
			case CONSUMER_KEY_PLAY_SKIP:		keychord("Play Skip", m); break;
			case CONSUMER_KEY_MUTE_PLAYER:		keychord("Mute Player", m); break;
			case CONSUMER_KEY_BASS_BOOST:		keychord("Bass Boost", m); break;
			case CONSUMER_KEY_LOUDNESS:		keychord("Loudness", m); break;
			case CONSUMER_KEY_VOLUME_UP:		keychord("Player Volume+", m); break;
			case CONSUMER_KEY_VOLUME_DOWN:		keychord("Player Volume-", m); break;
			case CONSUMER_KEY_BALANCE_RIGHT:	keychord("Balance Right", m); break;
			case CONSUMER_KEY_BALANCE_LEFT:		keychord("Balance Left", m); break;
			case CONSUMER_KEY_BASS_UP:		keychord("Bass+", m); break;
			case CONSUMER_KEY_BASS_DOWN:		keychord("Bass-", m); break;
			case CONSUMER_KEY_TREBLE_UP:		keychord("Treble+", m); break;
			case CONSUMER_KEY_TREBLE_DOWN:		keychord("Treble-", m); break;
			case CONSUMER_KEY_WORDPROCESSOR:	keychord("Word Processor", m); break;
			case CONSUMER_KEY_TEXT_EDITOR:		keychord("Text Editor", m); break;
			case CONSUMER_KEY_SPREADSHEET:		keychord("Spreadsheet", m); break;
			case CONSUMER_KEY_GRAPHICS_EDITOR:	keychord("Graphics Editor", m); break;
			case CONSUMER_KEY_PRESENTATION_APP:	keychord("Presentation App", m); break;
			case CONSUMER_KEY_DATABASE:		keychord("Database", m); break;
			case CONSUMER_KEY_MAIL:			keychord("Mail", m); break;
			case CONSUMER_KEY_NEWS:			keychord("News", m); break;
			case CONSUMER_KEY_VOICEMAIL:		keychord("Voicemail", m); break;
			case CONSUMER_KEY_ADDRESS_BOOK:		keychord("Address Book", m); break;
			case CONSUMER_KEY_CALENDAR:		keychord("Calendar", m); break;
			case CONSUMER_KEY_PROJECT_MANAGER:	keychord("Project Manager", m); break;
			case CONSUMER_KEY_TIMECARD:		keychord("Time Card", m); break;
			case CONSUMER_KEY_CHEQUEBOOK:		keychord("Cheque Book", m); break;
			case CONSUMER_KEY_CALCULATOR:		keychord("Calculator", m); break;
			case CONSUMER_KEY_LOCAL_COMPUTER:	keychord("Local Computer", m); break;
			case CONSUMER_KEY_LOCAL_NETWORK:	keychord("Local Network", m); break;
			case CONSUMER_KEY_WWW_BROWSER:		keychord("WWW Browser", m); break;
			case CONSUMER_KEY_CONFERENCE:		keychord("Conference", m); break;
			case CONSUMER_KEY_CHAT:			keychord("Chat", m); break;
			case CONSUMER_KEY_DIALLER:		keychord("Dialler", m); break;
			case CONSUMER_KEY_LOGON:		keychord("Log In", m); break;
			case CONSUMER_KEY_LOGOFF:		keychord("Log Out", m); break;
			case CONSUMER_KEY_LOCK:			keychord("Lock Session", m); break;
			case CONSUMER_KEY_CONTROL_PANEL:	keychord("Control Panel", m); break;
			case CONSUMER_KEY_CLI:			keychord("Command-Line", m); break;
			case CONSUMER_KEY_TASK_MANAGER:		keychord("Task Manager", m); break;
			case CONSUMER_KEY_SELECT_TASK:		keychord("Select Task", m); break;
			case CONSUMER_KEY_NEXT_TASK:		keychord("Next Task", m); break;
			case CONSUMER_KEY_PREVIOUS_TASK:	keychord("Previous Task", m); break;
			case CONSUMER_KEY_HALT_TASK:		keychord("Halt Task", m); break;
			case CONSUMER_KEY_HELP_CENTRE:		keychord("Help Centre", m); break;
			case CONSUMER_KEY_DOCUMENTS:		keychord("Documents", m); break;
			case CONSUMER_KEY_DESKTOP:		keychord("Desktop", m); break;
			case CONSUMER_KEY_SPELL_CHECK:		keychord("Spell Check", m); break;
			case CONSUMER_KEY_CLOCK:		keychord("Clock", m); break;
			case CONSUMER_KEY_FILE_MANAGER:		keychord("File Manager", m); break;
			case CONSUMER_KEY_IMAGE_BROWSER:	keychord("Image Browser", m); break;
			case CONSUMER_KEY_INSTANT_MESSAGING:	keychord("Instant Messaging", m); break;
			case CONSUMER_KEY_WWW_SHOPPING:		keychord("WWW Shopping", m); break;
			case CONSUMER_KEY_AUDIO_PLAYER:		keychord("Audio Player", m); break;
			case CONSUMER_KEY_NEW:			keychord("New", m); break;
			case CONSUMER_KEY_OPEN:			keychord("Open", m); break;
			case CONSUMER_KEY_CLOSE:		keychord("Close", m); break;
			case CONSUMER_KEY_EXIT:			keychord("Exit", m); break;
			case CONSUMER_KEY_MAXIMIZE:		keychord("Maximize", m); break;
			case CONSUMER_KEY_MINIMIZE:		keychord("Minimize", m); break;
			case CONSUMER_KEY_SAVE:			keychord("Save", m); break;
			case CONSUMER_KEY_PRINT:		keychord("Print", m); break;
			case CONSUMER_KEY_PROPERTIES:		keychord("Properties", m); break;
			case CONSUMER_KEY_UNDO:			keychord("Undo", m); break;
			case CONSUMER_KEY_COPY:			keychord("Copy", m); break;
			case CONSUMER_KEY_CUT:			keychord("Cut", m); break;
			case CONSUMER_KEY_PASTE:		keychord("Paste", m); break;
			case CONSUMER_KEY_SELECT_ALL:		keychord("Select All", m); break;
			case CONSUMER_KEY_FIND:			keychord("Find", m); break;
			case CONSUMER_KEY_FIND_AND_REPLACE:	keychord("Find & Replace", m); break;
			case CONSUMER_KEY_SEARCH:		keychord("Search", m); break;
			case CONSUMER_KEY_GOTO:			keychord("Goto", m); break;
			case CONSUMER_KEY_HOME:			keychord("Home", m); break;
			case CONSUMER_KEY_BACK:			keychord("Back", m); break;
			case CONSUMER_KEY_FORWARD:		keychord("Forward", m); break;
			case CONSUMER_KEY_STOP_LOADING:		keychord("Stop Loading", m); break;
			case CONSUMER_KEY_REFRESH:		keychord("Refresh", m); break;
			case CONSUMER_KEY_PREVIOUS_LINK:	keychord("Previous Link", m); break;
			case CONSUMER_KEY_NEXT_LINK:		keychord("Next Link", m); break;
			case CONSUMER_KEY_BOOKMARKS:		keychord("Bookmarks", m); break;
			case CONSUMER_KEY_HISTORY:		keychord("History", m); break;
			case CONSUMER_KEY_PAN_LEFT:		keychord("Pan Left", m); break;
			case CONSUMER_KEY_PAN_RIGHT:		keychord("Pan Right", m); break;
			default:
			{
				char name[64];
				std::snprintf(name, sizeof name, "%u", k);
				keychord("ConsumerUSB",name,m);
				break;
			}
		}
	}
}

void
Decoder::csi_fnk (
	const char * name
) {
	if (1U > QueryArgCount()) {
		std::fprintf(stdout, "%s\n", name);
	} else
	{
		// Vanilla CSI sequences for extended keys never have subarguments, so we pre-convert multiple arguments to a single key in our extended form.
		if (HasNoSubArgsFrom(0U))
			CollapseArgsToSubArgs(0U);
		// As an extension we permit multiple keys to be encoded in ISO 8613-3/ITU T.416 rep:mod form.
		for (std::size_t i(0U); i < QueryArgCount(); ++i) {
			if (1U < QuerySubArgCount(i)) {
				const uint_fast8_t mod(TranslateFromDECModifiers(GetArgOneIfZeroOrEmpty(i,1U)));
				dec_modifier(mod);
			}
			const unsigned num(GetArgOneIfZeroOrEmpty(i,0U));
			std::fprintf(stdout, "%s %u\n", name, num);
		}
	}
}

void
Decoder::iso2022(
	const char * name,
	char32_t character
) {
	std::fprintf(stdout, "%s ", name);
	plainchar(character, '\n');
}

void
Decoder::PrintableCharacter(
	bool error,
	unsigned short shift_level,
	char32_t character
) {
	if (error)
		std::fprintf(stdout, "(error) ");
	if (12U == shift_level) {
		if ('A' <= character && character <= 'Z')
			fkey("Linux", character - 'A' + 1U);
		else {
			std::fprintf(stdout, "SRS");
			plainchar(character, '\n');
		}
	} else
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
		if (input && rxvt_function_keys) switch (character) {
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
	char32_t character
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
	char32_t character,
	char32_t last_intermediate
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
	char32_t character,
	char32_t last_intermediate,
	char32_t first_private_parameter
) {
	if (NUL == last_intermediate) {
		if (NUL == first_private_parameter) {
			// XTerm's Backtab, and extensions for Enter and PF1 to PF5 with modifiers, use CSI; and overlap SCO function keys.
			// SCOFNK sequences never have modifiers, so we detect the lack of subarguments.
			// For best results, simply avoid SCO function keys.
			if (input && (sco_function_keys||teken_function_keys) && (1U > QueryArgCount())) {
				static const char other[9] = "@[\\]^_`{";
				// The SCOFNK system has no F0 ('L').
				if ('L' == character)
					;
				else
				// teken does not use SCOFNK for F1 ('M') to F12 ('X'); only genuine SCO Unix Multiscreen does.
				if (sco_function_keys
				&& ('L' <  character && character <= 'X')
				) {
					scofnk(character - 'L');
					goto skip_dec;
				} else
				// teken realigns with SCO at Level2+F1 ('Y')
				if ('Y' <= character && character <= 'Z') {
					scofnk(character - 'L');
					goto skip_dec;
				} else
				if ('a' <= character && character <= 'z') {
					scofnk(character - 'a' + 15U);
					goto skip_dec;
				} else
				if (const char * p = 0x20 < character && character < 0x80 ? std::strchr(other, static_cast<char>(character)) : nullptr) {
					scofnk(p - other + 41U);
					goto skip_dec;
				} else
				{
				}
			}
			if (input && rxvt_function_keys) switch (character) {
				case '~':	decfnk("rxvt", 0U); goto skip_dec;
				case '$':	decfnk("rxvt", INPUT_MODIFIER_LEVEL2); goto skip_dec;
				case '^':	decfnk("rxvt", INPUT_MODIFIER_CONTROL); goto skip_dec;
				case '@':	decfnk("rxvt", INPUT_MODIFIER_LEVEL2|INPUT_MODIFIER_CONTROL); goto skip_dec;
				case 'a':	keychord("rxvt", "UP", INPUT_MODIFIER_LEVEL2); goto skip_dec;
				case 'b':	keychord("rxvt", "DOWN", INPUT_MODIFIER_LEVEL2); goto skip_dec;
				case 'c':	keychord("rxvt", "RIGHT", INPUT_MODIFIER_LEVEL2); goto skip_dec;
				case 'd':	keychord("rxvt", "LEFT", INPUT_MODIFIER_LEVEL2); goto skip_dec;
			}
			switch (character) {
	// ---- ECMA-defined final characters ----
				case '@':	csi_one_arg("ICH"); break;
				case 'A':	input ? csi_fnk("CUU") : csi_one_arg("CUU"); break;
				case 'B':	input ? csi_fnk("CUD") : csi_one_arg("CUD"); break;
				case 'C':	input ? csi_fnk("CUF") : csi_one_arg("CUF"); break;
				case 'D':	input ? csi_fnk("CUB") : csi_one_arg("CUB"); break;
				case 'E':	input ? csi_fnk("CNL") : csi_one_arg("CNL"); break;
				case 'F':	input ? csi_fnk("CPL") : csi_one_arg("CPL"); break;
				case 'G':	input ? csi_fnk("CHA") : csi("CHA"); break;
				case 'H':	input ? csi_fnk("CUP") : csi("CUP"); break;
				case 'I':	input ? csi_fnk("TAB") : csi_one_arg("CHT"); break;
				case 'J':	csi("ED"); break;
				case 'K':	csi("EL"); break;
				case 'L':	input ? csi_fnk("IL") : csi_one_arg("IL"); break;
				case 'M':	input ? csi_fnk("KEY_PAD_ENTER") : csi_one_arg("DL"); break;
				case 'N':	csi("EF"); break;
				case 'O':	input ? csi("XTERMFO") : csi("EA"); break;
				case 'P':	input ? csi_fnk("KEY_PAD_F1") : csi_one_arg("DCH"); break;
				case 'Q':	input ? csi_fnk("KEY_PAD_F2") : csi("SEE"); break;
				case 'R':	input ? csi_fnk("KEY_PAD_F3") : csi("CPR"); break;
				case 'S':	input ? csi_fnk("KEY_PAD_F4") : csi_one_arg("SU"); break;
				case 'T':	input ? csi_fnk("KEY_PAD_F5") : csi_one_arg("SD"); break;
				case 'U':	csi("NP"); break;
				case 'V':	csi("PP"); break;
				case 'W':	csi("CTC"); break;
				case 'X':	csi_one_arg("ECH"); break;
				case 'Y':	csi_one_arg("CVT"); break;
				case 'Z':	input ? csi_fnk("CBT") : csi_one_arg("CBT"); break;
				case '[':	csi("SRS"); break;
				case '\\':	csi("PTX"); break;
				case ']':	csi("SDS"); break;
				case '^':	csi("SIMD"); break;
				case '`':	csi_one_arg("HPA"); break;
				case 'a':	csi_one_arg("HPR"); break;
				case 'b':	csi("REP"); break;
				case 'c':	csi("DA"); break;
				case 'd':	csi_one_arg("VPA"); break;
				case 'e':	csi_one_arg("VPR"); break;
				case 'f':	csi("HVP"); break;
				case 'g':	csi("TBC"); break;
				case 'h':	csi("SM"); break;
				case 'i':	csi("MC"); break;
				case 'j':	csi_one_arg("HPB"); break;
				case 'k':	csi_one_arg("VPB"); break;
				case 'l':	csi("RM"); break;
				case 'm':	csi("SGR"); break;
				case 'n':	csi("DSR"); break;
				case 'o':	csi("DAQ"); break;
	// ---- ECMA private-use final characters begin here. ----
				case 'p':	csi("DECSTR"); break;
				case 'q':	csi("DECLL"); break;
				case 'r':	csi("DECSTBM"); break;
				case 's':	if (QueryArgCount() > 0) csi("DECSLRM"); else csi("SCOSC"); break;
				case 't':	csi("DECSLPP"); break;
				case 'u':	csi("SCORC"); break;
				case 'v':	csi("DECSVST"); break;
				case 'w':	csi("DECSHORP"); break;
				case 'x':	csi("SCOSGR"); break;
				case 'y':	csi("DECTST"); break;
				case 'z':	csi("DECSVERP"); break;
				case '|':	csi("DECTTC"); break;
				case '}':	csi("DECPRO"); break;
				case '~':	decfnk("DEC",0U); break;
				default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
			}
skip_dec: 		;
		} else
		if ('?' == first_private_parameter) switch (character) {
			case 'W':	csi("DECCTC"); break;
			case 'c':	csi("LINUXSCUSR"); break;
			case 'h':	csi("DECSM"); break;
			case 'l':	csi("DECRM"); break;
			case 'm':	csi("XTQMODKEYS"); break;
			case 'n':	csi("DECDSR"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
		if ('>' == first_private_parameter) switch (character) {
			case 'c':	csi("DECDA2"); break;
			case 'm':	csi("XTMODKEYS"); break;
			case 'n':	csi("XTDMODKEYS"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
		if ('=' == first_private_parameter) switch (character) {
			case 'A':	csi("SCOABG"); break;
			case 'B':	csi("SCOBLPD"); break;
			case 'C':	csi("SCOCUSR"); break;
			case 'D':	csi("SCOVGAI"); break;
			case 'E':	csi("SCOVGAB"); break;
			case 'F':	csi("SCOANFG"); break;
			case 'G':	csi("SCOANBG"); break;
			case 'H':	csi("SCOARFG"); break;
			case 'I':	csi("SCOARBG"); break;
			case 'J':	csi("SCOAGFG"); break;
			case 'K':	csi("SCOAGBG"); break;
			case 'L':	csi("SCOECM"); break;
			case 'M':	csi("SCORQC"); break;
			case 'S':	csi("C25LSCURS"); break;
			case 'T':	csi("C25MODE"); break;
			case 'c':	csi("DECDA3"); break;
			case 'g':	csi("SCOAG"); break;
			case 'h':	csi("SCOSM"); break;
			case 'l':	csi("SCORM"); break;
			case 'x':	csi("C25AGR"); break;
			case 'z':	csi("C25VTSW"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
		if ('<' == first_private_parameter) switch (character) {
			case 'M':	csi("XTerm mouse press"); break;
			case 'm':	csi("XTerm mouse release"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
			csi_unknown(character, last_intermediate, first_private_parameter);
	} else
	if ('$' == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
			case '|':	csi("DECSCPP"); break;
			case 'p':	csi("DECRQM (Standard)"); break;
			case 'r':	csi("DECCARA"); break;
			case 'w':	csi("DECRQPSR"); break;
			case 'y':	csi("DECRPM (Standard)"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
		if ('?' == first_private_parameter) switch (character) {
			case 'p':	csi("DECRQM (DEC)"); break;
			case 'y':	csi("DECRPM (DEC)"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
			csi_unknown(character, last_intermediate, first_private_parameter);
	} else
	if ('*' == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
			case '|':	csi("DECSNLS"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
			csi_unknown(character, last_intermediate, first_private_parameter);
	} else
	if (' ' == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
			case '@':	csi_one_arg("SL"); break;
			case 'A':	csi_one_arg("SR"); break;
			case 'B':	csi("GSM"); break;
			case 'C':	csi("GSS"); break;
			case 'D':	csi("FNT"); break;
			case 'E':	csi("TSS"); break;
			case 'F':	csi("JFY"); break;
			case 'G':	csi("SPI"); break;
			case 'H':	csi("QUAD"); break;
			case 'W':	fnk(); break;
			case 'q':	csi("DECSCUSR"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
		if ('?' == first_private_parameter) switch (character) {
			case 'W':	usb_extended(); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
		if ('=' == first_private_parameter) switch (character) {
			case 'W':	usb_consumer(); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
			csi_unknown(character, last_intermediate, first_private_parameter);
	} else
	if ('!' == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
			case 'p':	csi("DECSTR"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
			csi_unknown(character, last_intermediate, first_private_parameter);
	} else
	if ('\'' == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
			case 'w':	csi("DECEFR"); break;
			case '{':	csi("DECSLE"); break;
			case '|':	csi("DECRQLP"); break;
			case 'z':	csi("DECELR"); break;
			default:	csi_unknown(character, last_intermediate, first_private_parameter); break;
		} else
			csi_unknown(character, last_intermediate, first_private_parameter);
	} else
	if ('&' == last_intermediate) {
		if (NUL == first_private_parameter) switch (character) {
			case 'w':	csi("DECLRP"); break;
		} else
			csi_unknown(character, last_intermediate, first_private_parameter);
	} else
		csi_unknown(character, last_intermediate, first_private_parameter);
}

void
Decoder::ControlString(char32_t start_char, char32_t term_char)
{
	switch (start_char) {
		default:	std::fprintf(stdout, "unknown control string "); plainchar(start_char, ' '); break;
		case DCS:	std::fprintf(stdout, "DCS "); break;
		case OSC:	std::fprintf(stdout, "OSC "); break;
		case PM:	std::fprintf(stdout, "PM "); break;
		case APC:	std::fprintf(stdout, "APC "); break;
		case SOS:	std::fprintf(stdout, "SOS "); break;
	}
	for (std::size_t s(0U); s < QueryControlStringLength(); ++s)
		plainchar(QueryControlStringCharacter(s), ' ');
	switch (term_char) {
		default:	std::fprintf(stdout, "unknown terminator "); plainchar(term_char, ' '); break;
		case BEL:	std::fprintf(stdout, "BEL "); break;
		case ST:	std::fprintf(stdout, "ST "); break;
	}
	std::fputc('\n', stdout);
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
			message_fatal_errno(prog, envs, name);
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
	bool no_can(false);
	bool no_7bit(false);
	bool permit_control_strings(false);
	try {
		popt::bool_definition input_option('i', "input", "Treat ambiguous I/O sequences as input rather than as output.", input);
		popt::bool_definition no_can_option('\0', "no-cancel", "Disable CAN cancellation.", no_can);
		popt::bool_definition no_7bit_option('\0', "no-7bit", "Disable 7-bit code extensions.", no_7bit);
		popt::bool_definition permit_control_strings_option('\0', "control-strings", "Permit control strings.", permit_control_strings);
		popt::definition * top_table[] = {
			&input_option,
			&no_can_option,
			&no_7bit_option,
			&permit_control_strings_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "[file(s)...]");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}
	Decoder decoder(prog, envs, input, !no_can, !no_7bit, permit_control_strings);
	if (args.empty()) {
		if (!decoder.process("<stdin>", STDIN_FILENO))
			throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
	} else {
		for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
			const char * name(*i);
			const FileDescriptorOwner fd(open_read_at(AT_FDCWD, name));
			if (0 > fd.get()) {
				message_fatal_errno(prog, envs, name);
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);	// Bernstein daemontools compatibility
			}
			if (!decoder.process(name, fd.get()))
				throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
		}
	}
	throw EXIT_SUCCESS;
}
