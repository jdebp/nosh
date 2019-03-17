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
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "kqueue_common.h"
#include "popt.h"
#include "fdutils.h"
#include "ttyutils.h"
#include "utils.h"
#include "ProcessEnvironment.h"
#include "SoftTerm.h"
#include "InputMessage.h"
#include "FileDescriptorOwner.h"
#include "SignalManagement.h"
#include "InputFIFO.h"

enum { PTY_MASTER_FILENO = 4 };

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
	virtual void SetSize(const coordinate & w, const coordinate & h);
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
VCSA::SetSize(const coordinate & w, const coordinate & h)
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
	virtual void SetSize(const coordinate & w, const coordinate & h);
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
	c[12] = (cell.attributes >> 0) & 0xFF;
	c[13] = (cell.attributes >> 8) & 0xFF;
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
UnicodeBuffer::SetSize(const coordinate & w, const coordinate & h)
{
	const uint16_t b[2] = { static_cast<uint16_t>(w), static_cast<uint16_t>(h) };
	pwrite(fd, b, sizeof b, 4U);
	ftruncate(fd, MakeOffset(w * h));
}

/* input side ***************************************************************
// **************************************************************************
*/

namespace {
class ECMA48InputEncoder :
	public SoftTerm::KeyboardBuffer,
	public SoftTerm::MouseBuffer
{
public:
	enum Emulation { DECVT, SCO_CONSOLE, LINUX_CONSOLE, NETBSD_CONSOLE, TEKEN, XTERM_PC };
	ECMA48InputEncoder(int, Emulation);
	void HandleMessage(uint32_t);
	void WriteOutput();
	bool OutputAvailable() { return output_pending > 0U; }
	bool HasInputSpace() { return output_pending + 128U < sizeof output_buffer; }
protected:
	const int master_fd;
	virtual void WriteLatin1Characters(std::size_t, const char *);
	virtual void WriteControl1Character(uint8_t);
	virtual void Set8BitControl1(bool);
	virtual void SetBackspaceIsBS(bool);
	virtual void SetDeleteIsDEL(bool);
	virtual void SetCursorApplicationMode(bool);
	virtual void SetCalculatorApplicationMode(bool);
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
	void WriteDELOrDECFNK(uint8_t m);
	void WriteBackspaceOrDEL(uint8_t m);
	void WriteReturnEnter(uint8_t m);
	void WriteCSISequence(unsigned m, char c);
	void WriteSS3Character(char c);
	void WriteBrokenSS3Sequence(unsigned m, char c);
	void SetPasting(bool p);
	void WriteUCS3Character(uint32_t c, bool p, bool a);
	void WriteDECFNK(unsigned n, unsigned m);
	void WriteLinuxConsoleFNK(unsigned m, char c);
	void WriteSCOConsoleFNK(char c);
	void WriteFunctionKeyDECVT(uint16_t k, uint8_t m);
	void WriteFunctionKeySCOConsole(uint16_t k, uint8_t m);
	void WriteFunctionKeyTeken(uint16_t k, uint8_t m);
	void WriteFunctionKey(uint16_t k, uint8_t m);
	void WriteDECVTNumericKeypadKey(char app_char, unsigned decfnk, unsigned m);
	void WriteDECVTNumericKeypadKey(char app_char, char ord_char);
	void WriteDECVTNumericKeypadKey(char app_char, char csi_char, unsigned m);
	void WriteDECVTNumericKeypadKey(char app_char, char csi_char, unsigned decfnk, unsigned m);
	void WriteDECVTCursorKeypadKey(char c, unsigned decfnk, unsigned m);
	void WriteXTermPCNumericKeypadKey(char app_char, unsigned decfnk, unsigned m);
	void WriteXTermPCNumericKeypadKey(char app_char, char ord_char);
	void WriteXTermPCNumericKeypadKey(char app_char, char csi_char, unsigned m);
	void WriteXTermPCCursorKeypadKey(char c, unsigned m);
	void WriteExtendedKeyCommonExtensions(uint16_t k, uint8_t m);
	void WriteExtendedKeyDECVT(uint16_t k, uint8_t m);
	void WriteExtendedKeySCOConsole(uint16_t k, uint8_t m);
	void WriteExtendedKeyLinuxConsole(uint16_t k, uint8_t m);
	void WriteExtendedKeyNetBSDConsole(uint16_t k, uint8_t m);
	void WriteExtendedKeyXTermPC(uint16_t k, uint8_t m);
	void WriteExtendedKeyTeken(uint16_t k, uint8_t m);
	void WriteExtendedKey(uint16_t k, uint8_t m);
	void SetMouseX(uint16_t p, uint8_t m);
	void SetMouseY(uint16_t p, uint8_t m);
	void SetMouseButton(uint8_t b, bool v, uint8_t m);
	void WriteWheelMotion(uint8_t b, int8_t o, uint8_t m);
	void WriteXTermMouse(int button, uint8_t modifiers);
	void WriteDECLocatorReport(int button);
	const Emulation emulation;
	bool send_8bit_controls, backspace_is_bs, delete_is_del;
	bool cursor_application_mode, calculator_application_mode;
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

inline
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
}

ECMA48InputEncoder::ECMA48InputEncoder(int m, Emulation e) : 
	master_fd(m),
	emulation(e),
	send_8bit_controls(false),
	backspace_is_bs(false),
	delete_is_del(false),
	cursor_application_mode(false),
	calculator_application_mode(false),
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
ECMA48InputEncoder::WriteRawCharacters(std::size_t l, const char * p)
{
	if (l > (sizeof output_buffer - output_pending))
		l = sizeof output_buffer - output_pending;
	std::memmove(output_buffer + output_pending, p, l);
	output_pending += l;
}

inline 
void 
ECMA48InputEncoder::WriteRawCharacters(const char * s) 
{ 
	WriteRawCharacters(std::strlen(s), s); 
}

void 
ECMA48InputEncoder::ReportSize(coordinate w, coordinate h)
{
	winsize size = { 0, 0, 0, 0 };
	size.ws_col = w;
	size.ws_row = h;
	tcsetwinsz_nointr(master_fd, size);
}

void 
ECMA48InputEncoder::WriteLatin1Characters(std::size_t l, const char * p)
{
	if (IsAll7Bit(l, p)) 
		return WriteRawCharacters(l, p);
	else while (l) {
		WriteUnicodeCharacter(static_cast<unsigned char>(*p++));
		--l;
	}
}

void 
ECMA48InputEncoder::WriteUnicodeCharacter(uint32_t c)
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
ECMA48InputEncoder::WriteControl1Character(uint8_t c)
{
	if (send_8bit_controls)
		WriteUnicodeCharacter(c);
	else {
		WriteUnicodeCharacter(ESC);
		WriteUnicodeCharacter(c - 0x40);
	}
}

void 
ECMA48InputEncoder::Set8BitControl1(bool b)
{
	send_8bit_controls = b;
}

void 
ECMA48InputEncoder::SetBackspaceIsBS(bool b)
{
	backspace_is_bs = b;
}

void 
ECMA48InputEncoder::SetDeleteIsDEL(bool b)
{
	delete_is_del = b;
}

void
ECMA48InputEncoder::SetCursorApplicationMode(bool b)
{
	cursor_application_mode = b;
}

void
ECMA48InputEncoder::SetCalculatorApplicationMode(bool b)
{
	calculator_application_mode = b;
}

void 
ECMA48InputEncoder::SetSendPasteEvent(bool b)
{
	send_paste = b;
}

void 
ECMA48InputEncoder::SetSendXTermMouse(bool b)
{
	send_xterm_mouse = b;
}

void 
ECMA48InputEncoder::SetSendXTermMouseClicks(bool b)
{
	send_xterm_mouse_clicks = b;
}

void 
ECMA48InputEncoder::SetSendXTermMouseButtonMotions(bool b)
{
	send_xterm_mouse_button_motions = b;
}

void 
ECMA48InputEncoder::SetSendXTermMouseNoButtonMotions(bool b)
{
	send_xterm_mouse_nobutton_motions = b;
}

void 
ECMA48InputEncoder::SetSendDECLocator(unsigned int mode)
{
	send_locator_mode = mode;
}

void 
ECMA48InputEncoder::SetSendDECLocatorPressEvent(bool b)
{
	send_locator_press_events = b;
}

void 
ECMA48InputEncoder::SetSendDECLocatorReleaseEvent(bool b)
{
	send_locator_release_events = b;
}

void
ECMA48InputEncoder::WriteDELOrDECFNK(uint8_t m)
{
	if (delete_is_del && 0 == m)
		WriteRawCharacters("\x7F"); 	// We can bypass UTF-8 encoding as we guarantee ASCII.
	else
		WriteDECFNK(3U,m);
}

void
ECMA48InputEncoder::WriteBackspaceOrDEL(uint8_t m)
{
	WriteRawCharacters(backspace_is_bs ^ !!(INPUT_MODIFIER_CONTROL & m) ? "\x08" : "\x7F"); 	// We can bypass UTF-8 encoding as we guarantee ASCII.
}

void
ECMA48InputEncoder::WriteReturnEnter(uint8_t m)
{
	WriteRawCharacters((INPUT_MODIFIER_CONTROL & m) ? "\x0A" : "\x0D"); 	// We can bypass UTF-8 encoding as we guarantee ASCII.
}

inline 
void 
ECMA48InputEncoder::WriteCSI() 
{ 
	WriteControl1Character(CSI); 
}

inline 
void 
ECMA48InputEncoder::WriteSS3() 
{ 
	WriteControl1Character(SS3); 
}

void 
ECMA48InputEncoder::WriteCSISequence(unsigned m, char c)
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
ECMA48InputEncoder::WriteSS3Character(char c)
{
	WriteSS3();
	WriteUnicodeCharacter(c);
}

/// \brief Write malformed SS3 sequences.
void 
ECMA48InputEncoder::WriteBrokenSS3Sequence(unsigned m, char c)
{
	WriteSS3();
	if (0 != m) {
		char b[16];
		const int n(snprintf(b, sizeof b, "%u%c", m + 1U, c));
		WriteRawCharacters(n, b);	// We can bypass UTF-8 encoding as we guarantee ASCII.
	} else
		WriteUnicodeCharacter(c);
}

void 
ECMA48InputEncoder::WriteDECFNK(unsigned n, unsigned m)
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
ECMA48InputEncoder::WriteLinuxConsoleFNK(unsigned m, char c)
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
ECMA48InputEncoder::WriteSCOConsoleFNK(char c)
{
	WriteCSI();
	WriteUnicodeCharacter(c);
}

void 
ECMA48InputEncoder::SetPasting(const bool p)
{
	if (p == pasting) return;
	pasting = p;
	if (send_paste)
		WriteDECFNK(pasting ? 200 : 201, 0);
}

void 
ECMA48InputEncoder::WriteFunctionKeyDECVT(uint16_t k, uint8_t m)
{
	switch (k) {
		case 1:		WriteDECFNK(11U,m); break;
		case 2:		WriteDECFNK(12U,m); break;
		case 3:		WriteDECFNK(13U,m); break;
		case 4:		WriteDECFNK(14U,m); break;
		case 5:		WriteDECFNK(15U,m); break;
		case 6:		WriteDECFNK(17U,m); break;
		case 7:		WriteDECFNK(18U,m); break;
		case 8:		WriteDECFNK(19U,m); break;
		case 9:		WriteDECFNK(20U,m); break;
		case 10:	WriteDECFNK(21U,m); break;
		case 11:	WriteDECFNK(23U,m); break;
		case 12:	WriteDECFNK(24U,m); break;
		case 13:	WriteDECFNK(25U,m); break;
		case 14:	WriteDECFNK(26U,m); break;
		case 15:	WriteDECFNK(28U,m); break;
		case 16:	WriteDECFNK(29U,m); break;
		case 17:	WriteDECFNK(31U,m); break;
		case 18:	WriteDECFNK(32U,m); break;
		case 19:	WriteDECFNK(33U,m); break;
		case 20:	WriteDECFNK(34U,m); break;
		case 21:	WriteDECFNK(35U,m); break;
		case 22:	WriteDECFNK(36U,m); break;
		default:
			std::fprintf(stderr, "WARNING: Function key #%" PRId32 " does not have a DEC VT mapping.\n", k);
			break;
	}
}

void 
ECMA48InputEncoder::WriteFunctionKeySCOConsole(uint16_t k, uint8_t /*m*/)
{
	static const char other[9] = "@[\\]^_`{";
	if (15U > k)
		WriteSCOConsoleFNK(k - 1U + 'M');
	else
	if (41U > k)
		WriteSCOConsoleFNK(k - 15U + 'a');
	else
	if (49U > k)
		WriteSCOConsoleFNK(other[k - 41U]);
}

void 
ECMA48InputEncoder::WriteFunctionKeyTeken(uint16_t k, uint8_t m)
{
	if (13U > k)
		WriteFunctionKeyDECVT(k, m);
	else
		WriteFunctionKeySCOConsole(k, m);
}

void 
ECMA48InputEncoder::WriteFunctionKey(uint16_t k, uint8_t m)
{
	SetPasting(false);
	switch (emulation) {
		default:		[[clang::fallthrough]];
		case XTERM_PC:		[[clang::fallthrough]];
		case LINUX_CONSOLE:	[[clang::fallthrough]];
		case NETBSD_CONSOLE:	[[clang::fallthrough]];
		case DECVT:		return WriteFunctionKeyDECVT(k, m);
		case TEKEN:		return WriteFunctionKeyTeken(k, m);
		case SCO_CONSOLE:	return WriteFunctionKeySCOConsole(k, m);
	}
}

void 
ECMA48InputEncoder::WriteDECVTNumericKeypadKey(char app_char, unsigned decfnk, unsigned m)
{
	if (calculator_application_mode)
		WriteSS3Character(app_char);
	else
		WriteDECFNK(decfnk, m);
}

void 
ECMA48InputEncoder::WriteDECVTNumericKeypadKey(char app_char, char ord_char)
{
	if (calculator_application_mode)
		WriteSS3Character(app_char);
	else
		WriteUnicodeCharacter(ord_char);
}

void 
ECMA48InputEncoder::WriteDECVTNumericKeypadKey(char app_char, char csi_char, unsigned m)
{
	if (calculator_application_mode)
		WriteSS3Character(app_char);
	else
		WriteCSISequence(m, csi_char);
}

void 
ECMA48InputEncoder::WriteDECVTNumericKeypadKey(char app_char, char csi_char, unsigned decfnk, unsigned m)
{
	if (calculator_application_mode)
		WriteSS3Character(app_char);
	else
	if (INPUT_MODIFIER_LEVEL3 & m)
		WriteDECFNK(decfnk, m);
	else
		WriteCSISequence(m, csi_char);
}

void 
ECMA48InputEncoder::WriteDECVTCursorKeypadKey(char c, unsigned decfnk, unsigned m)
{
	if (cursor_application_mode)
		WriteSS3Character(c);
	else
	if (INPUT_MODIFIER_LEVEL3 & m)
		WriteDECFNK(decfnk, m);
	else
		WriteCSISequence(m, c);
}

void 
ECMA48InputEncoder::WriteXTermPCNumericKeypadKey(char app_char, unsigned decfnk, unsigned m)
{
	if (calculator_application_mode && (INPUT_MODIFIER_LEVEL2 & m))
		WriteBrokenSS3Sequence(m, app_char);
	else
		WriteDECFNK(decfnk, m);
}

void 
ECMA48InputEncoder::WriteXTermPCNumericKeypadKey(char app_char, char ord_char)
{
	if (calculator_application_mode)
		WriteSS3Character(app_char);
	else
		WriteUnicodeCharacter(ord_char);
}

void 
ECMA48InputEncoder::WriteXTermPCNumericKeypadKey(char app_char, char csi_char, unsigned m)
{
	if (calculator_application_mode && (INPUT_MODIFIER_LEVEL2 & m))
		WriteBrokenSS3Sequence(m, app_char);
	else
		WriteCSISequence(m, csi_char);
}

void 
ECMA48InputEncoder::WriteXTermPCCursorKeypadKey(char c, unsigned m)
{
	if (cursor_application_mode && 0 == m)
		WriteSS3Character(c);
	else
		WriteCSISequence(m, c);
}

void 
ECMA48InputEncoder::WriteExtendedKeyCommonExtensions(uint16_t k, uint8_t /*m*/)
{
	switch (k) {
 		case EXTENDED_KEY_PAD_00:		WriteRawCharacters("00"); break;
 		case EXTENDED_KEY_PAD_000:		WriteRawCharacters("000"); break;
 		case EXTENDED_KEY_PAD_THOUSANDS_SEP:	WriteRawCharacters(","); break;
 		case EXTENDED_KEY_PAD_DECIMAL_SEP:	WriteRawCharacters("."); break;
 		case EXTENDED_KEY_PAD_CURRENCY_UNIT:	WriteUnicodeCharacter(0x00A4); break;
 		case EXTENDED_KEY_PAD_CURRENCY_SUB:	WriteUnicodeCharacter(0x00A2); break;
 		case EXTENDED_KEY_PAD_OPEN_BRACKET:	WriteRawCharacters("["); break;
 		case EXTENDED_KEY_PAD_CLOSE_BRACKET:	WriteRawCharacters("]"); break;
 		case EXTENDED_KEY_PAD_OPEN_BRACE:	WriteRawCharacters("{"); break;
 		case EXTENDED_KEY_PAD_CLOSE_BRACE:	WriteRawCharacters("}"); break;
 		case EXTENDED_KEY_PAD_A:		WriteRawCharacters("A"); break;
 		case EXTENDED_KEY_PAD_B:		WriteRawCharacters("B"); break;
 		case EXTENDED_KEY_PAD_C:		WriteRawCharacters("C"); break;
 		case EXTENDED_KEY_PAD_D:		WriteRawCharacters("D"); break;
 		case EXTENDED_KEY_PAD_E:		WriteRawCharacters("E"); break;
 		case EXTENDED_KEY_PAD_F:		WriteRawCharacters("F"); break;
 		case EXTENDED_KEY_PAD_XOR:		WriteUnicodeCharacter(0x22BB); break;
 		case EXTENDED_KEY_PAD_CARET:		WriteRawCharacters("^"); break;
 		case EXTENDED_KEY_PAD_PERCENT:		WriteRawCharacters("%"); break;
 		case EXTENDED_KEY_PAD_LESS:		WriteRawCharacters("<"); break;
 		case EXTENDED_KEY_PAD_GREATER:		WriteRawCharacters(">"); break;
 		case EXTENDED_KEY_PAD_AND:		WriteUnicodeCharacter(0x2227); break;
 		case EXTENDED_KEY_PAD_ANDAND:		WriteRawCharacters("&&"); break;
 		case EXTENDED_KEY_PAD_OR:		WriteUnicodeCharacter(0x2228); break;
 		case EXTENDED_KEY_PAD_OROR:		WriteRawCharacters("||"); break;
		case EXTENDED_KEY_PAD_COLON:		WriteRawCharacters(":"); break;
 		case EXTENDED_KEY_PAD_HASH:		WriteRawCharacters("#"); break;
 		case EXTENDED_KEY_PAD_SPACE:		WriteRawCharacters(" "); break;
 		case EXTENDED_KEY_PAD_AT:		WriteRawCharacters("@"); break;
 		case EXTENDED_KEY_PAD_EXCLAMATION:	WriteRawCharacters("!"); break;
 		case EXTENDED_KEY_PAD_SIGN:		WriteUnicodeCharacter(0x00B1); break;
		default:
			std::fprintf(stderr, "WARNING: %s: %08" PRIx32 "\n", "Unknown extended key", k);
			break;
	}
}

// These are the sequences defined by the DEC VT510 and VT520 programmers' references.
// Most termcaps/terminfos name this "vt220", or "vt420", or "vt520".
//
// * There is no way to transmit modifier state with "application mode" keys.
void 
ECMA48InputEncoder::WriteExtendedKeyDECVT(uint16_t k, uint8_t m)
{
	switch (k) {
	// The calculator keypad
		case EXTENDED_KEY_PAD_ASTERISK:		WriteDECVTNumericKeypadKey('j',    '*'     ); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteDECVTNumericKeypadKey('k',    '+'     ); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteDECVTNumericKeypadKey('l',    ','     ); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteDECVTNumericKeypadKey('m',    '-'     ); break;
		case EXTENDED_KEY_PAD_DELETE:		WriteDECVTNumericKeypadKey('n',        3U,m); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteDECVTNumericKeypadKey('o',    '/'     ); break;
		case EXTENDED_KEY_PAD_INSERT:		WriteDECVTNumericKeypadKey('p',        2U,m); break;
		case EXTENDED_KEY_PAD_END:		WriteDECVTNumericKeypadKey('q','F',       m); break;
		case EXTENDED_KEY_PAD_DOWN:		WriteDECVTNumericKeypadKey('r','B',    8U,m); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	WriteDECVTNumericKeypadKey('s',        6U,m); break;
		case EXTENDED_KEY_PAD_LEFT:		WriteDECVTNumericKeypadKey('t','D',    7U,m); break;
		case EXTENDED_KEY_PAD_CENTRE:		WriteDECVTNumericKeypadKey('u','E',       m); break;
		case EXTENDED_KEY_PAD_RIGHT:		WriteDECVTNumericKeypadKey('v','C',   10U,m); break;
		case EXTENDED_KEY_PAD_HOME:		WriteDECVTNumericKeypadKey('w','H',       m); break;
		case EXTENDED_KEY_PAD_UP:		WriteDECVTNumericKeypadKey('x','A',    9U,m); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		WriteDECVTNumericKeypadKey('y',        5U,m); break;
		case EXTENDED_KEY_PAD_TAB:		WriteCSISequence(m,'I'); break;
		case EXTENDED_KEY_PAD_ENTER:		if (calculator_application_mode) WriteSS3Character('M'); else WriteReturnEnter(m); break;
		case EXTENDED_KEY_PAD_F1:		WriteSS3Character('P'); break;
		case EXTENDED_KEY_PAD_F2:		WriteSS3Character('Q'); break;
		case EXTENDED_KEY_PAD_F3:		WriteSS3Character('R'); break;
		case EXTENDED_KEY_PAD_F4:		WriteSS3Character('S'); break;
		case EXTENDED_KEY_PAD_F5:		WriteSS3Character('T'); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_EQUALS:		WriteDECVTNumericKeypadKey('X',    '='     ); break;
	// The cursor/editing keypad
		case EXTENDED_KEY_SCROLL_UP:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_UP_ARROW:		WriteDECVTCursorKeypadKey('A',     9U,m); break;
		case EXTENDED_KEY_SCROLL_DOWN:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_DOWN_ARROW:		WriteDECVTCursorKeypadKey('B',     8U,m); break;
		case EXTENDED_KEY_RIGHT_ARROW:		WriteDECVTCursorKeypadKey('C',    10U,m); break;
		case EXTENDED_KEY_LEFT_ARROW:		WriteDECVTCursorKeypadKey('D',     7U,m); break;
		case EXTENDED_KEY_CENTRE:		WriteCSISequence(m,'E'); break;
		case EXTENDED_KEY_END:			WriteCSISequence(m,'F'); break;
		case EXTENDED_KEY_HOME:			WriteCSISequence(m,'H'); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_FIND:			WriteDECFNK(1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_INSERT:		WriteDECFNK(2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_DELETE:		WriteDELOrDECFNK(m); break;
		case EXTENDED_KEY_SELECT:		WriteDECFNK(4U,m); break;
		case EXTENDED_KEY_PREVIOUS:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_UP:		WriteDECFNK(5U,m); break;
		case EXTENDED_KEY_NEXT:			// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_DOWN:		WriteDECFNK(6U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(m); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteReturnEnter(m); break;
		default:				WriteExtendedKeyCommonExtensions(k,m); break;
	}
}

// These are what XTerm produces in its PC mode.
//
// Some important differences from a DEC VT with a PC keyboard and the PC Layout:
//  * XTerm reports modifiers in application mode but not in normal mode, resulting in faulty SS3 sequences; DEC VTPC does the opposite.
//  * In application mode, XTerm only distinguishes the calculator keypad keys from the cursor keypad keys if Level 2 shift is in effect; DEC VTPC always distinguishes.
//  * In application mode, XTerm reverts to normal mode for cursor and calculator keypad keys if Control or Level 3 shift (ALT) is in effect; DEC VTPC does not.
//  * In normal mode, XTerm does not switch to DECFNK sequences for the level 3 (actually ALT) modifier; DEC VTPC does.
void 
ECMA48InputEncoder::WriteExtendedKeyXTermPC(uint16_t k, uint8_t m)
{
	switch (k) {
	// The calculator keypad
		case EXTENDED_KEY_PAD_ASTERISK:		WriteXTermPCNumericKeypadKey('j',   '*'       ); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteXTermPCNumericKeypadKey('k',   '+'       ); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteXTermPCNumericKeypadKey('l',   ','       ); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteXTermPCNumericKeypadKey('m',   '-'       ); break;
		case EXTENDED_KEY_PAD_DELETE:		WriteXTermPCNumericKeypadKey('n',         3U,m); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteXTermPCNumericKeypadKey('o',   '/'       ); break;
		case EXTENDED_KEY_PAD_INSERT:		WriteXTermPCNumericKeypadKey('p',         2U,m); break;
		case EXTENDED_KEY_PAD_END:		WriteXTermPCNumericKeypadKey('q','F',        m); break;
		case EXTENDED_KEY_PAD_DOWN:		WriteXTermPCNumericKeypadKey('r','B',        m); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	WriteXTermPCNumericKeypadKey('s',         6U,m); break;
		case EXTENDED_KEY_PAD_LEFT:		WriteXTermPCNumericKeypadKey('t','D',        m); break;
		case EXTENDED_KEY_PAD_CENTRE:		WriteXTermPCNumericKeypadKey('u','E',        m); break;
		case EXTENDED_KEY_PAD_RIGHT:		WriteXTermPCNumericKeypadKey('v','C',        m); break;
		case EXTENDED_KEY_PAD_HOME:		WriteXTermPCNumericKeypadKey('w','H',        m); break;
		case EXTENDED_KEY_PAD_UP:		WriteXTermPCNumericKeypadKey('x','A',        m); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		WriteXTermPCNumericKeypadKey('y',         5U,m); break;
		case EXTENDED_KEY_PAD_TAB:		WriteXTermPCNumericKeypadKey('I','I',        m); break;
		case EXTENDED_KEY_PAD_ENTER:		if (calculator_application_mode) WriteXTermPCNumericKeypadKey('M','M',        m); else WriteReturnEnter(m); break;
		case EXTENDED_KEY_PAD_F1:		WriteXTermPCNumericKeypadKey('P','P',        m); break;
		case EXTENDED_KEY_PAD_F2:		WriteXTermPCNumericKeypadKey('Q','Q',        m); break;
		case EXTENDED_KEY_PAD_F3:		WriteXTermPCNumericKeypadKey('R','R',        m); break;
		case EXTENDED_KEY_PAD_F4:		WriteXTermPCNumericKeypadKey('S','S',        m); break;
		case EXTENDED_KEY_PAD_F5:		WriteXTermPCNumericKeypadKey('T','T',        m); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_EQUALS:		WriteXTermPCNumericKeypadKey('X',    '='      ); break;
	// The cursor/editing keypad
		case EXTENDED_KEY_SCROLL_UP:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_UP_ARROW:		WriteXTermPCCursorKeypadKey('A',m); break;
		case EXTENDED_KEY_SCROLL_DOWN:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_DOWN_ARROW:		WriteXTermPCCursorKeypadKey('B',m); break;
		case EXTENDED_KEY_RIGHT_ARROW:		WriteXTermPCCursorKeypadKey('C',m); break;
		case EXTENDED_KEY_LEFT_ARROW:		WriteXTermPCCursorKeypadKey('D',m); break;
		case EXTENDED_KEY_CENTRE:		WriteXTermPCCursorKeypadKey('E',m); break;
		case EXTENDED_KEY_END:			WriteXTermPCCursorKeypadKey('F',m); break;
		case EXTENDED_KEY_HOME:			WriteXTermPCCursorKeypadKey('H',m); break;
		case EXTENDED_KEY_BACKTAB:		WriteXTermPCCursorKeypadKey('Z',m); break;
		case EXTENDED_KEY_FIND:			WriteDECFNK(1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_INSERT:		WriteDECFNK(2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_DELETE:		WriteDELOrDECFNK(m); break;
		case EXTENDED_KEY_SELECT:		WriteDECFNK(4U,m); break;
		case EXTENDED_KEY_PREVIOUS:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_UP:		WriteDECFNK(5U,m); break;
		case EXTENDED_KEY_NEXT:			// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_DOWN:		WriteDECFNK(6U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(m); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteReturnEnter(m); break;
		default:				WriteExtendedKeyCommonExtensions(k,m); break;
	}
}

// These are the sequences defined by libteken, as used by the FreeBSD kernel since version 9.0.
//
// This emulation is fairly feature-poor.  Some important differences from DEC VT520 mode:
//  * There are no distinctions between application and normal modes.  Teken simply does not have this.
//  * There are no DECFNK sequences for the level 3 (actually ALT) modifier.
//  * There are no distinctions between cursor keypad keys and calculator keypad keys, except for Delete.
//  * Keypad plus/minus/enter/comma/slash/asterisk do not ever produce control sequences.
//  * Delete keys always send DEL.
// Some differences from vanilla teken:
//  * teken does not report any modifiers at all; we do.
//  * teken does not provide LF from return or enter with the control modifier; we do.
//  * teken does not have numeric keypad comma and equals; we are thus not bound by compatibility and give them DEC VT semantics.
//  * teken always issues DEL for calculator keypad Delete; we provide the DEC VT application mode as well.
void 
ECMA48InputEncoder::WriteExtendedKeyTeken(uint16_t k, uint8_t m)
{
	switch (k) {
		case EXTENDED_KEY_PAD_ASTERISK:		WriteRawCharacters("*"); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteRawCharacters("+"); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteDECVTNumericKeypadKey('l',    ','     ); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteRawCharacters("-"); break;
		case EXTENDED_KEY_PAD_DELETE:		WriteDECVTNumericKeypadKey('n',    DEL      ); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteRawCharacters("/"); break;
		case EXTENDED_KEY_SCROLL_UP:		// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_UP:		// this, which teken does not distinguish from:
		case EXTENDED_KEY_UP_ARROW:		WriteCSISequence(m,'A'); break;
		case EXTENDED_KEY_SCROLL_DOWN:		// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DOWN:		// this, which teken does not distinguish from:
		case EXTENDED_KEY_DOWN_ARROW:		WriteCSISequence(m,'B'); break;
		case EXTENDED_KEY_PAD_RIGHT:		// teken does not distinguish this from:
		case EXTENDED_KEY_RIGHT_ARROW:		WriteCSISequence(m,'C'); break;
		case EXTENDED_KEY_PAD_LEFT:		// teken does not distinguish this from:
		case EXTENDED_KEY_LEFT_ARROW:		WriteCSISequence(m,'D'); break;
		case EXTENDED_KEY_PAD_CENTRE:		// teken does not distinguish this from:
		case EXTENDED_KEY_CENTRE:		WriteCSISequence(m,'E'); break;
		case EXTENDED_KEY_PAD_END:		// teken does not distinguish this from:
		case EXTENDED_KEY_END:			WriteCSISequence(m,'F'); break;
		case EXTENDED_KEY_PAD_HOME:		// teken does not distinguish this from:
		case EXTENDED_KEY_HOME:			WriteCSISequence(m,'H'); break;
		case EXTENDED_KEY_PAD_TAB:		WriteCSISequence(m,'I'); break;
		case EXTENDED_KEY_PAD_F1:		WriteSS3Character('P'); break;
		case EXTENDED_KEY_PAD_F2:		WriteSS3Character('Q'); break;
		case EXTENDED_KEY_PAD_F3:		WriteSS3Character('R'); break;
		case EXTENDED_KEY_PAD_F4:		WriteSS3Character('S'); break;
		case EXTENDED_KEY_PAD_F5:		WriteSS3Character('T'); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_EQUALS:		WriteDECVTNumericKeypadKey('X',    '='     ); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_FIND:			WriteDECFNK(1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_INSERT:		// this, which teken does not distinguish from:
		case EXTENDED_KEY_INSERT:		WriteDECFNK(2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_DELETE:		WriteDELOrDECFNK(m); break;
		case EXTENDED_KEY_SELECT:		WriteDECFNK(4U,m); break;
		case EXTENDED_KEY_PREVIOUS:		// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_PAGE_UP:		// this, which teken does not distinguish from:
		case EXTENDED_KEY_PAGE_UP:		WriteDECFNK(5U,m); break;
		case EXTENDED_KEY_NEXT:			// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_PAGE_DOWN:	// this, which teken does not distinguish from:
		case EXTENDED_KEY_PAGE_DOWN:		WriteDECFNK(6U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(m); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteReturnEnter(m); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteReturnEnter(m); break;
		default:				WriteExtendedKeyCommonExtensions(k,m); break;
	}
}

// This is what a DEC VT520 produces in SCO Console mode.
// It natches teken in CONS25 mode, and the older cons25 FreeBSD console.
//
// This emulation is fairly feature-poor.  Some important differences from DEC VT520 mode:
//  * There are no distinctions between application and normal modes.  The SCO console simply does not have this.
//  * There are no DECFNK sequences for the level 3 (actually ALT) modifier.
//  * There are no distinctions between cursor keypad keys and calculator keypad keys, at all.
//  * Keypad plus/minus/enter/comma/slash/asterisk do not ever produce control sequences.
//  * The SCO console does not have numeric keypad comma and equals; we are thus not bound by compatibility and give them DEC VT semantics.
//  * Delete keys always send DEL.
void 
ECMA48InputEncoder::WriteExtendedKeySCOConsole(uint16_t k, uint8_t m)
{
	switch (k) {
		case EXTENDED_KEY_PAD_ASTERISK:		WriteRawCharacters("*"); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteRawCharacters("+"); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteRawCharacters(","); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteRawCharacters("-"); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteRawCharacters("/"); break;
		case EXTENDED_KEY_SCROLL_UP:		// This is not a SCO console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_UP:		// this, which the SCO console does not distinguish from:
		case EXTENDED_KEY_UP_ARROW:		WriteCSISequence(m,'A'); break;
		case EXTENDED_KEY_SCROLL_DOWN:		// This is not a SCO console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DOWN:		// this, which the SCO console does not distinguish from:
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
		case EXTENDED_KEY_PAD_EQUALS_AS400:	// This is not a SCO console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_EQUALS:		WriteDECVTNumericKeypadKey('X',    '='     ); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(m); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteReturnEnter(m); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteReturnEnter(m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a SCO console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DELETE:		// this, which the SCO console does not distinguish from:
		case EXTENDED_KEY_DELETE:		WriteRawCharacters("\x7F"); break;
		default:				WriteExtendedKeyCommonExtensions(k,m); break;
	}
}

// This is what the Linux kernel terminal emulator produces.
// The termcap/terminfo name is "linux".
//
// This emulation is fairly feature-poor.  Some important differences from DEC VT520 mode:
//  * There are no distinctions between application and normal modes.  The Linux console simply does not have this.
//  * There are no special sequences for the level 3 (actually ALT) modifier.
//  * There are no distinctions between cursor keypad keys and calculator keypad keys, at all.
//  * Keypad plus/minus/enter/comma/slash/asterisk do not ever produce control sequences.
//  * Calculator keypad function keys send LinuxFNK sequences too similar to cursor keys.
//  * HOME and END send the DEC VT sequences for FIND and SELECT rather than their own sequences.
// Some differences from the vanilla Linux console:
//  * The Linux console does not report any modifiers at all; we do.
//  * The Linux console does not provide LF from return or enter with the control modifier; we do.
//  * The Linux console does not have numeric keypad comma and equals; we are thus not bound by compatibility and give them DEC VT semantics.
void 
ECMA48InputEncoder::WriteExtendedKeyLinuxConsole(uint16_t k, uint8_t m)
{
	switch (k) {
		case EXTENDED_KEY_PAD_ASTERISK:		WriteRawCharacters("*"); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteRawCharacters("+"); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteRawCharacters(","); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteRawCharacters("-"); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteRawCharacters("/"); break;
		case EXTENDED_KEY_SCROLL_UP:		// This is not a Linux console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_UP:		// this, which the Linux console does not distinguish from:
		case EXTENDED_KEY_UP_ARROW:		WriteCSISequence(m,'A'); break;
		case EXTENDED_KEY_SCROLL_DOWN:		// This is not a Linux console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DOWN:		// this, which the Linux console does not distinguish from:
		case EXTENDED_KEY_DOWN_ARROW:		WriteCSISequence(m,'B'); break;
		case EXTENDED_KEY_PAD_RIGHT:		// The Linux console does not distinguish this from:
		case EXTENDED_KEY_RIGHT_ARROW:		WriteCSISequence(m,'C'); break;
		case EXTENDED_KEY_PAD_LEFT:		// The Linux console does not distinguish this from:
		case EXTENDED_KEY_LEFT_ARROW:		WriteCSISequence(m,'D'); break;
		case EXTENDED_KEY_PAD_CENTRE:		// The Linux console does not distinguish this from:
		case EXTENDED_KEY_CENTRE:		WriteCSISequence(m,'G'); break;
		case EXTENDED_KEY_PAD_TAB:		WriteCSISequence(m,'I'); break;
		case EXTENDED_KEY_PAD_F1:		WriteLinuxConsoleFNK(m,'A'); break;
		case EXTENDED_KEY_PAD_F2:		WriteLinuxConsoleFNK(m,'B'); break;
		case EXTENDED_KEY_PAD_F3:		WriteLinuxConsoleFNK(m,'C'); break;
		case EXTENDED_KEY_PAD_F4:		WriteLinuxConsoleFNK(m,'D'); break;
		case EXTENDED_KEY_PAD_F5:		WriteLinuxConsoleFNK(m,'E'); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	// This is not a Linux console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_EQUALS:		WriteDECVTNumericKeypadKey('X',    '='     ); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_HOME:			// The Linux console does not distinguish this from:
		case EXTENDED_KEY_PAD_HOME:		// this, which the Linux console erroneously makes the same as:
		case EXTENDED_KEY_FIND:			WriteDECFNK(1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a Linux console key, but we make it equivalent to:
		case EXTENDED_KEY_INSERT:		// this, which the Linux console does not distinguish from:
		case EXTENDED_KEY_PAD_INSERT:		WriteDECFNK(2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a Linux console key, but we make it equivalent to:
		case EXTENDED_KEY_DELETE:		// this, which the Linux console does not distinguish from:
		case EXTENDED_KEY_PAD_DELETE:		WriteDELOrDECFNK(m); break;
		case EXTENDED_KEY_END:			// The Linux console does not distinguish this from:
		case EXTENDED_KEY_PAD_END:		// this, which the Linux console erroneously makes the same as:
		case EXTENDED_KEY_SELECT:		WriteDECFNK(4U,m); break;
		case EXTENDED_KEY_PREVIOUS:		// This is not a Linux console key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_UP:		// this, which the Linux console does not distinguish from:
		case EXTENDED_KEY_PAD_PAGE_UP:		WriteDECFNK(5U,m); break;
		case EXTENDED_KEY_NEXT:			// This is not a Linux console key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_DOWN:		// this, which the Linux console does not distinguish from:
		case EXTENDED_KEY_PAD_PAGE_DOWN:	WriteDECFNK(6U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(m); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteReturnEnter(m); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteReturnEnter(m); break;
		default:				WriteExtendedKeyCommonExtensions(k,m); break;
	}
}

// This is what the NetBSD kernel terminal emulator produces in vt100 mode.
// The termcap/terminfo name is "wsvt".
//
// This emulation is fairly feature-poor.  Some important differences from DEC VT520 mode:
//  * There are no distinctions between application and normal modes.  The NetBSD console simply does not have this.
//  * There are no special sequences for the level 3 (actually ALT) modifier.
//  * There are no distinctions between cursor keypad keys and calculator keypad keys, at all.
//  * Keypad plus/minus/enter/comma/slash/asterisk do not ever produce control sequences.
// Some differences from the vanilla NetBSD console:
//  * The NetBSD console does not have numeric keypad comma and equals; we are thus not bound by compatibility and give them DEC VT semantics.
void 
ECMA48InputEncoder::WriteExtendedKeyNetBSDConsole(uint16_t k, uint8_t m)
{
	switch (k) {
		case EXTENDED_KEY_PAD_ASTERISK:		WriteRawCharacters("*"); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteRawCharacters("+"); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteRawCharacters(","); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteRawCharacters("-"); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteRawCharacters("/"); break;
		case EXTENDED_KEY_SCROLL_UP:		// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_UP:		// this, which the NetBSD console does not distinguish from:
		case EXTENDED_KEY_UP_ARROW:		WriteCSISequence(m,'A'); break;
		case EXTENDED_KEY_SCROLL_DOWN:		// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DOWN:		// this, which the NetBSD console does not distinguish from:
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
		case EXTENDED_KEY_PAD_EQUALS_AS400:	// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_EQUALS:		WriteDECVTNumericKeypadKey('X',    '='     ); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequence(m,'Z'); break;
		case EXTENDED_KEY_FIND:			WriteDECFNK(1U,m); break;
		case EXTENDED_KEY_SELECT:		WriteDECFNK(4U,m); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_PAGE_DOWN:		WriteDECFNK(6U,m); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_PAGE_UP:		WriteDECFNK(5U,m); break;
		case EXTENDED_KEY_PAD_HOME:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_HOME:			WriteDECFNK(7U,m); break;
		case EXTENDED_KEY_PAD_END:		// The NetBSD console does not distinguish this from:
		case EXTENDED_KEY_END:			WriteDECFNK(8U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(m); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteReturnEnter(m); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteReturnEnter(m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DELETE:		// this, which the NetBSD console does not distinguish from:
		case EXTENDED_KEY_DELETE:		WriteRawCharacters("\x7F"); break;
		default:				WriteExtendedKeyCommonExtensions(k,m); break;
	}
}

void 
ECMA48InputEncoder::WriteExtendedKey(uint16_t k, uint8_t m)
{
	SetPasting(false);
	switch (emulation) {
		default:		[[clang::fallthrough]];
		case DECVT:		return WriteExtendedKeyDECVT(k, m);
		case SCO_CONSOLE:	return WriteExtendedKeySCOConsole(k, m);
		case LINUX_CONSOLE:	return WriteExtendedKeyLinuxConsole(k, m);
		case NETBSD_CONSOLE:	return WriteExtendedKeyNetBSDConsole(k, m);
		case XTERM_PC:		return WriteExtendedKeyXTermPC(k, m);
		case TEKEN:		return WriteExtendedKeyTeken(k, m);
	}
}

void 
ECMA48InputEncoder::WriteWheelMotion(uint8_t w, int8_t o, uint8_t m) 
{
	SetPasting(false);
	// The horizontal wheel (#1) is an extension to the xterm protocol.
	// Each direction on a wheel is a different button.
	// XTerm reports use buttons 3 and upwards, because actual mice buttons were numbered from 0 to 2.
	// DEC Locator reports use buttons 4 and upwards, because the original DEC Locator specification defined 4 actual mouse buttons.
	while (0 != o) {
		if (0 > o) {
			++o;
			const int button(4 + 2 * w);
			mouse_buttons[button] = true;
			WriteXTermMouse(button, m);
			WriteDECLocatorReport(button + 1);
			mouse_buttons[button] = false;
#if 0	// vim cannot cope with button up wheel events.
			WriteXTermMouse(button, m);
#endif
			WriteDECLocatorReport(button + 1);
		}
		if (0 < o) {
			--o;
			const int button(3 + 2 * w);
			mouse_buttons[button] = true;
			WriteXTermMouse(button, m);
			WriteDECLocatorReport(button + 1);
			mouse_buttons[button] = false;
#if 0	// vim cannot cope with button up wheel events.
			WriteXTermMouse(button, m);
#endif
			WriteDECLocatorReport(button + 1);
		}
	}
}

void 
ECMA48InputEncoder::WriteXTermMouse(
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
ECMA48InputEncoder::WriteDECLocatorReport(int button) 
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
ECMA48InputEncoder::SetMouseX(uint16_t p, uint8_t m) 
{
	SetPasting(false);
	if (mouse_column != p) {
		mouse_column = p; 
		WriteXTermMouse(-1, m);
		// DEC Locator reports only report button events.
	}
}

void 
ECMA48InputEncoder::SetMouseY(uint16_t p, uint8_t m) 
{ 
	SetPasting(false);
	if (mouse_row != p) {
		mouse_row = p; 
		WriteXTermMouse(-1, m);
		// DEC Locator reports only report button events.
	}
}

void 
ECMA48InputEncoder::SetMouseButton(uint8_t b, bool v, uint8_t m) 
{ 
	SetPasting(false);
	if (mouse_buttons[b] != v) {
		mouse_buttons[b] = v; 
		WriteXTermMouse(b, m);
		WriteDECLocatorReport(b);
	}
}

void 
ECMA48InputEncoder::RequestDECLocatorReport()
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
ECMA48InputEncoder::WriteUCS3Character(uint32_t c, bool pasted, bool accelerator)
{
	SetPasting(pasted);
	if (accelerator)
		WriteUnicodeCharacter(ESC);
	WriteUnicodeCharacter(c);
	// Interrupt after any pasted character that could otherwise begin a DECFNK sequence.
	if (0x1B == c || 0x9B == c)
		SetPasting(false);
}

void
ECMA48InputEncoder::HandleMessage(uint32_t b)
{
	switch (b & INPUT_MSG_MASK) {
		case INPUT_MSG_UCS3:	WriteUCS3Character(b & ~INPUT_MSG_MASK, false, false); break;
		case INPUT_MSG_PUCS3:	WriteUCS3Character(b & ~INPUT_MSG_MASK, true, false); break;
		case INPUT_MSG_AUCS3:	WriteUCS3Character(b & ~INPUT_MSG_MASK, false, true); break;
		case INPUT_MSG_EKEY:	WriteExtendedKey((b >> 8U) & 0xFFFF, b & 0xFF); break;
		case INPUT_MSG_FKEY:	WriteFunctionKey((b >> 8U) & 0xFFFF, b & 0xFF); break;
		case INPUT_MSG_XPOS:	SetMouseX((b >> 8U) & 0xFFFF, b & 0xFF); break;
		case INPUT_MSG_YPOS:	SetMouseY((b >> 8U) & 0xFFFF, b & 0xFF); break;
		case INPUT_MSG_WHEEL:	WriteWheelMotion((b >> 16U) & 0xFF, static_cast<int8_t>((b >> 8U) & 0xFF), b & 0xFF); break;
		case INPUT_MSG_BUTTON:	SetMouseButton((b >> 16U) & 0xFF, (b >> 8U) & 0xFF, b & 0xFF); break;
		default:
			std::fprintf(stderr, "WARNING: %s: %" PRIx32 "\n", "Unknown input message", b);
			break;
	}
}

void
ECMA48InputEncoder::WriteOutput()
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
	virtual void SetSize(const coordinate & w, const coordinate & h);
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
MultipleBuffer::SetSize(const coordinate & w, const coordinate & h)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->SetSize(w, h);
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
	emulation_definition(char s, const char * l, const char * d, ECMA48InputEncoder::Emulation & e, ECMA48InputEncoder::Emulation v) : simple_named_definition(s, l, d), emulation(e), value(v) {}
	virtual void action(popt::processor &);
	virtual ~emulation_definition();
protected:
	ECMA48InputEncoder::Emulation & emulation;
	ECMA48InputEncoder::Emulation value;
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
	ECMA48InputEncoder::Emulation emulation(ECMA48InputEncoder::LINUX_CONSOLE);
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	ECMA48InputEncoder::Emulation emulation(ECMA48InputEncoder::TEKEN);
#elif defined(__NetBSD__) || defined(__OpenBSD__)
	ECMA48InputEncoder::Emulation emulation(ECMA48InputEncoder::NETBSD_CONSOLE);
#else
	ECMA48InputEncoder::Emulation emulation(ECMA48InputEncoder::DECVT);
#endif
	bool vcsa(false);

	try {
		emulation_definition linux_option('\0', "linux", "Emulate the Linux virtual console.", emulation, ECMA48InputEncoder::LINUX_CONSOLE);
		emulation_definition sco_option('\0', "sco", "Emulate the SCO virtual console.", emulation, ECMA48InputEncoder::SCO_CONSOLE);
		emulation_definition teken_option('\0', "teken", "Emulate the teken library.", emulation, ECMA48InputEncoder::TEKEN);
		emulation_definition netbsd_option('\0', "netbsd", "Emulate the NetBSD virtual console.", emulation, ECMA48InputEncoder::NETBSD_CONSOLE);
		emulation_definition decvt_option('\0', "decvt", "Emulate the DEC VT.", emulation, ECMA48InputEncoder::DECVT);
		emulation_definition xtermpc_option('\0', "xtermpc", "Emulate a subset of XTerm in Sun/PC mode.", emulation, ECMA48InputEncoder::XTERM_PC);
		popt::bool_definition vcsa_option('\0', "vcsa", "Maintain a vcsa-compatible display buffer.", vcsa);
		popt::definition * top_table[] = {
			&linux_option,
			&sco_option,
			&teken_option,
			&netbsd_option,
			&decvt_option,
			&xtermpc_option,
			&vcsa_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{directory}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
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

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}
	std::vector<struct kevent> ip;

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
	InputFIFO input_fifo(open_read_at(dir_fd.get(), "input"));
	if (0 > input_fifo.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (0 > fchown(input_fifo.get(), -1, getegid())) {
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

	append_event(ip, PTY_MASTER_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
	append_event(ip, PTY_MASTER_FILENO, EVFILT_WRITE, EV_ADD, 0, 0, 0);
	append_event(ip, input_fifo.get(), EVFILT_READ, EV_ADD, 0, 0, 0);
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, 0);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);

	ubuffer.WriteBOM();
	MultipleBuffer mbuffer;
	mbuffer.Add(&ubuffer);
	if (vcsa)
		mbuffer.Add(&vbuffer);
	ECMA48InputEncoder input_encoder(PTY_MASTER_FILENO, emulation);
	// X terminal emulators choose 80 by 24, for compatibility with real DEC VTs.
	// We choose 80 by 25 because we are, rather, being compatible with the kernel terminal emluators, which have no status lines and default to PC 25 line modes.
	SoftTerm emulator(mbuffer, input_encoder, input_encoder, 80U, 25U);
	// We want slightly different defaults, with UTF-8 input mode on because that's what our input encoder sends, and tostop mode on.
	tcsetattr_nointr(PTY_MASTER_FILENO, TCSADRAIN, sane(false /*tostop on*/, false /*utf8 on*/));

	bool hangup(false);
	while (!shutdown_signalled && !hangup) {
		append_event(ip, PTY_MASTER_FILENO, EVFILT_WRITE, input_encoder.OutputAvailable() ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		append_event(ip, input_fifo.get(), EVFILT_READ, input_encoder.HasInputSpace() ? EV_ENABLE : EV_DISABLE, 0, 0, 0);

		struct kevent p[128];
		const int rc(kevent(queue, ip.data(), ip.size(), p, sizeof p/sizeof *p, 0));
		ip.clear();

		if (0 > rc) {
			if (EINTR == errno) continue;
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}

		bool masterin_ready(false), masterout_ready(false), master_hangup(false), fifo_ready(false), fifo_hangup(false);

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_SIGNAL:
					handle_signal(e.ident);
					break;
				case EVFILT_READ:
					if (PTY_MASTER_FILENO == e.ident) {
						masterin_ready = true;
						master_hangup |= EV_EOF & e.flags;
					}
					if (input_fifo.get() == static_cast<int>(e.ident)) {
						fifo_ready = true;
						fifo_hangup |= EV_EOF & e.flags;
					}
					break;
				case EVFILT_WRITE:
					if (PTY_MASTER_FILENO == e.ident)
						masterout_ready = true;
					break;
			}
		}

		if (masterin_ready) {
			char b[16384];
			const int l(read(PTY_MASTER_FILENO, b, sizeof b));
			if (l > 0) {
				for (int i(0); i < l; ++i)
					emulator.Process(b[i]);
				master_hangup = false;
			}
		}
		if (fifo_ready)
			input_fifo.ReadInput();
		if (master_hangup)
			hangup = true;
		while (input_fifo.HasMessage() && input_encoder.HasInputSpace())
			input_encoder.HandleMessage(input_fifo.PullMessage());
		if (masterout_ready) 
			input_encoder.WriteOutput();
	}

	unlinkat(dir_fd.get(), "tty", 0);
	throw EXIT_SUCCESS;
}
