/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <string>
#include "config/hasvis.h"
#if defined(HAS_VIS)
#include <vis.h>
#else
#include "ControlCharacters.h"
#endif
#include "VisDecoder.h"

namespace {

#if !defined(HAS_VIS)

#if !defined(UNVIS_END)
enum /*flags*/ { UNVIS_END = 1 };
#endif
#if !defined(UNVIS_VALID)
enum /*actions*/ { UNVIS_NOCHAR, UNVIS_SYNBAD, UNVIS_ERROR, UNVIS_VALID, UNVIS_VALIDPUSH };
#endif

enum /*state_bits*/ {
	CHARMASK	= 0x00FF,
	COUNTMASK	= 0x0F00,
		INCR	= 0x0100,
	STATEMASK	= 0xF000,
		START	= 0x0000,
		SLASH	= 0x1000,
		OCTAL	= 0x2000,
		M	= 0x3000,
		C	= 0x4000,
		MD	= 0x5000,
		MC	= 0x6000,
};

inline
int
unvis_end(char & out, int & state)
{
	switch (state & STATEMASK) {
		case OCTAL:
			out = static_cast<char>(state & CHARMASK);
			state = START;
			return UNVIS_VALID;
		case START:
			return UNVIS_NOCHAR;
		case SLASH:
		case M:
		case C:
		case MD:
		case MC:
			state = START;
			return UNVIS_SYNBAD;
		default:
			state = START;
			return UNVIS_ERROR;
	}
}

inline
int
unvis_normal(char & out, const char c, int & state)
{
	switch (state & STATEMASK) {
		default:
			state = START;
			return UNVIS_ERROR;
		case START:
			if ('\\' == c) {
				state = SLASH;
				return UNVIS_NOCHAR;
			} else
				out = c;
			return UNVIS_VALID;
		case SLASH:
			if ('8' != c && '9' != c && std::isdigit(c)) {
				state = OCTAL;
				goto octal;
			} else
			if ('M' == c) {
				state = M;
				return UNVIS_NOCHAR;
			} else
			if ('^' == c) {
				state = C;
				return UNVIS_NOCHAR;
			} else
			{
				state = START;
				// We overdecode slash sequences, like we overdecode octal.
				switch (c) {
					case 'a':	out = BEL; return UNVIS_VALID;
					case 'b':	out = BS; return UNVIS_VALID;
					case 'f':	out = FF; return UNVIS_VALID;
					case 'n':	out = LF; return UNVIS_VALID;
					case 'r':	out = CR; return UNVIS_VALID;
					case 's':	out = SPC; return UNVIS_VALID;
					case 't':	out = TAB; return UNVIS_VALID;
					case 'v':	out = VT; return UNVIS_VALID;
					case '\\':	out = c; return UNVIS_VALID;
					// Abort and simply swallow everything so far.
					default:	return UNVIS_SYNBAD;
				}
			}
		octal:
		case OCTAL:
		{
			// Hitting an invalid octal character aborts anything in progress.
			if ('8' == c || '9' == c || !std::isdigit(c)) {
				state = START;
				out = static_cast<char>(state & CHARMASK);
				return UNVIS_VALIDPUSH;
			}
			// We overdecode octal, allowing anything rather than just 040 and 240.
			const unsigned n(((state & CHARMASK) << 3U) | (c - '0'));
			const unsigned count(state & COUNTMASK);
			if ((count / INCR) >= 2U) {
				state = START;
				out = static_cast<char>(n & CHARMASK);
				return UNVIS_VALID;
			}
			state = (state & ~(COUNTMASK|CHARMASK)) | ((count + INCR) & COUNTMASK) | (n & CHARMASK);
			return UNVIS_NOCHAR;
		}
		case M:
			if ('^' == c)
				state = MC;
			else
			if ('-' == c)
				state = MD;
			else
			{
				// Abort and simply swallow everything so far.
				state = START;
				return UNVIS_SYNBAD;
			}
			return UNVIS_NOCHAR;
		case MD:
			state = START;
			if (std::isgraph(c))
				out = static_cast<char>(c + 0x80);
			else
				// Abort and simply swallow everything so far.
				return UNVIS_SYNBAD;
			return UNVIS_VALID;
		case MC:
		case C:
		{
			const unsigned offset(MC == state ? 0x80 : 0x00);
			state = START;
			if ('?' == c)
				out = static_cast<char>(static_cast<unsigned char>(DEL) + offset);
			else
			if (0x40 <= c && c <= 0x5F)
				out = static_cast<char>(c - 0x40 + offset);
			else
			if (0x60 <= c && c <= 0x7E)
				out = static_cast<char>(c - 0x60 + offset);
			else
				// Abort and simply swallow everything so far.
				return UNVIS_SYNBAD;
			return UNVIS_VALID;
		}
	}
}

int
unvis(char *outp, char in, int * statep, int flags)
{
	if (flags & UNVIS_END)
		return unvis_end(*outp, *statep);
	else
		return unvis_normal(*outp, in, *statep);
}

#endif

}

VisDecoder::VisDecoder() :
	c('\0'),
	unvis_state(0)
{
}

void
VisDecoder::Begin()
{
	unvis_state = 0;
}

std::string
VisDecoder::Normal(
	char character
) {
	std::string s;
again:
	switch (unvis(&c, character, &unvis_state, 0)) {
		default:
		case UNVIS_ERROR:
			unvis_state = 0;
			break;
		case UNVIS_NOCHAR:
		case UNVIS_SYNBAD:
			break;
		case UNVIS_VALID:
			s += c;
			break;
		case UNVIS_VALIDPUSH:
			s += c;
			goto again;
	}
	return s;
}

std::string
VisDecoder::End(
) {
	std::string s;
again:
	switch (unvis(&c, '\0', &unvis_state, UNVIS_END)) {
		default:
		case UNVIS_ERROR:
			break;
		case UNVIS_NOCHAR:
		case UNVIS_SYNBAD:
			break;
		case UNVIS_VALID:
			s += c;
			break;
		case UNVIS_VALIDPUSH:
			s += c;
			goto again;
	}
	unvis_state = 0;
	return s;
}
