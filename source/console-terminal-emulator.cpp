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
#include "kqueue_common.h"
#include <sys/ioctl.h>
#include <unistd.h>
#include "popt.h"
#include "fdutils.h"
#include "ttyutils.h"
#include "utils.h"
#include "ProcessEnvironment.h"
#include "SoftTerm.h"
#include "InputMessage.h"
#include "FileDescriptorOwner.h"
#include "SignalManagement.h"

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

namespace {
class VCSA : 
	public SoftTerm::ScreenBuffer,
	public FileDescriptorOwner
{
public:
	VCSA(int d) : FileDescriptorOwner(d) {}
	virtual void WriteNCells(coordinate p, coordinate n, const CharacterCell & c);
	virtual void CopyNCells(coordinate d, coordinate s, coordinate n);
	virtual void ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void SetCursorPos(coordinate x, coordinate y);
	virtual void SetCursorType(CursorSprite::glyph_type, CursorSprite::attribute_type);
	virtual void SetPointerType(PointerSprite::attribute_type);
	virtual void SetSize(coordinate w, coordinate h);
protected:
	void MakeCA(char ca[2], const CharacterCell & c);
	enum { CELL_LENGTH = 2U, HEADER_LENGTH = 4U };
	off_t MakeOffset(coordinate s) { return HEADER_LENGTH + CELL_LENGTH * s; }
};
}

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
VCSA::CopyNCells(coordinate d, coordinate s, coordinate n)
{
	char ca[2];
	if (d < s) {
		while (n) {
			const off_t target(MakeOffset(d));
			const off_t source(MakeOffset(s));
			pread(fd, ca, sizeof ca, source);
			pwrite(fd, ca, sizeof ca, target);
			++s;
			++d;
			--n;
		}
	} else 
	if (d > s) {
		s += n;
		d += n;
		while (n) {
			--s;
			--d;
			--n;
			const off_t target(MakeOffset(d));
			const off_t source(MakeOffset(s));
			pread(fd, ca, sizeof ca, source);
			pwrite(fd, ca, sizeof ca, target);
		}
	}
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
	while (e > s + n) {
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
VCSA::SetPointerType(PointerSprite::attribute_type)
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

namespace {
class UnicodeBuffer : 
	public SoftTerm::ScreenBuffer,
	public FileDescriptorOwner
{
public:
	UnicodeBuffer(int d) : FileDescriptorOwner(d) {}
	void WriteBOM();
	virtual void WriteNCells(coordinate p, coordinate n, const CharacterCell & c);
	virtual void CopyNCells(coordinate d, coordinate s, coordinate n);
	virtual void ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void SetCursorPos(coordinate x, coordinate y);
	virtual void SetCursorType(CursorSprite::glyph_type, CursorSprite::attribute_type);
	virtual void SetPointerType(PointerSprite::attribute_type);
	virtual void SetSize(coordinate w, coordinate h);
protected:
	enum { CELL_LENGTH = 16U, HEADER_LENGTH = 16U };
	void MakeCA(char c[CELL_LENGTH], const CharacterCell & cell);
	off_t MakeOffset(coordinate s) { return HEADER_LENGTH + CELL_LENGTH * static_cast<off_t>(s); }
};
}

void
UnicodeBuffer::WriteBOM() 
{
	const uint32_t bom(0xFEFF);
	pwrite(fd, &bom, sizeof bom, 0U);
}

void 
UnicodeBuffer::MakeCA(char c[CELL_LENGTH], const CharacterCell & cell)
{
	c[0] = cell.foreground.alpha;
	c[1] = cell.foreground.red;
	c[2] = cell.foreground.green;
	c[3] = cell.foreground.blue;
	c[4] = cell.background.alpha;
	c[5] = cell.background.red;
	c[6] = cell.background.green;
	c[7] = cell.background.blue;
	std::memcpy(&c[8], &cell.character, 4);
	c[12] = cell.attributes;
	c[13] = 0;
	c[14] = 0;
	c[15] = 0;
}

void 
UnicodeBuffer::WriteNCells(coordinate s, coordinate n, const CharacterCell & c)
{
	char ca[CELL_LENGTH * 256U];
	for (coordinate i(0U); i < (sizeof ca / CELL_LENGTH); ++i)
		MakeCA(ca + CELL_LENGTH * i, c);
	for (coordinate i(0U), w(sizeof ca / CELL_LENGTH); i < n; i += w) {
		if (w > n) w = n;
		pwrite(fd, ca, CELL_LENGTH * w, MakeOffset(s + i));
	}
}

void 
UnicodeBuffer::CopyNCells(coordinate d, coordinate s, coordinate n)
{
	char ca[CELL_LENGTH];
	if (d < s) {
		while (n) {
			const off_t target(MakeOffset(d));
			const off_t source(MakeOffset(s));
			pread(fd, ca, sizeof ca, source);
			pwrite(fd, ca, sizeof ca, target);
			++s;
			++d;
			--n;
		}
	} else 
	if (d > s) {
		s += n;
		d += n;
		while (n) {
			--s;
			--d;
			--n;
			const off_t target(MakeOffset(d));
			const off_t source(MakeOffset(s));
			pread(fd, ca, sizeof ca, source);
			pwrite(fd, ca, sizeof ca, target);
		}
	}
}

void 
UnicodeBuffer::ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c)
{
	char ca[CELL_LENGTH * 256U];
	for (coordinate w(sizeof ca / CELL_LENGTH); s + n < e; s += w) {
		if (s + n + w > e) w = e - (s + n);
		const off_t target(MakeOffset(s));
		const off_t source(MakeOffset(s + n));
		pread(fd, ca, CELL_LENGTH * w, source);
		pwrite(fd, ca, CELL_LENGTH * w, target);
	}
	for (coordinate i(0U); i < (sizeof ca / CELL_LENGTH); ++i)
		MakeCA(ca + CELL_LENGTH * i, c);
	for (coordinate w(sizeof ca / CELL_LENGTH); s < e; s += w) {
		if (s + w > e) w = e - s;
		pwrite(fd, ca, CELL_LENGTH * w, MakeOffset(s));
	}
}

void 
UnicodeBuffer::ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c)
{
	char ca[CELL_LENGTH * 256U];
	for (coordinate w(sizeof ca / CELL_LENGTH); e > s + n; ) {
		if (s + n + w > e) w = e - (s + n);
		e -= w;
		const off_t target(MakeOffset(e));
		const off_t source(MakeOffset(e - n));
		pread(fd, ca, CELL_LENGTH * w, source);
		pwrite(fd, ca, CELL_LENGTH * w, target);
	}
	for (coordinate i(0U); i < (sizeof ca / CELL_LENGTH); ++i)
		MakeCA(ca + CELL_LENGTH * i, c);
	for (coordinate w(sizeof ca / CELL_LENGTH); e > s; ) {
		if (s + w > e) w = e - s;
		e -= w;
		pwrite(fd, ca, CELL_LENGTH * w, MakeOffset(e));
	}
}

void 
UnicodeBuffer::SetCursorType(CursorSprite::glyph_type g, CursorSprite::attribute_type a)
{
	const char b[2] = { static_cast<char>(g), static_cast<char>(a) };
	pwrite(fd, b, sizeof b, 12U);
}

void 
UnicodeBuffer::SetPointerType(PointerSprite::attribute_type a)
{
	const char b[1] = { static_cast<char>(a) };
	pwrite(fd, b, sizeof b, 14U);
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

/* input side ***************************************************************
// **************************************************************************
*/

namespace {
class InputFIFO :
	public SoftTerm::KeyboardBuffer,
	public SoftTerm::MouseBuffer,
	public FileDescriptorOwner
{
public:
	enum Emulation { SCO_CONSOLE, LINUX_CONSOLE, NETBSD_CONSOLE, XTERM_PC, DECVT };
	InputFIFO(int, int, Emulation);
	void ReadInput();
	void WriteOutput();
	bool OutputAvailable() { return output_pending > 0U; }
	bool HasInputSpace() { return output_pending < sizeof output_buffer; }
protected:
	const int master_fd;
	virtual void WriteLatin1Characters(std::size_t, const char *);
	virtual void WriteControl1Character(uint8_t);
	virtual void Set8BitControl1(bool);
	virtual void SetBackspaceIsBS(bool);
	virtual void SetDeleteIsDEL(bool);
	virtual void SetSendPasteEvent(bool);
	virtual void ReportSize(coordinate w, coordinate h);
	virtual void SetSendXTermMouse(bool);
	virtual void SetSendXTermMouseClicks(bool);
	virtual void SetSendXTermMouseButtonMotions(bool);
	virtual void SetSendXTermMouseNoButtonMotions(bool);
	virtual void SetSendDECLocator(unsigned int);
	virtual void SetSendDECLocatorPressEvent(bool);
	virtual void SetSendDECLocatorReleaseEvent(bool);
	virtual void RequestDECLocatorReport();
	void WriteRawCharacters(std::size_t, const char *);
	void WriteRawCharacters(const char * s);
	void WriteCSI();
	void WriteSS3();
	void WriteUnicodeCharacter(uint32_t c);
	void WriteBackspaceOrDEL();
	void WriteCSISequence(unsigned m, char c);
	void WriteSS3Sequence(unsigned m, char c);
	void SetPasting(bool p);
	void WriteUCS3Character(uint32_t c, bool p);
	void WriteFunctionKeyDECVT1(unsigned n, unsigned m);
	void WriteFunctionKeyLinuxConsole2(unsigned m, char c);
	void WriteFunctionKeySCOConsole1(char c);
	void WriteFunctionKeyDECVT(uint16_t k, uint8_t m);
	void WriteFunctionKeySCOConsole(uint16_t k, uint8_t m);
	void WriteFunctionKey(uint16_t k, uint8_t m);
	void WriteExtendedKeyDECVT(uint16_t k, uint8_t m);
	void WriteExtendedKeySCOConsole(uint16_t k, uint8_t m);
	void WriteExtendedKeyLinuxConsole(uint16_t k, uint8_t m);
	void WriteExtendedKeyNetBSDConsole(uint16_t k, uint8_t m);
	void WriteExtendedKeyXTermPCMode(uint16_t k, uint8_t m);
	void WriteExtendedKey(uint16_t k, uint8_t m);
	void SetMouseX(uint16_t p, uint8_t m);
	void SetMouseY(uint16_t p, uint8_t m);
	void SetMouseButton(uint8_t b, bool v, uint8_t m);
	void WriteWheelMotion(uint8_t b, int8_t o, uint8_t m);
	void WriteXTermMouse(int button, uint8_t modifiers);
	void WriteDECLocatorReport(int button);
	const Emulation emulation;
	bool send_8bit_controls, backspace_is_bs, delete_is_del;
	bool send_xterm_mouse, send_xterm_mouse_clicks, send_xterm_mouse_button_motions, send_xterm_mouse_nobutton_motions, send_locator_press_events, send_locator_release_events, send_paste;
	unsigned int send_locator_mode;
	char input_buffer[256];
	std::size_t input_read;
	char output_buffer[4096];
	std::size_t output_pending;
	uint16_t mouse_column, mouse_row;
	bool mouse_buttons[8];
	bool pasting;
};
}

InputFIFO::InputFIFO(int i, int m, Emulation e) : 
	FileDescriptorOwner(i),
	master_fd(m),
	emulation(e),
	send_8bit_controls(false),
	backspace_is_bs(false),
	delete_is_del(false),
	send_xterm_mouse(false), 
	send_xterm_mouse_clicks(false), 
	send_xterm_mouse_button_motions(false), 
	send_xterm_mouse_nobutton_motions(false),
	send_locator_press_events(false),
	send_locator_release_events(false),
	send_paste(false),
	send_locator_mode(0U),
	input_read(0U),
	output_pending(0U),
	mouse_column(0U),
	mouse_row(0U),
	pasting(false)
{
	for (std::size_t j(0U); j < sizeof mouse_buttons/sizeof *mouse_buttons; ++j)
		mouse_buttons[j] = false;
}

void 
InputFIFO::WriteRawCharacters(std::size_t l, const char * p)
{
	if (l > (sizeof output_buffer - output_pending))
		l = sizeof output_buffer - output_pending;
	std::memmove(output_buffer + output_pending, p, l);
	output_pending += l;
}

inline 
void 
InputFIFO::WriteRawCharacters(const char * s) 
{ 
	WriteRawCharacters(std::strlen(s), s); 
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
			static_cast<char>(0xC0 | (0x1F & (c >> 6U))),
			static_cast<char>(0x80 | (0x3F & (c >> 0U))),
		};
		WriteRawCharacters(sizeof s, s);
	} else
	if (c < 0x00010000) {
		const char s[3] = { 
			static_cast<char>(0xE0 | (0x0F & (c >> 12U))),
			static_cast<char>(0x80 | (0x3F & (c >> 6U))),
			static_cast<char>(0x80 | (0x3F & (c >> 0U))),
		};
		WriteRawCharacters(sizeof s, s);
	} else
	if (c < 0x00200000) {
		const char s[4] = { 
			static_cast<char>(0xF0 | (0x07 & (c >> 18U))),
			static_cast<char>(0x80 | (0x3F & (c >> 12U))),
			static_cast<char>(0x80 | (0x3F & (c >> 6U))),
			static_cast<char>(0x80 | (0x3F & (c >> 0U))),
		};
		WriteRawCharacters(sizeof s, s);
	} else
	if (c < 0x04000000) {
		const char s[5] = { 
			static_cast<char>(0xF8 | (0x03 & (c >> 24U))),
			static_cast<char>(0x80 | (0x3F & (c >> 18U))),
			static_cast<char>(0x80 | (0x3F & (c >> 12U))),
			static_cast<char>(0x80 | (0x3F & (c >> 6U))),
			static_cast<char>(0x80 | (0x3F & (c >> 0U))),
		};
		WriteRawCharacters(sizeof s, s);
	} else
	{
		const char s[6] = { 
			static_cast<char>(0xFC | (0x01 & (c >> 30U))),
			static_cast<char>(0x80 | (0x3F & (c >> 24U))),
			static_cast<char>(0x80 | (0x3F & (c >> 18U))),
			static_cast<char>(0x80 | (0x3F & (c >> 12U))),
			static_cast<char>(0x80 | (0x3F & (c >> 6U))),
			static_cast<char>(0x80 | (0x3F & (c >> 0U))),
		};
		WriteRawCharacters(sizeof s, s);
	}
}

void 
InputFIFO::WriteControl1Character(uint8_t c)
{
	if (send_8bit_controls)
		WriteUnicodeCharacter(c);
	else {
		WriteUnicodeCharacter('\x1b');
		WriteUnicodeCharacter(c - 0x40);
	}
}

void 
InputFIFO::Set8BitControl1(bool b)
{
	send_8bit_controls = b;
}

void 
InputFIFO::SetBackspaceIsBS(bool b)
{
	backspace_is_bs = b;
}

void 
InputFIFO::SetDeleteIsDEL(bool b)
{
	delete_is_del = b;
}

void 
InputFIFO::SetSendPasteEvent(bool b)
{
	send_paste = b;
}

void 
InputFIFO::SetSendXTermMouse(bool b)
{
	send_xterm_mouse = b;
}

void 
InputFIFO::SetSendXTermMouseClicks(bool b)
{
	send_xterm_mouse_clicks = b;
}

void 
InputFIFO::SetSendXTermMouseButtonMotions(bool b)
{
	send_xterm_mouse_button_motions = b;
}

void 
InputFIFO::SetSendXTermMouseNoButtonMotions(bool b)
{
	send_xterm_mouse_nobutton_motions = b;
}

void 
InputFIFO::SetSendDECLocator(unsigned int mode)
{
	send_locator_mode = mode;
}

void 
InputFIFO::SetSendDECLocatorPressEvent(bool b)
{
	send_locator_press_events = b;
}

void 
InputFIFO::SetSendDECLocatorReleaseEvent(bool b)
{
	send_locator_release_events = b;
}

void
InputFIFO::WriteBackspaceOrDEL()
{
	WriteRawCharacters(backspace_is_bs ? "\x08" : "\x7F"); 	// We can bypass UTF-8 encoding as we guarantee ASCII.
}

inline 
void 
InputFIFO::WriteCSI() 
{ 
	WriteControl1Character('\x9b'); 
}

inline 
void 
InputFIFO::WriteSS3() 
{ 
	WriteControl1Character('\x8f'); 
}

void 
InputFIFO::WriteCSISequence(unsigned m, char c)
{
	WriteCSI();
	if (0 != m) {
		char b[16];
		const int n(snprintf(b, sizeof b, "1;%u%c", m + 1U, c));
		WriteRawCharacters(n, b);	// We can bypass UTF-8 encoding as we guarantee ASCII.
	} else
		WriteUnicodeCharacter(c);
}

void 
InputFIFO::WriteSS3Sequence(unsigned m, char c)
{
	WriteSS3();
	if (0 != m) {
		char b[16];
		const int n(snprintf(b, sizeof b, "1;%u%c", m + 1U, c));
		WriteRawCharacters(n, b);	// We can bypass UTF-8 encoding as we guarantee ASCII.
	} else
		WriteUnicodeCharacter(c);
}

/// \brief DECFNK
void 
InputFIFO::WriteFunctionKeyDECVT1(unsigned n, unsigned m)
{
	WriteCSI();
	char b[16];
	if (0 != m)
		snprintf(b, sizeof b, "%u;%u~", n, m + 1U);
	else
		snprintf(b, sizeof b, "%u~", n);
	WriteRawCharacters(b);	// We can bypass UTF-8 encoding as we guarantee ASCII.
}

void 
InputFIFO::WriteFunctionKeyLinuxConsole2(unsigned m, char c)
{
	WriteCSI();
	char b[16];
	if (0 != m)
		snprintf(b, sizeof b, "[1;%u%c", m + 1U, c);
	else
		snprintf(b, sizeof b, "[%c", c);
	WriteRawCharacters(b);	// We can bypass UTF-8 encoding as we guarantee ASCII.
}

void 
InputFIFO::WriteFunctionKeySCOConsole1(char c)
{
	WriteCSI();
	WriteUnicodeCharacter(c);
}

void 
InputFIFO::SetPasting(const bool p)
{
	if (p == pasting) return;
	pasting = p;
	if (send_paste)
		WriteFunctionKeyDECVT1(pasting ? 200 : 201, 0);
}

void 
InputFIFO::WriteFunctionKeyDECVT(uint16_t k, uint8_t m)
{
	switch (k) {
		case 1:		WriteFunctionKeyDECVT1(11U,m); break;
		case 2:		WriteFunctionKeyDECVT1(12U,m); break;
		case 3:		WriteFunctionKeyDECVT1(13U,m); break;
		case 4:		WriteFunctionKeyDECVT1(14U,m); break;
		case 5:		WriteFunctionKeyDECVT1(15U,m); break;
		case 6:		WriteFunctionKeyDECVT1(17U,m); break;
		case 7:		WriteFunctionKeyDECVT1(18U,m); break;
		case 8:		WriteFunctionKeyDECVT1(19U,m); break;
		case 9:		WriteFunctionKeyDECVT1(20U,m); break;
		case 10:	WriteFunctionKeyDECVT1(21U,m); break;
		case 11:	WriteFunctionKeyDECVT1(23U,m); break;
		case 12:	WriteFunctionKeyDECVT1(24U,m); break;
		case 13:	WriteFunctionKeyDECVT1(25U,m); break;
		case 14:	WriteFunctionKeyDECVT1(26U,m); break;
		case 15:	WriteFunctionKeyDECVT1(28U,m); break;
		case 16:	WriteFunctionKeyDECVT1(29U,m); break;
		case 17:	WriteFunctionKeyDECVT1(31U,m); break;
		case 18:	WriteFunctionKeyDECVT1(32U,m); break;
		case 19:	WriteFunctionKeyDECVT1(33U,m); break;
		case 20:	WriteFunctionKeyDECVT1(34U,m); break;
		case 21:	WriteFunctionKeyDECVT1(35U,m); break;
		case 22:	WriteFunctionKeyDECVT1(36U,m); break;
		default:
			std::fprintf(stderr, "WARNING: Function key #%" PRId32 " does not have a DEC VT mapping.\n", k);
			break;
	}
}

void 
InputFIFO::WriteFunctionKeySCOConsole(uint16_t k, uint8_t /*m*/)
{
	static const char other[9] = "@[<]^_'{";
	if (15U > k)
		WriteFunctionKeySCOConsole1(k - 1U + 'M');
	else
	if (41U > k)
		WriteFunctionKeySCOConsole1(k - 15U + 'a');
	else
	if (49U > k)
		WriteFunctionKeySCOConsole1(other[k - 41U]);
}

void 
InputFIFO::WriteFunctionKey(uint16_t k, uint8_t m)
{
	switch (emulation) {
		case SCO_CONSOLE:	return WriteFunctionKeySCOConsole(k, m);
		case LINUX_CONSOLE:
		case XTERM_PC:	
		case NETBSD_CONSOLE:
		case DECVT:
		default:		return WriteFunctionKeyDECVT(k, m);
	}
}

// These are the sequences defined by the DEC VT520 programmers' reference.
// Most termcaps/terminfos describe this as "vt220".
//
// * This doesn't implement ALT+arrows turning into function keys 7 to 10.
void 
InputFIFO::WriteExtendedKeyDECVT(uint16_t k, uint8_t m)
{
	switch (k) {
	// The calculator keypad is in permanent application mode; as NumLock has no meaning here.
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
		case EXTENDED_KEY_PAD_CENTRE:		WriteSS3Sequence(m,'u'); break;
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
	// The editing keys are in permanent application mode; as local editing has no meaning here.
		case EXTENDED_KEY_UP_ARROW:		WriteCSISequence(m,'A'); break;
		case EXTENDED_KEY_DOWN_ARROW:		WriteCSISequence(m,'B'); break;
		case EXTENDED_KEY_RIGHT_ARROW:		WriteCSISequence(m,'C'); break;
		case EXTENDED_KEY_LEFT_ARROW:		WriteCSISequence(m,'D'); break;
		case EXTENDED_KEY_CENTRE:		WriteCSISequence(m,'E'); break;
		case EXTENDED_KEY_END:			WriteCSISequence(m,'F'); break;
		case EXTENDED_KEY_HOME:			WriteCSISequence(m,'H'); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_FIND:			WriteFunctionKeyDECVT1(1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_INSERT:		WriteFunctionKeyDECVT1(2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_DELETE:		WriteFunctionKeyDECVT1(3U,m); break;
		case EXTENDED_KEY_SELECT:		WriteFunctionKeyDECVT1(4U,m); break;
		case EXTENDED_KEY_PREVIOUS:		WriteFunctionKeyDECVT1(5U,m); break;
		case EXTENDED_KEY_PAGE_UP:		WriteFunctionKeyDECVT1(5U,m); break;
		case EXTENDED_KEY_NEXT:			WriteFunctionKeyDECVT1(6U,m); break;
		case EXTENDED_KEY_PAGE_DOWN:		WriteFunctionKeyDECVT1(6U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteRawCharacters("\x0D"); break;
		default:
			std::fprintf(stderr, "WARNING: %s: %" PRIx32 "\n", "Unknown extended key", k);
			break;
	}
}

// These are the sequences defined by xterm's PC mode.
// It is also what the FreeBSD kernel has aimed to produce since version 9.0.
// Where FreeBSD differs from xterm's PC mode, we stick with xterm; as that is the documented goal of FreeBSD after all.
//
// Some important differences from xterm's VT220 mode:
//  * Editing/cursor keypad cursor keys use SS3 (not CSI) and a different set of final characters.
//  * Calculator keypad cursor keys use CSI (not SS3) and the final characters of the equivalent cursor keypad keys.
//  * Calculator keypad insert, delete, page up, and page down are not distinguished from the equivalent editing pad keys.
void 
InputFIFO::WriteExtendedKeyXTermPCMode(uint16_t k, uint8_t m)
{
	switch (k) {
	// The calculator keypad is in permanent application mode; as NumLock has no meaning here.
		case EXTENDED_KEY_PAD_ASTERISK:		WriteSS3Sequence(m,'j'); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteSS3Sequence(m,'k'); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteSS3Sequence(m,'l'); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteSS3Sequence(m,'m'); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteSS3Sequence(m,'o'); break;
		case EXTENDED_KEY_PAD_UP:		WriteCSISequence(m,'A'); break;
		case EXTENDED_KEY_PAD_DOWN:		WriteCSISequence(m,'B'); break;
		case EXTENDED_KEY_PAD_RIGHT:		WriteCSISequence(m,'C'); break;
		case EXTENDED_KEY_PAD_LEFT:		WriteCSISequence(m,'D'); break;
		case EXTENDED_KEY_PAD_CENTRE:		WriteCSISequence(m,'E'); break;
		case EXTENDED_KEY_PAD_END:		WriteCSISequence(m,'F'); break;
		case EXTENDED_KEY_PAD_HOME:		WriteCSISequence(m,'H'); break;
		case EXTENDED_KEY_PAD_TAB:		WriteSS3Sequence(m,'I'); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteSS3Sequence(m,'M'); break;
		case EXTENDED_KEY_PAD_F1:		WriteSS3Sequence(m,'P'); break;
		case EXTENDED_KEY_PAD_F2:		WriteSS3Sequence(m,'Q'); break;
		case EXTENDED_KEY_PAD_F3:		WriteSS3Sequence(m,'R'); break;
		case EXTENDED_KEY_PAD_F4:		WriteSS3Sequence(m,'S'); break;
		case EXTENDED_KEY_PAD_F5:		WriteSS3Sequence(m,'T'); break;
		case EXTENDED_KEY_PAD_EQUALS:		WriteSS3Sequence(m,'X'); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	WriteSS3Sequence(m,'X'); break;
	// The editing keys are in permanent normal mode; as local editing has no meaning here.
		case EXTENDED_KEY_SCROLL_UP:		WriteSS3Sequence(INPUT_MODIFIER_LEVEL2,'A'); break;
		case EXTENDED_KEY_UP_ARROW:		WriteSS3Sequence(m,'A'); break;
		case EXTENDED_KEY_SCROLL_DOWN:		WriteSS3Sequence(INPUT_MODIFIER_LEVEL2,'B'); break;
		case EXTENDED_KEY_DOWN_ARROW:		WriteSS3Sequence(m,'B'); break;
		case EXTENDED_KEY_RIGHT_ARROW:		WriteSS3Sequence(m,'C'); break;
		case EXTENDED_KEY_LEFT_ARROW:		WriteSS3Sequence(m,'D'); break;
		case EXTENDED_KEY_CENTRE:		WriteSS3Sequence(m,'E'); break;
		case EXTENDED_KEY_END:			WriteSS3Sequence(m,'F'); break;
		case EXTENDED_KEY_HOME:			WriteSS3Sequence(m,'H'); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_FIND:			WriteFunctionKeyDECVT1(1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not an xterm key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_INSERT:		// this, which xterm does not distinguish from:
		case EXTENDED_KEY_INSERT:		WriteFunctionKeyDECVT1(2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not an xterm key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DELETE:		// this, which xterm does not distinguish from:
		case EXTENDED_KEY_DELETE:		WriteFunctionKeyDECVT1(3U,m); break;
		case EXTENDED_KEY_SELECT:		WriteFunctionKeyDECVT1(4U,m); break;
		case EXTENDED_KEY_PREVIOUS:		WriteFunctionKeyDECVT1(5U,m); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		// xterm does not distinguish this from:
		case EXTENDED_KEY_PAGE_UP:		WriteFunctionKeyDECVT1(5U,m); break;
		case EXTENDED_KEY_NEXT:			WriteFunctionKeyDECVT1(6U,m); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	// xterm does not distinguish this from:
		case EXTENDED_KEY_PAGE_DOWN:		WriteFunctionKeyDECVT1(6U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteRawCharacters("\x0D"); break;
		default:
			std::fprintf(stderr, "WARNING: %s: %" PRIx32 "\n", "Unknown extended key", k);
			break;
	}
}

// This is what a DEC VT520 produces in SCO Console mode.
//
// This emulation is fairly feature-poor:
//  * It makes no distinction between the calculator keypad keys and the equivalent editing/cursor keypad keys.
//  * It makes no distinction between the calculator keypad keys and the equivalent main keypad keys.
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
		case EXTENDED_KEY_PAD_CENTRE:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_CENTRE:		WriteCSISequence(m,'E'); break;
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
		case EXTENDED_KEY_PAD_F1:		WriteCSISequence(m,'M'); break;
		case EXTENDED_KEY_PAD_F2:		WriteCSISequence(m,'N'); break;
		case EXTENDED_KEY_PAD_F3:		WriteCSISequence(m,'O'); break;
		case EXTENDED_KEY_PAD_F4:		WriteCSISequence(m,'P'); break;
		case EXTENDED_KEY_PAD_F5:		WriteCSISequence(m,'Q'); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteRawCharacters("\x0D"); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteRawCharacters("\x0A"); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteRawCharacters(","); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteRawCharacters("/"); break;
		case EXTENDED_KEY_PAD_ASTERISK:		WriteRawCharacters("*"); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteRawCharacters("-"); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteRawCharacters("+"); break;
		case EXTENDED_KEY_PAD_EQUALS:		WriteRawCharacters("="); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	WriteRawCharacters("="); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a SCO console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DELETE:		// this, which the SCO console does not distinguish from:
		case EXTENDED_KEY_DELETE:		WriteRawCharacters("\x7F"); break;
		default:
			std::fprintf(stderr, "WARNING: %s: %" PRIx32 "\n", "Unknown extended key", k);
			break;
	}
}

// This is what the Linux kernel terminal emulator produces.
// The termcap/terminfo name is "linux".
//
// This emulation is fairly feature-poor:
//  * It makes no distinction between the calculator keypad keys and the equivalent editing/cursor keypad keys.
//  * It makes no distinction between the calculator keypad keys and the equivalent main keypad keys.
//  * Calculator keypad function keys send sequences too similar to cursor keys.
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
		case EXTENDED_KEY_PAD_CENTRE:		// The Linux console does not distinguish this from:
		case EXTENDED_KEY_CENTRE:		WriteCSISequence(m,'G'); break;
		case EXTENDED_KEY_PAD_F1:		WriteFunctionKeyLinuxConsole2(m,'A'); break;
		case EXTENDED_KEY_PAD_F2:		WriteFunctionKeyLinuxConsole2(m,'B'); break;
		case EXTENDED_KEY_PAD_F3:		WriteFunctionKeyLinuxConsole2(m,'C'); break;
		case EXTENDED_KEY_PAD_F4:		WriteFunctionKeyLinuxConsole2(m,'D'); break;
		case EXTENDED_KEY_PAD_F5:		WriteFunctionKeyLinuxConsole2(m,'E'); break;
		case EXTENDED_KEY_HOME:			// The Linux console does not distinguish this from:
		case EXTENDED_KEY_PAD_HOME:		WriteFunctionKeyDECVT1(1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a Linux console key, but we make it equivalent to:
		case EXTENDED_KEY_INSERT:		// this, which the Linux console does not distinguish from:
		case EXTENDED_KEY_PAD_INSERT:		WriteFunctionKeyDECVT1(2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a Linux console key, but we make it equivalent to:
		case EXTENDED_KEY_DELETE:		// this, which the Linux console does not distinguish from:
		case EXTENDED_KEY_PAD_DELETE:		WriteFunctionKeyDECVT1(3U,m); break;
		case EXTENDED_KEY_END:			// The Linux console does not distinguish this from:
		case EXTENDED_KEY_PAD_END:		WriteFunctionKeyDECVT1(4U,m); break;
		case EXTENDED_KEY_PAGE_UP:		// The Linux console does not distinguish this from:
		case EXTENDED_KEY_PAD_PAGE_UP:		WriteFunctionKeyDECVT1(5U,m); break;
		case EXTENDED_KEY_PAGE_DOWN:		// The Linux console does not distinguish this from:
		case EXTENDED_KEY_PAD_PAGE_DOWN:	WriteFunctionKeyDECVT1(6U,m); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteRawCharacters("\x0D"); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteRawCharacters("\x0A"); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteRawCharacters(","); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteRawCharacters("/"); break;
		case EXTENDED_KEY_PAD_ASTERISK:		WriteRawCharacters("*"); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteRawCharacters("-"); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteRawCharacters("+"); break;
		case EXTENDED_KEY_PAD_EQUALS:		WriteRawCharacters("="); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	WriteRawCharacters("="); break;
		default:
			std::fprintf(stderr, "WARNING: %s: %" PRIx32 "\n", "Unknown extended key", k);
			break;
	}
}

// This is what the NetBSD kernel terminal emulator produces in vt100 mode.
// The termcap/terminfo name is "wsvt".
//
// This emulation is fairly feature-poor:
//  * It makes no distinction between the calculator keypad keys and the equivalent editing/cursor keypad keys.
//  * It makes no distinction between the calculator keypad keys and the equivalent main keypad keys.
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
		case EXTENDED_KEY_PAD_CENTRE:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_CENTRE:		WriteCSISequence(m,'E'); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_INSERT:		// this, which the NetBSD console does not distinguish from:
		case EXTENDED_KEY_INSERT:		WriteCSISequence(m,'L'); break;
		case EXTENDED_KEY_PAD_F1:		WriteCSISequence(m,'P'); break;
		case EXTENDED_KEY_PAD_F2:		WriteCSISequence(m,'Q'); break;
		case EXTENDED_KEY_PAD_F3:		WriteCSISequence(m,'R'); break;
		case EXTENDED_KEY_PAD_F4:		WriteCSISequence(m,'S'); break;
		case EXTENDED_KEY_PAD_F5:		WriteCSISequence(m,'T'); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_FIND:			WriteFunctionKeyDECVT1(1U,m); break;
		case EXTENDED_KEY_SELECT:		WriteFunctionKeyDECVT1(4U,m); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_PAGE_DOWN:		WriteFunctionKeyDECVT1(6U,m); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_PAGE_UP:		WriteFunctionKeyDECVT1(6U,m); break;
		case EXTENDED_KEY_PAD_HOME:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_HOME:			WriteFunctionKeyDECVT1(7U,m); break;
		case EXTENDED_KEY_PAD_END:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_END:			WriteFunctionKeyDECVT1(8U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteRawCharacters("\x0D"); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteRawCharacters("\x0A"); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteRawCharacters(","); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteRawCharacters("/"); break;
		case EXTENDED_KEY_PAD_ASTERISK:		WriteRawCharacters("*"); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteRawCharacters("-"); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteRawCharacters("+"); break;
		case EXTENDED_KEY_PAD_EQUALS:		WriteRawCharacters("="); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	WriteRawCharacters("="); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DELETE:		// this, which the NetBSD console does not distinguish from:
		case EXTENDED_KEY_DELETE:		WriteRawCharacters("\x7F"); break;
		default:
			std::fprintf(stderr, "WARNING: %s: %" PRIx32 "\n", "Unknown extended key", k);
			break;
	}
}

void 
InputFIFO::WriteExtendedKey(uint16_t k, uint8_t m)
{
	SetPasting(false);
	switch (emulation) {
		case SCO_CONSOLE:	return WriteExtendedKeySCOConsole(k, m);
		case LINUX_CONSOLE:	return WriteExtendedKeyLinuxConsole(k, m);
		case XTERM_PC:		return WriteExtendedKeyXTermPCMode(k, m);
		case NETBSD_CONSOLE:	return WriteExtendedKeyNetBSDConsole(k, m);
		case DECVT:
		default:		return WriteExtendedKeyDECVT(k, m);
	}
}

void 
InputFIFO::WriteWheelMotion(uint8_t w, int8_t o, uint8_t m) 
{
	SetPasting(false);
	// The horizontal wheel (#1) is an extension to the xterm protocol.
	while (0 != o) {
		if (0 > o) {
			++o;
			const int button(4 + 2 * w);
			mouse_buttons[button] = true;
			WriteXTermMouse(button, m);
			WriteDECLocatorReport(button);
			mouse_buttons[button] = false;
#if 0	// vim cannot cope with button up wheel events.
			WriteXTermMouse(button, m);
#endif
			WriteDECLocatorReport(button);
		}
		if (0 < o) {
			--o;
			const int button(3 + 2 * w);
			mouse_buttons[button] = true;
			WriteXTermMouse(button, m);
			WriteDECLocatorReport(button);
			mouse_buttons[button] = false;
#if 0	// vim cannot cope with button up wheel events.
			WriteXTermMouse(button, m);
#endif
			WriteDECLocatorReport(button);
		}
	}
}

void 
InputFIFO::WriteXTermMouse(
	int button, 
	uint8_t modifiers
) {
	if (!send_xterm_mouse) return;

	bool pressed(false);
	if (button < 0) {
		for (std::size_t j(0U); j < sizeof mouse_buttons/sizeof *mouse_buttons; ++j)
			if (mouse_buttons[j]) {
				pressed = true;
				break;
			}
		if (pressed ? !send_xterm_mouse_button_motions : !send_xterm_mouse_nobutton_motions) return;
	} else {
		if (!send_xterm_mouse_clicks) return;
		pressed = mouse_buttons[button];
	}

	unsigned flags(0);

	if (button < 0)
		flags |= 32U;
	else
	if (button < 3)
		flags |= button;
	else
	if (button < 6)
		flags |= (button - 3) | 64U;

	if (INPUT_MODIFIER_LEVEL2 & modifiers)
		flags |= 4U;
	if (INPUT_MODIFIER_CONTROL & modifiers)
		flags |= 16U;
	if (INPUT_MODIFIER_SUPER & modifiers)
		flags |= 8U;

	WriteCSI();
	char b[32];
	const char c(pressed ? 'M' : 'm');
	snprintf(b, sizeof b, "<%u;%u;%u%c", flags, mouse_column + 1U, mouse_row + 1U, c);
	WriteRawCharacters(b);
}

void 
InputFIFO::WriteDECLocatorReport(int button) 
{
	if (!send_locator_mode) return;
	if (0 <= button) {
		if (static_cast<unsigned int>(button) >= sizeof mouse_buttons/sizeof *mouse_buttons) return;
		if (mouse_buttons[button] ? !send_locator_press_events : !send_locator_release_events) return;
	}

	unsigned event(0U);
	if (button < 0)
		event = 1U;
	else if (button < 4)
		event = button * 2U + 2U + (mouse_buttons[button] ? 0U : 1U);
	else
		// This is an extension to the DEC protocol.
		event = (button - 4) * 2U + 12U + (mouse_buttons[button] ? 0U : 1U);
	unsigned buttons(0U);
	for (std::size_t j(0U); j < sizeof mouse_buttons/sizeof *mouse_buttons; ++j)
		if (mouse_buttons[j])
			buttons |= 1U << j;

	WriteCSI();
	char b[32];
	const unsigned int mouse_page(0U);
	snprintf(b, sizeof b, "%u;%u;%u;%u;%u&w", event, buttons, mouse_row + 1U, mouse_column + 1U, mouse_page);
	WriteRawCharacters(b);

	// Turn oneshot mode off.
	// Invalid buttons and suppressed reports don't turn oneshot mode off, because oneshot mode is from the point of view of the client.
	if (2U == send_locator_mode) send_locator_mode = 0U;
}

void 
InputFIFO::SetMouseX(uint16_t p, uint8_t m) 
{
	SetPasting(false);
	if (mouse_column != p) {
		mouse_column = p; 
		WriteXTermMouse(-1, m);
		// DEC Locator reports only report button events.
	}
}

void 
InputFIFO::SetMouseY(uint16_t p, uint8_t m) 
{ 
	SetPasting(false);
	if (mouse_row != p) {
		mouse_row = p; 
		WriteXTermMouse(-1, m);
		// DEC Locator reports only report button events.
	}
}

void 
InputFIFO::SetMouseButton(uint8_t b, bool v, uint8_t m) 
{ 
	SetPasting(false);
	if (mouse_buttons[b] != v) {
		mouse_buttons[b] = v; 
		WriteXTermMouse(b, m);
		WriteDECLocatorReport(b);
	}
}

void 
InputFIFO::RequestDECLocatorReport()
{
	SetPasting(false);
	if (0U == send_locator_mode) {
		WriteCSI();
		WriteRawCharacters("0&w");
		return;
	}
	WriteDECLocatorReport(-1);
}

void 
InputFIFO::WriteUCS3Character(uint32_t c, bool pasted)
{
	SetPasting(pasted);
	WriteUnicodeCharacter(c);
	// Interrupt after any pasted character that could otherwise begin a DECFNK sequence.
	if (0x1B == c || 0x9B == c)
		SetPasting(false);
}

void
InputFIFO::ReadInput()
{
	const int l(read(fd, input_buffer + input_read, sizeof input_buffer - input_read));
	if (l > 0)
		input_read += l;
	while (input_read >= 4U) {
		uint32_t b;
		std::memcpy(&b, input_buffer, 4U);
		input_read -= 4U;
		std::memmove(input_buffer, input_buffer + 4U, input_read);
		switch (b & INPUT_MSG_MASK) {
			case INPUT_MSG_UCS3:	WriteUCS3Character(b & ~INPUT_MSG_MASK, false); break;
			case INPUT_MSG_PUCS3:	WriteUCS3Character(b & ~INPUT_MSG_MASK, true); break;
			case INPUT_MSG_EKEY:	WriteExtendedKey((b >> 8U) & 0xFFFF, b & 0xFF); break;
			case INPUT_MSG_FKEY:	WriteFunctionKey((b >> 8U) & 0xFF, b & 0xFF); break;
			case INPUT_MSG_XPOS:	SetMouseX((b >> 8U) & 0xFFFF, b & 0xFF); break;
			case INPUT_MSG_YPOS:	SetMouseY((b >> 8U) & 0xFFFF, b & 0xFF); break;
			case INPUT_MSG_WHEEL:	WriteWheelMotion((b >> 16U) & 0xFF, static_cast<int8_t>((b >> 8U) & 0xFF), b & 0xFF); break;
			case INPUT_MSG_BUTTON:	SetMouseButton((b >> 16U) & 0xFF, (b >> 8U) & 0xFF, b & 0xFF); break;
			default:
				std::fprintf(stderr, "WARNING: %s: %" PRIx32 "\n", "Unknown input message", b);
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

namespace {
class MultipleBuffer : 
	public SoftTerm::ScreenBuffer 
{
public:
	MultipleBuffer();
	~MultipleBuffer();
	void Add(SoftTerm::ScreenBuffer * v) { buffers.push_back(v); }
	virtual void WriteNCells(coordinate p, coordinate n, const CharacterCell & c);
	virtual void CopyNCells(coordinate d, coordinate s, coordinate n);
	virtual void ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void SetCursorPos(coordinate x, coordinate y);
	virtual void SetCursorType(CursorSprite::glyph_type, CursorSprite::attribute_type);
	virtual void SetPointerType(PointerSprite::attribute_type);
	virtual void SetSize(coordinate w, coordinate h);
protected:
	typedef std::list<SoftTerm::ScreenBuffer *> Buffers;
	Buffers buffers;
};
}

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
MultipleBuffer::CopyNCells(coordinate d, coordinate s, coordinate n)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->CopyNCells(d, s, n);
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
MultipleBuffer::SetPointerType(PointerSprite::attribute_type a)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->SetPointerType(a);
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

namespace {
class UnicodeSoftTerm : 
	public UTF8Decoder::UCS32CharacterSink,
	public SoftTerm
{
public:
	UnicodeSoftTerm(ScreenBuffer & s, KeyboardBuffer & k, MouseBuffer & m) : SoftTerm(s, k, m) {}
	virtual void Write(uint32_t character, bool decoder_error, bool overlong);
};
}

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

/* Emulation options ********************************************************
// **************************************************************************
*/

struct emulation_definition : public popt::simple_named_definition {
public:
	emulation_definition(char s, const char * l, const char * d, InputFIFO::Emulation & e, InputFIFO::Emulation v) : simple_named_definition(s, l, d), emulation(e), value(v) {}
	virtual void action(popt::processor &);
	virtual ~emulation_definition();
protected:
	InputFIFO::Emulation & emulation;
	InputFIFO::Emulation value;
};
emulation_definition::~emulation_definition() {}
void emulation_definition::action(popt::processor &)
{
	emulation = value;
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_terminal_emulator [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
#if defined(__LINUX__) || defined(__linux__)
	InputFIFO::Emulation emulation(InputFIFO::LINUX_CONSOLE);
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	InputFIFO::Emulation emulation(InputFIFO::XTERM_PC);
#elif defined(__NetBSD__) || defined(__OpenBSD__)
	InputFIFO::Emulation emulation(InputFIFO::NETBSD_CONSOLE);
#else
	InputFIFO::Emulation emulation(InputFIFO::DECVT);
#endif
	bool vcsa(false);

	try {
		emulation_definition linux_option('\0', "linux", "Emulate the Linux virtual console.", emulation, InputFIFO::LINUX_CONSOLE);
		emulation_definition sco_option('\0', "sco", "Emulate the SCO virtual console.", emulation, InputFIFO::SCO_CONSOLE);
		emulation_definition freebsd_option('\0', "freebsd", "Emulate the FreeBSD virtual console.", emulation, InputFIFO::XTERM_PC);
		emulation_definition netbsd_option('\0', "netbsd", "Emulate the NetBSD virtual console.", emulation, InputFIFO::NETBSD_CONSOLE);
		emulation_definition decvt_option('\0', "decvt", "Emulate the DEC VT.", emulation, InputFIFO::DECVT);
		popt::bool_definition vcsa_option('\0', "vcsa", "Maintain a vcsa-compatible display buffer.", vcsa);
		popt::definition * top_table[] = {
			&linux_option,
			&sco_option,
			&freebsd_option,
			&netbsd_option,
			&decvt_option,
			&vcsa_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "directory");

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
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * dirname(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	const char * tty(envs.query("TTY"));
	if (!tty) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "TTY", "Missing environment variable.");
		throw EXIT_FAILURE;
	}

	FileDescriptorOwner dir_fd(open_dir_at(AT_FDCWD, dirname));
	if (0 > dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/: %s\n", prog, dirname, std::strerror(error));
		throw EXIT_FAILURE;
	}

	// We need an explicit lock file, because we cannot lock FIFOs.
	FileDescriptorOwner lock_fd(open_lockfile_at(dir_fd.get(), "lock"));
	if (0 > lock_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "lock", std::strerror(error));
		throw EXIT_FAILURE;
	}
	// We are allowed to open the read end of a FIFO in non-blocking mode without having to wait for a writer.
	mkfifoat(dir_fd.get(), "input", 0620);
	InputFIFO input(open_read_at(dir_fd.get(), "input"), PTY_MASTER_FILENO, emulation);
	if (0 > input.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (0 > fchown(input.get(), -1, getegid())) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	// We have to keep a client (write) end descriptor open to the input FIFO.
	// Otherwise, the first console client process triggers POLLHUP when it closes its end.
	// Opening the FIFO for read+write isn't standard, although it would work on Linux.
	FileDescriptorOwner input_write_fd(open_writeexisting_at(dir_fd.get(), "input"));
	if (0 > input_write_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	VCSA vbuffer(open_readwritecreate_at(dir_fd.get(), "vcsa", 0640));
	if (0 > vbuffer.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "vcsa", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (0 > fchown(vbuffer.get(), -1, getegid())) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "vcsa", std::strerror(error));
		throw EXIT_FAILURE;
	}
	UnicodeBuffer ubuffer(open_readwritecreate_at(dir_fd.get(), "display", 0640));
	if (0 > ubuffer.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (0 > fchown(ubuffer.get(), -1, getegid())) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	unlinkat(dir_fd.get(), "tty", 0);
	if (0 > linkat(AT_FDCWD, tty, dir_fd.get(), "tty", 0)) {
		if (0 > symlinkat(tty, dir_fd.get(), "tty")) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s -> %s/tty: %s\n", prog, tty, dirname, std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, 0);

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct kevent p[16];
	{
		size_t index(0);
		set_event(&p[index++], PTY_MASTER_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
		set_event(&p[index++], PTY_MASTER_FILENO, EVFILT_WRITE, EV_ADD, 0, 0, 0);
		set_event(&p[index++], input.get(), EVFILT_READ, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		if (0 > kevent(queue, p, index, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	ubuffer.WriteBOM();
	MultipleBuffer mbuffer;
	mbuffer.Add(&ubuffer);
	if (vcsa)
		mbuffer.Add(&vbuffer);
	UnicodeSoftTerm emulator(mbuffer, input, input);
	UTF8Decoder decoder(emulator);

	bool hangup(false);
	while (!shutdown_signalled && !hangup) {
		EV_SET(&p[0], PTY_MASTER_FILENO, EVFILT_WRITE, input.OutputAvailable() ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		EV_SET(&p[1], input.get(), EVFILT_READ, input.HasInputSpace() ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		const int rc(kevent(queue, p, 1, p, sizeof p/sizeof *p, 0));

		if (0 > rc) {
			if (EINTR == errno) continue;
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}

		bool masterin_ready(false), masterout_ready(false), master_hangup(false), fifo_ready(false), fifo_hangup(false);

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			if (EVFILT_SIGNAL == e.filter)
				handle_signal(e.ident);
			if (EVFILT_READ == e.filter && PTY_MASTER_FILENO == e.ident) {
				masterin_ready = true;
				master_hangup |= EV_EOF & e.flags;
			}
			if (EVFILT_READ == e.filter && input.get() == static_cast<int>(e.ident)) {
				fifo_ready = true;
				fifo_hangup |= EV_EOF & e.flags;
			}
			if (EVFILT_WRITE == e.filter && PTY_MASTER_FILENO == e.ident)
				masterout_ready = true;
		}

		if (masterin_ready) {
			char b[16384];
			const int l(read(PTY_MASTER_FILENO, b, sizeof b));
			if (l > 0) {
				for (int i(0); i < l; ++i)
					decoder.SendUTF8(b[i]);
				master_hangup = false;
			}
		}
		if (fifo_ready)
			input.ReadInput();
		if (master_hangup)
			hangup = true;
		if (masterout_ready) 
			input.WriteOutput();
	}

	unlinkat(dir_fd.get(), "tty", 0);
	throw EXIT_SUCCESS;
}
