/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <iostream>
#include <inttypes.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/event.h>
#if defined(__LINUX__) || defined(__linux__)
#include <sys/poll.h>
#endif
#include <sys/ioctl.h>
#include <unistd.h>
#include "popt.h"
#include "fdutils.h"
#include "utils.h"
#include "SoftTerm.h"
#include "InputMessage.h"

enum { PTY_MASTER_FILENO = 4 };

/* UTF-8 decoding class *****************************************************
// **************************************************************************
*/

class UTF8Decoder 
{
public:
	class UCS32CharacterSink
	{
	public:
		virtual void Write(uint32_t character, bool decoder_error, bool overlong) = 0;
	};
	UTF8Decoder(UCS32CharacterSink &);
	void SendUTF8(uint_fast8_t);
protected:
	UCS32CharacterSink & sink;
	unsigned short expected_continuation_bytes;
	uint_fast32_t assemblage, minimum;
	void SendGood();
	void SendBad();
};

UTF8Decoder::UTF8Decoder(UCS32CharacterSink & s) :
	sink(s),
	expected_continuation_bytes(0U),
	assemblage(0U),
	minimum(0U)
{
}

inline
void 
UTF8Decoder::SendGood()
{
	sink.Write(assemblage, false, assemblage < minimum);
	minimum = 0U;
	expected_continuation_bytes = 0U;
}

inline
void 
UTF8Decoder::SendBad()
{
	assemblage = assemblage << (6U * expected_continuation_bytes);
	sink.Write(assemblage, true, assemblage < minimum);
	minimum = 0U;
	expected_continuation_bytes = 0U;
}

void 
UTF8Decoder::SendUTF8(uint_fast8_t c)
{
	if ((0x80 & c) == 0x00) {
		// 0x2x_0nnn_nnnn
		if (0 < expected_continuation_bytes)
			SendBad();
		assemblage = c;
		minimum = 0U;
		SendGood();
	} else
	if ((0xC0 & c) == 0x80) {
		// ... 0x2x_10nn_nnnn ...
		if (0 == expected_continuation_bytes) {
			assemblage = c;
			minimum = 0U;
			SendBad();
		} else {
			--expected_continuation_bytes;
			assemblage = (assemblage << 6U) | (c & 0x3F);
			if (0 == expected_continuation_bytes) 
				SendGood();
		}
	} else
	if ((0xE0 & c) == 0xC0) {
		// 0x2x_110n_nnnn 0x2x_10nn_nnnn
		if (0 < expected_continuation_bytes)
			SendBad();
		expected_continuation_bytes = 1U;
		assemblage = c & 0x1F;
		minimum = 0x00000080;	// Code points less than 0x2x_0000_0000_0000_0000_0000_0000_1000_0000 have a shorter encoding.
	} else
	if ((0xF0 & c) == 0xE0) {
		// 0x2x_1110_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn
		if (0 < expected_continuation_bytes)
			SendBad();
		expected_continuation_bytes = 2U;
		assemblage = c & 0x0F;
		minimum = 0x00000800;	// Code points less than 0x2x_0000_0000_0000_0000_0000_1000_0000_0000 have a shorter encoding.
	} else
	if ((0xF8 & c) == 0xF0) {
		// 0x2x_1111_0nnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn
		if (0 < expected_continuation_bytes)
			SendBad();
		expected_continuation_bytes = 3U;
		assemblage = c & 0x07;
		minimum = 0x00010000;	// Code points less than 0x2x_0000_0000_0000_0001_0000_0000_0000_0000 have a shorter encoding.
	} else
	if ((0xFC & c) == 0xF8) {
		// 0x2x_1111_10nn 0x2x_10nn_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn
		if (0 < expected_continuation_bytes)
			SendBad();
		expected_continuation_bytes = 4U;
		assemblage = c & 0x03;
		minimum = 0x00200000;	// Code points less than 0x2x_0000_0000_0010_0000_0000_0000_0000_0000 have a shorter encoding.
	} else
	if ((0xFE & c) == 0xFC) {
		// 0x2x_1111_110n 0x2x_10nn_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn 0x2x_10nn_nnnn
		if (0 < expected_continuation_bytes)
			SendBad();
		expected_continuation_bytes = 5U;
		assemblage = c & 0x01;
		minimum = 0x04000000;	// Code points less than 0x2x_0000_0100_0000_0000_0000_0000_0000_0000 have a shorter encoding.
	} else {
		// 0x2x_1111_111n
		if (0 < expected_continuation_bytes)
			SendBad();
		assemblage = c;
		minimum = 0U;
		SendBad();
	}
}

/* Old-style vcsa screen buffer *********************************************
// **************************************************************************
*/

class VCSA : 
	public SoftTerm::ScreenBuffer 
{
public:
	VCSA(int d) : fd(d) {}
	virtual void WriteNCells(coordinate p, coordinate n, const CharacterCell & c);
	virtual void ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void SetCursorPos(coordinate x, coordinate y);
	virtual void SetCursorType(CursorSprite::glyph_type, CursorSprite::attribute_type);
	virtual void SetSize(coordinate w, coordinate h);
protected:
	const int fd;
	void MakeCA(char ca[2], const CharacterCell & c);
	enum { CELL_LENGTH = 2U, HEADER_LENGTH = 4U };
	off_t MakeOffset(coordinate s) { return HEADER_LENGTH + CELL_LENGTH * s; }
};

static
unsigned char
VGAColour (
	const CharacterCell::colour_type & c
) {
	enum {
		CGA_BLACK,
		CGA_BLUE,
		CGA_GREEN,
		CGA_CYAN,
		CGA_RED,
		CGA_MAGENTA,
		CGA_YELLOW,
		CGA_WHITE
	};

	if (c.red < c.green) {
		// Something with no red.
		return	c.green < c.blue ? CGA_BLUE :
			c.blue < c.green ? CGA_GREEN :
			CGA_CYAN;
	} else
	if (c.green < c.red) {
		// Something with no green.
		return	c.red < c.blue ? CGA_BLUE :
			c.blue < c.red ? CGA_RED :
			CGA_MAGENTA;
	} else
	{
		// Something with equal red and green.
		return	c.red < c.blue ? CGA_BLUE :
			c.blue < c.red ? CGA_YELLOW :
			c.red ? CGA_WHITE :
			CGA_BLACK;
	}
}

void 
VCSA::MakeCA(char ca[CELL_LENGTH], const CharacterCell & c)
{
	ca[0] = c.character > 0xFE ? 0xFF : c.character;
	ca[1] = (c.attributes & c.BLINK ? 0x80 : 0x00) |
		(c.attributes & c.BOLD ? 0x08 : 0x00) |
		(VGAColour(c.foreground) << (c.attributes & c.INVERSE ? 4 : 0)) |
		(VGAColour(c.background) << (c.attributes & c.INVERSE ? 0 : 4)) |
		0;
}

void 
VCSA::WriteNCells(coordinate s, coordinate n, const CharacterCell & c)
{
	char ca[CELL_LENGTH];
	MakeCA(ca, c);
	for (coordinate i(0U); i < n; ++i)
		pwrite(fd, ca, sizeof ca, MakeOffset(s + i));
}

void 
VCSA::ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c)
{
	char ca[2];
	while (s + n < e) {
		const off_t target(MakeOffset(s));
		const off_t source(MakeOffset(s + n));
		pread(fd, ca, sizeof ca, source);
		pwrite(fd, ca, sizeof ca, target);
		++s;
	}
	MakeCA(ca, c);
	while (s < e) {
		pwrite(fd, ca, sizeof ca, MakeOffset(s));
		++s;
	}
}

void 
VCSA::ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c)
{
	char ca[2];
	while (e - n > s) {
		--e;
		const off_t target(MakeOffset(e));
		const off_t source(MakeOffset(e - n));
		pread(fd, ca, sizeof ca, source);
		pwrite(fd, ca, sizeof ca, target);
	}
	MakeCA(ca, c);
	while (e > s) {
		--e;
		pwrite(fd, ca, sizeof ca, MakeOffset(e));
	}
}

void 
VCSA::SetCursorPos(coordinate x, coordinate y)
{
	const unsigned char b[2] = { static_cast<unsigned char>(x), static_cast<unsigned char>(y) };
	pwrite(fd, b, sizeof b, 2);
}

void 
VCSA::SetCursorType(CursorSprite::glyph_type, CursorSprite::attribute_type)
{
}

void 
VCSA::SetSize(coordinate w, coordinate h)
{
	const unsigned char b[2] = { static_cast<unsigned char>(h), static_cast<unsigned char>(w) };
	pwrite(fd, b, sizeof b, 0);
	ftruncate(fd, MakeOffset(w * h));
}

/* New-style Unicode screen buffer ******************************************
// **************************************************************************
*/

class UnicodeBuffer : 
	public SoftTerm::ScreenBuffer 
{
public:
	UnicodeBuffer(int d);
	virtual void WriteNCells(coordinate p, coordinate n, const CharacterCell & c);
	virtual void ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void SetCursorPos(coordinate x, coordinate y);
	virtual void SetCursorType(CursorSprite::glyph_type, CursorSprite::attribute_type);
	virtual void SetSize(coordinate w, coordinate h);
protected:
	const int fd;
	enum { CELL_LENGTH = 16U, HEADER_LENGTH = 16U };
	void MakeCA(char c[CELL_LENGTH], const CharacterCell & cell);
	off_t MakeOffset(coordinate s) { return HEADER_LENGTH + CELL_LENGTH * static_cast<off_t>(s); }
};

UnicodeBuffer::UnicodeBuffer(int d) : 
	fd(d) 
{
	const uint32_t bom(0xFEFF);
	pwrite(fd, &bom, sizeof bom, 0U);
}

void 
UnicodeBuffer::MakeCA(char c[CELL_LENGTH], const CharacterCell & cell)
{
	c[0] = cell.attributes;
	c[1] = cell.foreground.red;
	c[2] = cell.foreground.green;
	c[3] = cell.foreground.blue;
	c[4] = 0;
	c[5] = cell.background.red;
	c[6] = cell.background.green;
	c[7] = cell.background.blue;
	std::memcpy(&c[8], &cell.character, 4);
	c[12] = 0;
	c[13] = 0;
	c[14] = 0;
	c[15] = 0;
}

void 
UnicodeBuffer::WriteNCells(coordinate s, coordinate n, const CharacterCell & c)
{
	char ca[CELL_LENGTH];
	MakeCA(ca, c);
	for (coordinate i(0U); i < n; ++i)
		pwrite(fd, ca, sizeof ca, MakeOffset(s + i));
}

void 
UnicodeBuffer::ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c)
{
	char ca[CELL_LENGTH];
	while (s + n < e) {
		const off_t target(MakeOffset(s));
		const off_t source(MakeOffset(s + n));
		pread(fd, ca, sizeof ca, source);
		pwrite(fd, ca, sizeof ca, target);
		++s;
	}
	MakeCA(ca, c);
	while (s < e) {
		pwrite(fd, ca, sizeof ca, MakeOffset(s));
		++s;
	}
}

void 
UnicodeBuffer::ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c)
{
	char ca[CELL_LENGTH];
	while (e - n > s) {
		--e;
		const off_t target(MakeOffset(e));
		const off_t source(MakeOffset(e - n));
		pread(fd, ca, sizeof ca, source);
		pwrite(fd, ca, sizeof ca, target);
	}
	MakeCA(ca, c);
	while (e > s) {
		--e;
		pwrite(fd, ca, sizeof ca, MakeOffset(e));
	}
}

void 
UnicodeBuffer::SetCursorType(CursorSprite::glyph_type g, CursorSprite::attribute_type a)
{
	const char b[2] = { static_cast<char>(g), static_cast<char>(a) };
	pwrite(fd, b, sizeof b, 12U);
}

void 
UnicodeBuffer::SetCursorPos(coordinate x, coordinate y)
{
	const uint16_t b[2] = { static_cast<uint16_t>(x), static_cast<uint16_t>(y) };
	pwrite(fd, b, sizeof b, 8U);
}

void 
UnicodeBuffer::SetSize(coordinate w, coordinate h)
{
	const uint16_t b[2] = { static_cast<uint16_t>(w), static_cast<uint16_t>(h) };
	pwrite(fd, b, sizeof b, 4U);
	ftruncate(fd, MakeOffset(w * h));
}

/* input FIFO ***************************************************************
// **************************************************************************
*/

class InputFIFO :
	public SoftTerm::KeyboardBuffer
{
public:
	InputFIFO(int, int);
	void ReadInput();
	void WriteOutput();
	bool OutputAvailable() { return output_pending > 0U; }
	bool HasInputSpace() { return output_pending < sizeof output_buffer; }
protected:
	const int input_fd, master_fd;
	virtual void WriteLatin1Characters(std::size_t, const char *);
	virtual void ReportSize(coordinate w, coordinate h);
	void WriteRawCharacters(std::size_t, const char *);
	void WriteRawCharacters(const char * s) { WriteRawCharacters(std::strlen(s), s); }
	void WriteUnicodeCharacter(uint32_t c);
	void WriteCSISequence(unsigned m, char c);
	void WriteSS3Sequence(unsigned m, char c);
	void WriteDECVTFunctionKey(unsigned n, unsigned m);
	void WriteLinuxFunctionKey(unsigned m, char c);
	void WriteExtendedKeyDECVT(uint16_t k, uint8_t m);
	void WriteExtendedKeySCOConsole(uint16_t k, uint8_t m);
	void WriteExtendedKeyLinuxConsole(uint16_t k, uint8_t m);
	void WriteExtendedKeyNetBSDConsole(uint16_t k, uint8_t m);
	void WriteExtendedKeyXTermPCMode(uint16_t k, uint8_t m);
	void WriteExtendedKey(uint16_t k, uint8_t m);
	void WriteFunctionKey(uint16_t k, uint8_t m);
	char input_buffer[4];
	std::size_t input_read;
	char output_buffer[4096];
	std::size_t output_pending;
};

InputFIFO::InputFIFO(int i, int m) : 
	input_fd(i),
	master_fd(m),
	input_read(0U),
	output_pending(0U)
{
}

void 
InputFIFO::WriteRawCharacters(std::size_t l, const char * p)
{
	if (l > sizeof output_buffer - output_pending)
		l = sizeof output_buffer - output_pending;
	std::memmove(output_buffer + output_pending, p, l);
	output_pending += l;
}

void 
InputFIFO::ReportSize(coordinate w, coordinate h)
{
	winsize size = { 0, 0, 0, 0 };
	size.ws_col = w;
	size.ws_row = h;
	tcsetwinsz_nointr(master_fd, size);
}

static inline
bool
IsAll7Bit (std::size_t l, const char * p)
{
	while (l) {
		if (static_cast<unsigned char>(*p++) > 0x7F)
			return false;
		--l;
	}
	return true;
}

void 
InputFIFO::WriteLatin1Characters(std::size_t l, const char * p)
{
	if (IsAll7Bit(l, p)) 
		return WriteRawCharacters(l, p);
	else while (l) {
		WriteUnicodeCharacter(static_cast<unsigned char>(*p++));
		--l;
	}
}

void 
InputFIFO::WriteUnicodeCharacter(uint32_t c)
{
	if (c < 0x00000080) {
		const char s[1] = { 
			static_cast<char>(c) 
		};
		WriteRawCharacters(sizeof s, s);
	} else
	if (c < 0x00000800) {
		const char s[2] = { 
			0xC0 | (0x1F & (c >> 6U)),
			0x80 | (0x3F & (c >> 0U)),
		};
		WriteRawCharacters(sizeof s, s);
	} else
	if (c < 0x00001000) {
		const char s[3] = { 
			0xE0 | (0x0F & (c >> 12U)),
			0x80 | (0x3F & (c >> 6U)),
			0x80 | (0x3F & (c >> 0U)),
		};
		WriteRawCharacters(sizeof s, s);
	} else
	if (c < 0x00020000) {
		const char s[4] = { 
			0xF0 | (0x07 & (c >> 18U)),
			0x80 | (0x3F & (c >> 12U)),
			0x80 | (0x3F & (c >> 6U)),
			0x80 | (0x3F & (c >> 0U)),
		};
		WriteRawCharacters(sizeof s, s);
	} else
	if (c < 0x00400000) {
		const char s[5] = { 
			0xF8 | (0x03 & (c >> 24U)),
			0x80 | (0x3F & (c >> 18U)),
			0x80 | (0x3F & (c >> 12U)),
			0x80 | (0x3F & (c >> 6U)),
			0x80 | (0x3F & (c >> 0U)),
		};
		WriteRawCharacters(sizeof s, s);
	} else
	{
		const char s[6] = { 
			0xFC | (0x01 & (c >> 30U)),
			0x80 | (0x3F & (c >> 24U)),
			0x80 | (0x3F & (c >> 18U)),
			0x80 | (0x3F & (c >> 12U)),
			0x80 | (0x3F & (c >> 6U)),
			0x80 | (0x3F & (c >> 0U)),
		};
		WriteRawCharacters(sizeof s, s);
	}
}

void 
InputFIFO::WriteCSISequence(unsigned m, char c)
{
	char b[16];
	if (0 != m)
		snprintf(b, sizeof b, "\x1b[1;%u%c", m + 1U, c);
	else
		snprintf(b, sizeof b, "\x1b[%c", c);
	WriteRawCharacters(b);
}

void 
InputFIFO::WriteSS3Sequence(unsigned m, char c)
{
	char b[16];
	if (0 != m)
		snprintf(b, sizeof b, "\x1bO1;%u%c", m + 1U, c);
	else
		snprintf(b, sizeof b, "\x1bO%c", c);
	WriteRawCharacters(b);
}

void 
InputFIFO::WriteDECVTFunctionKey(unsigned n, unsigned m)
{
	char b[16];
	if (0 != m)
		snprintf(b, sizeof b, "\x1b[%u;%u~", n, m + 1U);
	else
		snprintf(b, sizeof b, "\x1b[%u~", n);
	WriteRawCharacters(b);
}

void 
InputFIFO::WriteLinuxFunctionKey(unsigned m, char c)
{
	char b[16];
	if (0 != m)
		snprintf(b, sizeof b, "\x1b[[1;%u%c", m + 1U, c);
	else
		snprintf(b, sizeof b, "\x1b[[%c", c);
	WriteRawCharacters(b);
}

// These are the sequences defined by the DEC VT520 programmers' reference.
// Most termcaps/terminfos describe this as "vt220".
void 
InputFIFO::WriteExtendedKeyDECVT(uint16_t k, uint8_t m)
{
	switch (k) {
	// The auxiliary keypad is in permanent application mode.
		case EXTENDED_KEY_PAD_ASTERISK:		WriteSS3Sequence(m,'j'); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteSS3Sequence(m,'k'); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteSS3Sequence(m,'l'); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteSS3Sequence(m,'m'); break;
		case EXTENDED_KEY_PAD_DELETE:		WriteSS3Sequence(m,'n'); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteSS3Sequence(m,'o'); break;
		case EXTENDED_KEY_PAD_INSERT:		WriteSS3Sequence(m,'p'); break;
		case EXTENDED_KEY_PAD_END:		WriteSS3Sequence(m,'q'); break;
		case EXTENDED_KEY_PAD_DOWN:		WriteSS3Sequence(m,'r'); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	WriteSS3Sequence(m,'s'); break;
		case EXTENDED_KEY_PAD_LEFT:		WriteSS3Sequence(m,'t'); break;
		case EXTENDED_KEY_PAD_CENTER:		WriteSS3Sequence(m,'u'); break;
		case EXTENDED_KEY_PAD_RIGHT:		WriteSS3Sequence(m,'v'); break;
		case EXTENDED_KEY_PAD_HOME:		WriteSS3Sequence(m,'w'); break;
		case EXTENDED_KEY_PAD_UP:		WriteSS3Sequence(m,'x'); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		WriteSS3Sequence(m,'y'); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteSS3Sequence(m,'M'); break;
		case EXTENDED_KEY_PAD_F1:		WriteSS3Sequence(m,'P'); break;
		case EXTENDED_KEY_PAD_F2:		WriteSS3Sequence(m,'Q'); break;
		case EXTENDED_KEY_PAD_F3:		WriteSS3Sequence(m,'R'); break;
		case EXTENDED_KEY_PAD_F4:		WriteSS3Sequence(m,'S'); break;
		case EXTENDED_KEY_PAD_F5:		WriteSS3Sequence(m,'T'); break;
		case EXTENDED_KEY_PAD_EQUALS:		WriteSS3Sequence(m,'X'); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	WriteSS3Sequence(m,'X'); break;
	// The editing keys are in permanent ANSI mode.
		case EXTENDED_KEY_UP_ARROW:		WriteCSISequence(m,'A'); break;
		case EXTENDED_KEY_DOWN_ARROW:		WriteCSISequence(m,'B'); break;
		case EXTENDED_KEY_RIGHT_ARROW:		WriteCSISequence(m,'C'); break;
		case EXTENDED_KEY_LEFT_ARROW:		WriteCSISequence(m,'D'); break;
		case EXTENDED_KEY_CENTER:		WriteCSISequence(m,'E'); break;
		case EXTENDED_KEY_END:			WriteCSISequence(m,'F'); break;
		case EXTENDED_KEY_HOME:			WriteCSISequence(m,'H'); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_FIND:			WriteDECVTFunctionKey(1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_INSERT:		WriteDECVTFunctionKey(2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_DELETE:		WriteDECVTFunctionKey(3U,m); break;
		case EXTENDED_KEY_SELECT:		WriteDECVTFunctionKey(4U,m); break;
		case EXTENDED_KEY_PREVIOUS:		WriteDECVTFunctionKey(5U,m); break;
		case EXTENDED_KEY_PAGE_UP:		WriteDECVTFunctionKey(5U,m); break;
		case EXTENDED_KEY_NEXT:			WriteDECVTFunctionKey(6U,m); break;
		case EXTENDED_KEY_PAGE_DOWN:		WriteDECVTFunctionKey(6U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteRawCharacters("\x08"); break;
		default:
			std::fprintf(stderr, "WARNING: %s: %"PRIx32"\n", "Unknown extended key", k);
			break;
	}
}

// These are the sequences defined by xterm's PC mode.
// It is also what the FreeBSD kernel has aimed to produce since version 9.0.
// Note that FreeBSD's teken-based xterm-alike console is idiosyncratic and has already been replaced with yet another rewrite.
// Where FreeBSD differs from xterm's PC mode, we stick with xterm; as that is the documented goal of FreeBSD after all.
//
// Some important differences from xterm's VT220 mode:
//  * Editing/cursor keypad cursor keys use SS3 (not CSI) and a different set of final characters.
//  * Auxiliary keypad cursor keys use CSI (not SS3) and the final characters of the equivalent cursor keypad keys.
//  * Auxiliary keypad insert, delete, page up, and page down are not distinguished from the equivalent editing pad keys.
void 
InputFIFO::WriteExtendedKeyXTermPCMode(uint16_t k, uint8_t m)
{
	switch (k) {
	// The auxiliary keypad is in permanent application mode.
		case EXTENDED_KEY_PAD_ASTERISK:		WriteSS3Sequence(m,'j'); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteSS3Sequence(m,'k'); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteSS3Sequence(m,'l'); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteSS3Sequence(m,'m'); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteSS3Sequence(m,'o'); break;
		case EXTENDED_KEY_PAD_UP:		WriteCSISequence(m,'A'); break;
		case EXTENDED_KEY_PAD_DOWN:		WriteCSISequence(m,'B'); break;
		case EXTENDED_KEY_PAD_RIGHT:		WriteCSISequence(m,'C'); break;
		case EXTENDED_KEY_PAD_LEFT:		WriteCSISequence(m,'D'); break;
		case EXTENDED_KEY_PAD_CENTER:		WriteCSISequence(m,'E'); break;
		case EXTENDED_KEY_PAD_END:		WriteCSISequence(m,'F'); break;
		case EXTENDED_KEY_PAD_HOME:		WriteCSISequence(m,'H'); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteSS3Sequence(m,'M'); break;
		case EXTENDED_KEY_PAD_F1:		WriteSS3Sequence(m,'P'); break;
		case EXTENDED_KEY_PAD_F2:		WriteSS3Sequence(m,'Q'); break;
		case EXTENDED_KEY_PAD_F3:		WriteSS3Sequence(m,'R'); break;
		case EXTENDED_KEY_PAD_F4:		WriteSS3Sequence(m,'S'); break;
		case EXTENDED_KEY_PAD_F5:		WriteSS3Sequence(m,'T'); break;
		case EXTENDED_KEY_PAD_EQUALS:		WriteSS3Sequence(m,'X'); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	WriteSS3Sequence(m,'X'); break;
	// The editing keys are in permanent normal mode.
		case EXTENDED_KEY_SCROLL_UP:		WriteSS3Sequence(INPUT_MODIFIER_SHIFT,'A'); break;
		case EXTENDED_KEY_UP_ARROW:		WriteSS3Sequence(m,'A'); break;
		case EXTENDED_KEY_SCROLL_DOWN:		WriteSS3Sequence(INPUT_MODIFIER_SHIFT,'B'); break;
		case EXTENDED_KEY_DOWN_ARROW:		WriteSS3Sequence(m,'B'); break;
		case EXTENDED_KEY_RIGHT_ARROW:		WriteSS3Sequence(m,'C'); break;
		case EXTENDED_KEY_LEFT_ARROW:		WriteSS3Sequence(m,'D'); break;
		case EXTENDED_KEY_CENTER:		WriteSS3Sequence(m,'E'); break;
		case EXTENDED_KEY_END:			WriteSS3Sequence(m,'F'); break;
		case EXTENDED_KEY_HOME:			WriteSS3Sequence(m,'H'); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_FIND:			WriteDECVTFunctionKey(1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not an xterm key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_INSERT:		// this, which xterm does not distinguish from:
		case EXTENDED_KEY_INSERT:		WriteDECVTFunctionKey(2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not an xterm key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DELETE:		// this, which xterm does not distinguish from:
		case EXTENDED_KEY_DELETE:		WriteDECVTFunctionKey(3U,m); break;
		case EXTENDED_KEY_SELECT:		WriteDECVTFunctionKey(4U,m); break;
		case EXTENDED_KEY_PREVIOUS:		WriteDECVTFunctionKey(5U,m); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		// xterm does not distinguish this from:
		case EXTENDED_KEY_PAGE_UP:		WriteDECVTFunctionKey(5U,m); break;
		case EXTENDED_KEY_NEXT:			WriteDECVTFunctionKey(6U,m); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	// xterm does not distinguish this from:
		case EXTENDED_KEY_PAGE_DOWN:		WriteDECVTFunctionKey(6U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteRawCharacters("\x08"); break;
		default:
			std::fprintf(stderr, "WARNING: %s: %"PRIx32"\n", "Unknown extended key", k);
			break;
	}
}

// This is what a DEC VT520 produces in SCO Console mode.
//
// This emulation is fairly feature-poor:
//  * It makes no distinction between the auxiliary keypad keys and the equivalent editing/cursor keypad keys.
//  * It makes no distinction between the auxiliary keypad keys and the equivalent main keypad keys.
void 
InputFIFO::WriteExtendedKeySCOConsole(uint16_t k, uint8_t m)
{
	switch (k) {
		case EXTENDED_KEY_PAD_UP:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_UP_ARROW:		WriteCSISequence(m,'A'); break;
		case EXTENDED_KEY_PAD_DOWN:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_DOWN_ARROW:		WriteCSISequence(m,'B'); break;
		case EXTENDED_KEY_PAD_RIGHT:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_RIGHT_ARROW:		WriteCSISequence(m,'C'); break;
		case EXTENDED_KEY_PAD_LEFT:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_LEFT_ARROW:		WriteCSISequence(m,'D'); break;
		case EXTENDED_KEY_PAD_CENTER:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_CENTER:		WriteCSISequence(m,'E'); break;
		case EXTENDED_KEY_PAD_END:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_END:			WriteCSISequence(m,'F'); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	// The SCO console does not distinguish this from:
		case EXTENDED_KEY_PAGE_DOWN:		WriteCSISequence(m,'G'); break;
		case EXTENDED_KEY_PAD_HOME:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_HOME:			WriteCSISequence(m,'H'); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_PAGE_UP:		WriteCSISequence(m,'I'); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a SCO console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_INSERT:		// this, which the SCO console does not distinguish from:
		case EXTENDED_KEY_INSERT:		WriteCSISequence(m,'L'); break;
		case EXTENDED_KEY_PAD_F1:		WriteCSISequence(m,'P'); break;
		case EXTENDED_KEY_PAD_F2:		WriteCSISequence(m,'Q'); break;
		case EXTENDED_KEY_PAD_F3:		WriteCSISequence(m,'R'); break;
		case EXTENDED_KEY_PAD_F4:		WriteCSISequence(m,'S'); break;
		case EXTENDED_KEY_PAD_F5:		WriteCSISequence(m,'T'); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_BACKSPACE:		WriteRawCharacters("\x08"); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteRawCharacters("\x0D"); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteRawCharacters("\x0A"); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteRawCharacters(","); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteRawCharacters("/"); break;
		case EXTENDED_KEY_PAD_ASTERISK:		WriteRawCharacters("*"); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteRawCharacters("-"); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteRawCharacters("+"); break;
		case EXTENDED_KEY_PAD_EQUALS:		WriteRawCharacters("="); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	WriteRawCharacters("="); break;
		case EXTENDED_KEY_TAB:			WriteRawCharacters("\x09"); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a SCO console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DELETE:		// this, which the SCO console does not distinguish from:
		case EXTENDED_KEY_DELETE:		WriteRawCharacters("\x7F"); break;
		default:
			std::fprintf(stderr, "WARNING: %s: %"PRIx32"\n", "Unknown extended key", k);
			break;
	}
}

// This is what the Linux kernel terminal emulator produces.
// The termcap/terminfo name is "linux".
//
// This emulation is fairly feature-poor:
//  * It makes no distinction between the auxiliary keypad keys and the equivalent editing/cursor keypad keys.
//  * It makes no distinction between the auxiliary keypad keys and the equivalent main keypad keys.
//  * Auxiliary keypad function keys send sequences too similar to cursor keys.
//  * If a program accidentally uses a vt1xx/vt2xx/xterm/wsvt TERM setting, HOME and END will look like FIND and SELECT.
void 
InputFIFO::WriteExtendedKeyLinuxConsole(uint16_t k, uint8_t m)
{
	switch (k) {
		case EXTENDED_KEY_PAD_UP:		// The Linux console does not distinguish this from:
		case EXTENDED_KEY_UP_ARROW:		WriteCSISequence(m,'A'); break;
		case EXTENDED_KEY_PAD_DOWN:		// The Linux console does not distinguish this from:
		case EXTENDED_KEY_DOWN_ARROW:		WriteCSISequence(m,'B'); break;
		case EXTENDED_KEY_PAD_RIGHT:		// The Linux console does not distinguish this from:
		case EXTENDED_KEY_RIGHT_ARROW:		WriteCSISequence(m,'C'); break;
		case EXTENDED_KEY_PAD_LEFT:		// The Linux console does not distinguish this from:
		case EXTENDED_KEY_LEFT_ARROW:		WriteCSISequence(m,'D'); break;
		case EXTENDED_KEY_PAD_CENTER:		// The Linux console does not distinguish this from:
		case EXTENDED_KEY_CENTER:		WriteCSISequence(m,'G'); break;
		case EXTENDED_KEY_PAD_F1:		WriteLinuxFunctionKey(m,'A'); break;
		case EXTENDED_KEY_PAD_F2:		WriteLinuxFunctionKey(m,'B'); break;
		case EXTENDED_KEY_PAD_F3:		WriteLinuxFunctionKey(m,'C'); break;
		case EXTENDED_KEY_PAD_F4:		WriteLinuxFunctionKey(m,'D'); break;
		case EXTENDED_KEY_PAD_F5:		WriteLinuxFunctionKey(m,'E'); break;
		case EXTENDED_KEY_HOME:			// The Linux console does not distinguish this from:
		case EXTENDED_KEY_PAD_HOME:		WriteDECVTFunctionKey(1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a Linux console key, but we make it equivalent to:
		case EXTENDED_KEY_INSERT:		// this, which the Linux console does not distinguish from:
		case EXTENDED_KEY_PAD_INSERT:		WriteDECVTFunctionKey(2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a Linux console key, but we make it equivalent to:
		case EXTENDED_KEY_DELETE:		// this, which the Linux console does not distinguish from:
		case EXTENDED_KEY_PAD_DELETE:		WriteDECVTFunctionKey(3U,m); break;
		case EXTENDED_KEY_END:			// The Linux console does not distinguish this from:
		case EXTENDED_KEY_PAD_END:		WriteDECVTFunctionKey(4U,m); break;
		case EXTENDED_KEY_PAGE_UP:		// The Linux console does not distinguish this from:
		case EXTENDED_KEY_PAD_PAGE_UP:		WriteDECVTFunctionKey(5U,m); break;
		case EXTENDED_KEY_PAGE_DOWN:		// The Linux console does not distinguish this from:
		case EXTENDED_KEY_PAD_PAGE_DOWN:	WriteDECVTFunctionKey(6U,m); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_BACKSPACE:		WriteRawCharacters("\x7F"); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteRawCharacters("\x0D"); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteRawCharacters("\x0A"); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteRawCharacters(","); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteRawCharacters("/"); break;
		case EXTENDED_KEY_PAD_ASTERISK:		WriteRawCharacters("*"); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteRawCharacters("-"); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteRawCharacters("+"); break;
		case EXTENDED_KEY_PAD_EQUALS:		WriteRawCharacters("="); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	WriteRawCharacters("="); break;
		case EXTENDED_KEY_TAB:			WriteRawCharacters("\x09"); break;
		default:
			std::fprintf(stderr, "WARNING: %s: %"PRIx32"\n", "Unknown extended key", k);
			break;
	}
}

// This is what the NetBSD kernel terminal emulator produces in vt100 mode.
// The termcap/terminfo name is "wsvt".
//
// This emulation is fairly feature-poor:
//  * It makes no distinction between the auxiliary keypad keys and the equivalent editing/cursor keypad keys.
//  * It makes no distinction between the auxiliary keypad keys and the equivalent main keypad keys.
void 
InputFIFO::WriteExtendedKeyNetBSDConsole(uint16_t k, uint8_t m)
{
	switch (k) {
		case EXTENDED_KEY_PAD_UP:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_UP_ARROW:		WriteCSISequence(m,'A'); break;
		case EXTENDED_KEY_PAD_DOWN:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_DOWN_ARROW:		WriteCSISequence(m,'B'); break;
		case EXTENDED_KEY_PAD_RIGHT:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_RIGHT_ARROW:		WriteCSISequence(m,'C'); break;
		case EXTENDED_KEY_PAD_LEFT:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_LEFT_ARROW:		WriteCSISequence(m,'D'); break;
		case EXTENDED_KEY_PAD_CENTER:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_CENTER:		WriteCSISequence(m,'E'); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_INSERT:		// this, which the NetBSD console does not distinguish from:
		case EXTENDED_KEY_INSERT:		WriteCSISequence(m,'L'); break;
		case EXTENDED_KEY_PAD_F1:		WriteCSISequence(m,'P'); break;
		case EXTENDED_KEY_PAD_F2:		WriteCSISequence(m,'Q'); break;
		case EXTENDED_KEY_PAD_F3:		WriteCSISequence(m,'R'); break;
		case EXTENDED_KEY_PAD_F4:		WriteCSISequence(m,'S'); break;
		case EXTENDED_KEY_PAD_F5:		WriteCSISequence(m,'T'); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_FIND:			WriteDECVTFunctionKey(1U,m); break;
		case EXTENDED_KEY_SELECT:		WriteDECVTFunctionKey(4U,m); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_PAGE_DOWN:		WriteDECVTFunctionKey(6U,m); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_PAGE_UP:		WriteDECVTFunctionKey(6U,m); break;
		case EXTENDED_KEY_PAD_HOME:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_HOME:			WriteDECVTFunctionKey(7U,m); break;
		case EXTENDED_KEY_PAD_END:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_END:			WriteDECVTFunctionKey(8U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteRawCharacters("\x08"); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteRawCharacters("\x0D"); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteRawCharacters("\x0A"); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteRawCharacters(","); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteRawCharacters("/"); break;
		case EXTENDED_KEY_PAD_ASTERISK:		WriteRawCharacters("*"); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteRawCharacters("-"); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteRawCharacters("+"); break;
		case EXTENDED_KEY_PAD_EQUALS:		WriteRawCharacters("="); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	WriteRawCharacters("="); break;
		case EXTENDED_KEY_TAB:			WriteRawCharacters("\x09"); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DELETE:		// this, which the NetBSD console does not distinguish from:
		case EXTENDED_KEY_DELETE:		WriteRawCharacters("\x7F"); break;
		default:
			std::fprintf(stderr, "WARNING: %s: %"PRIx32"\n", "Unknown extended key", k);
			break;
	}
}

void 
InputFIFO::WriteExtendedKey(uint16_t k, uint8_t m)
{
#if defined(__LINUX__) || defined(__linux__)
	return WriteExtendedKeyLinuxConsole(k, m);
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	return WriteExtendedKeyXTermPCMode(k, m);
#elif defined(__NetBSD__)
	return WriteExtendedKeyNetBSDConsole(k, m);
#else
	return WriteExtendedKeyDECVT(k, m);
#	error "Don't know what the terminal type for your console driver is."
#endif
}

void 
InputFIFO::WriteFunctionKey(uint16_t k, uint8_t m)
{
	switch (k) {
		case 1:		WriteDECVTFunctionKey(11U,m); break;
		case 2:		WriteDECVTFunctionKey(12U,m); break;
		case 3:		WriteDECVTFunctionKey(13U,m); break;
		case 4:		WriteDECVTFunctionKey(14U,m); break;
		case 5:		WriteDECVTFunctionKey(15U,m); break;
		case 6:		WriteDECVTFunctionKey(17U,m); break;
		case 7:		WriteDECVTFunctionKey(18U,m); break;
		case 8:		WriteDECVTFunctionKey(19U,m); break;
		case 9:		WriteDECVTFunctionKey(20U,m); break;
		case 10:	WriteDECVTFunctionKey(21U,m); break;
		case 11:	WriteDECVTFunctionKey(23U,m); break;
		case 12:	WriteDECVTFunctionKey(24U,m); break;
		case 13:	WriteDECVTFunctionKey(25U,m); break;
		case 14:	WriteDECVTFunctionKey(26U,m); break;
		case 15:	WriteDECVTFunctionKey(28U,m); break;
		case 16:	WriteDECVTFunctionKey(29U,m); break;
		case 17:	WriteDECVTFunctionKey(31U,m); break;
		case 18:	WriteDECVTFunctionKey(32U,m); break;
		case 19:	WriteDECVTFunctionKey(33U,m); break;
		case 20:	WriteDECVTFunctionKey(34U,m); break;
		default:
			std::fprintf(stderr, "WARNING: %s: %"PRIx32"\n", "Unknown function key", k);
			break;
	}
}

void
InputFIFO::ReadInput()
{
	const int l(read(input_fd, input_buffer + input_read, sizeof input_buffer - input_read));
	if (l > 0)
		input_read += l;
	if (input_read >= sizeof input_buffer) {
		uint32_t b;
		std::memcpy(&b, input_buffer, 4U);
		input_read -= 4U;
		switch (b & INPUT_MSG_MASK) {
			case INPUT_MSG_UCS24:	WriteUnicodeCharacter(b & 0x00FFFFFF); break;
			case INPUT_MSG_EKEY:	WriteExtendedKey((b >> 8U) & 0xFFFF, b & 0xFF); break;
			case INPUT_MSG_FKEY:	WriteFunctionKey((b >> 8U) & 0xFF, b & 0xFF); break;
			default:
				std::fprintf(stderr, "WARNING: %s: %"PRIx32"\n", "Unknown input message", b);
				break;
		}
	}
}

void
InputFIFO::WriteOutput()
{
	const int l(write(master_fd, output_buffer, output_pending));
	if (l > 0) {
		std::memmove(output_buffer, output_buffer + l, output_pending - l);
		output_pending -= l;
	}
}

/* Buffer multiplier ********************************************************
// **************************************************************************
*/

class MultipleBuffer : 
	public SoftTerm::ScreenBuffer 
{
public:
	MultipleBuffer();
	~MultipleBuffer();
	void Add(SoftTerm::ScreenBuffer * v) { buffers.push_back(v); }
	virtual void WriteNCells(coordinate p, coordinate n, const CharacterCell & c);
	virtual void ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void SetCursorPos(coordinate x, coordinate y);
	virtual void SetCursorType(CursorSprite::glyph_type, CursorSprite::attribute_type);
	virtual void SetSize(coordinate w, coordinate h);
protected:
	typedef std::list<SoftTerm::ScreenBuffer *> Buffers;
	Buffers buffers;
};

MultipleBuffer::MultipleBuffer() 
{
}

MultipleBuffer::~MultipleBuffer()
{
}

void 
MultipleBuffer::WriteNCells(coordinate s, coordinate n, const CharacterCell & c)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->WriteNCells(s, n, c);
}

void 
MultipleBuffer::ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->ScrollUp(s, e, n, c);
}

void 
MultipleBuffer::ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->ScrollDown(s, e, n, c);
}

void 
MultipleBuffer::SetCursorType(CursorSprite::glyph_type g, CursorSprite::attribute_type a)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->SetCursorType(g, a);
}

void 
MultipleBuffer::SetCursorPos(coordinate x, coordinate y)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->SetCursorPos(x, y);
}

void 
MultipleBuffer::SetSize(coordinate w, coordinate h)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->SetSize(w, h);
}

/* Link the UTF-8 decoder to the terminal emulator **************************
// **************************************************************************
*/

class UnicodeSoftTerm : 
	public UTF8Decoder::UCS32CharacterSink,
	public SoftTerm
{
public:
	UnicodeSoftTerm(ScreenBuffer & s, KeyboardBuffer & k) : SoftTerm(s, k) {}
	virtual void Write(uint32_t character, bool decoder_error, bool overlong);
};

void 
UnicodeSoftTerm::Write(uint32_t character, bool decoder_error, bool overlong)
{
	SoftTerm::Write(character, decoder_error, overlong);
}

/* Signal handling **********************************************************
// **************************************************************************
*/

static sig_atomic_t shutdown_signalled(false);

static
void
handle_signal (
	int signo
) {
	switch (signo) {
		case SIGTERM:	shutdown_signalled = true; break;
		case SIGINT:	shutdown_signalled = true; break;
		case SIGHUP:	shutdown_signalled = true; break;
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_terminal_emulator ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));

	try {
		popt::top_table_definition main_option(0, 0, "Main options", "directory");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing directory name.");
		throw EXIT_FAILURE;
	}
	const char * dirname(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	const char * tty(std::getenv("TTY"));
	if (!tty) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "TTY", "Missing environment variable.");
		throw EXIT_FAILURE;
	}

	const int dir_fd(open_dir_at(AT_FDCWD, dirname));
	if (0 > dir_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/: %s\n", prog, dirname, std::strerror(error));
		throw EXIT_FAILURE;
	}

	// We need an explicit lock file, because we cannot lock FIFOs.
	const int lock_fd(open_lockfile_at(dir_fd, "lock"));
	if (0 > lock_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "lock", std::strerror(error));
		throw EXIT_FAILURE;
	}
	// We are allowed to open the read end of a FIFO in non-blocking mode without having to wait for a writer.
	mkfifoat(dir_fd, "input", 0620);
	const int input_fd(open_read_at(dir_fd, "input"));
	if (0 > input_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	// We have to keep a client (write) end descriptor open to the input FIFO.
	// Otherwise, the first console client process triggers POLLHUP when it closes its end.
	// Opening the FIFO for read+write isn't standard, although it would work on Linux.
	const int input_write_fd(open_writeexisting_at(dir_fd, "input"));
	if (0 > input_write_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	const int vcsa_fd(open_readwritecreate_at(dir_fd, "vcsa", 0640));
	if (0 > vcsa_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "vcsa", std::strerror(error));
		throw EXIT_FAILURE;
	}
	const int unicode_fd(open_readwritecreate_at(dir_fd, "display", 0640));
	if (0 > unicode_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	unlinkat(dir_fd, "tty", 0);
	if (0 > linkat(AT_FDCWD, tty, dir_fd, "tty", 0)) {
		if (0 > symlinkat(tty, dir_fd, "tty")) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s -> %s/tty: %s\n", prog, tty, dirname, std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	// It would be alright to use kqueue here, because we don't fork child processes from this point onwards.
	// However, the Linux kevent() implementation has a bug somewhere that causes it to always return an EVFILT_READ event for TTYs, even if a read would in fact block.

#if defined(__LINUX__) || defined(__linux__)
	struct sigaction sa;
	sa.sa_flags=0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler=handle_signal;
	sigaction(SIGTERM,&sa,NULL);
	sigaction(SIGINT,&sa,NULL);
	sigaction(SIGHUP,&sa,NULL);

	pollfd p[2];
	p[0].fd = PTY_MASTER_FILENO;
	p[0].events = POLLIN|POLLOUT|POLLHUP;
	p[1].fd = input_fd;
	p[1].events = POLLIN;
#else
	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct kevent p[16];

	{
		struct sigaction sa;
		sa.sa_flags=0;
		sigemptyset(&sa.sa_mask);
		// We only need to set handlers for those signals that would otherwise directly terminate the process.
		sa.sa_handler=SIG_IGN;
		sigaction(SIGTERM,&sa,NULL);
		sigaction(SIGINT,&sa,NULL);
		sigaction(SIGHUP,&sa,NULL);

		size_t index(0);
		EV_SET(&p[index++], PTY_MASTER_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], PTY_MASTER_FILENO, EVFILT_WRITE, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], input_fd, EVFILT_READ, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		if (0 > kevent(queue, p, index, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}
#endif

	InputFIFO keyboard(input_fd, PTY_MASTER_FILENO);
	VCSA vbuffer(vcsa_fd);
	UnicodeBuffer ubuffer(unicode_fd);
	MultipleBuffer mbuffer;
	mbuffer.Add(&ubuffer);
	mbuffer.Add(&vbuffer);
	UnicodeSoftTerm emulator(mbuffer, keyboard);
	UTF8Decoder decoder(emulator);

	bool hangup(false);
	while (!shutdown_signalled && !hangup) {
#if defined(__LINUX__) || defined(__linux__)
		if (keyboard.OutputAvailable()) p[0].events |= POLLOUT; else p[0].events &= ~POLLOUT;
		if (keyboard.HasInputSpace()) p[1].events |= POLLIN; else p[1].events &= ~POLLIN;
		const int rc(poll(p, sizeof p/sizeof *p, -1));
#else
		EV_SET(&p[0], PTY_MASTER_FILENO, EVFILT_WRITE, keyboard.OutputAvailable() ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		EV_SET(&p[1], input_fd, EVFILT_READ, keyboard.HasInputSpace() ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		const int rc(kevent(queue, p, 1, p, sizeof p/sizeof *p, 0));
#endif

		if (0 > rc) {
			if (EINTR == errno) continue;
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}

#if defined(__LINUX__) || defined(__linux__)
		const bool masterin_ready(p[0].revents & POLLIN);
		const bool masterout_ready(p[0].revents & POLLOUT);
		bool master_hangup(p[0].revents & POLLHUP);
		const bool fifo_ready(p[1].revents & POLLIN);
#else
		bool masterin_ready(false), masterout_ready(false), master_hangup(false), fifo_ready(false), fifo_hangup(false);

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			if (EVFILT_SIGNAL == e.filter)
				handle_signal(e.ident);
			if (EVFILT_READ == e.filter && PTY_MASTER_FILENO == e.ident) {
				masterin_ready = true;
				master_hangup |= EV_EOF & e.flags;
			}
			if (EVFILT_READ == e.filter && input_fd == static_cast<int>(e.ident)) {
				fifo_ready = true;
				fifo_hangup |= EV_EOF & e.flags;
			}
			if (EVFILT_WRITE == e.filter && PTY_MASTER_FILENO == e.ident)
				masterout_ready = true;
		}
#endif

		if (masterin_ready) {
			char b[4096];
			const int l(read(PTY_MASTER_FILENO, b, sizeof b));
			if (l > 0) {
				for (int i(0); i < l; ++i)
					decoder.SendUTF8(b[i]);
				master_hangup = false;
			}
		}
		if (fifo_ready)
			keyboard.ReadInput();
		if (master_hangup)
			hangup = true;
		if (masterout_ready) 
			keyboard.WriteOutput();
	}

	unlinkat(dir_fd, "tty", 0);
	throw EXIT_SUCCESS;
}
