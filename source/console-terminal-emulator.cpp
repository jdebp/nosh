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
#include <csignal>
#include <climits>
#include <cerrno>
#include <cctype>
#include <iostream>
#include <inttypes.h>
#include <stdint.h>
#include <sys/types.h>
#include <termios.h>
#if defined(__linux__) || defined(__LINUX__)
#include <sys/ioctl.h>	// For struct winsize on Linux
#endif
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
#include "UTF8Encoder.h"
#include "UnicodeClassification.h"

/* Old-style vcsa screen buffer *********************************************
// **************************************************************************
*/

namespace {

class VCSA : 
	public SoftTerm::ScreenBuffer,
	public FileDescriptorOwner
{
public:
	VCSA(int d) : FileDescriptorOwner(d), saved_buffer(), altbuffer(false) {}
	virtual void ReadCell(coordinate s, CharacterCell & c);
	virtual void WriteNCells(coordinate p, coordinate n, const CharacterCell & c);
	virtual void ModifyNCells(coordinate s, coordinate n, CharacterCell::attribute_type turnoff, CharacterCell::attribute_type flipon, bool fg_touched, const CharacterCell::colour_type & fg, bool bg_touched, const CharacterCell::colour_type & bg);
	virtual void CopyNCells(coordinate d, coordinate s, coordinate n);
	virtual void ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void SetCursorPos(coordinate x, coordinate y);
	virtual void SetCursorType(CursorSprite::glyph_type, CursorSprite::attribute_type);
	virtual void SetPointerType(PointerSprite::attribute_type);
	virtual void SetScreenFlags(ScreenFlags::flag_type);
	virtual void SetSize(coordinate w, coordinate h);
	virtual void SetAltBuffer(bool);
protected:
	enum { CELL_LENGTH = 2U, HEADER_LENGTH = 4U };
	struct cell { char bytes[CELL_LENGTH]; };
	typedef std::vector<cell> SaveBuffer;
	SaveBuffer saved_buffer;
	bool altbuffer;
	static char MakeA(const CharacterCell::attribute_type, unsigned char vgaf, unsigned char vgab);
	static void MakeCA(cell & ca, const CharacterCell & c);
	static off_t MakeOffset(coordinate s) { return HEADER_LENGTH + CELL_LENGTH * s; }
	ssize_t pread(cell & ca, off_t o) { return ::pread(fd, &ca, sizeof ca, o); }
	ssize_t pwrite(const cell & ca, off_t o) { return ::pwrite(fd, &ca, sizeof ca, o); }
};

inline
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

inline unsigned int TranslateToDECModifiers(unsigned int n) { return n + 1U; }
inline unsigned int TranslateToDECCoordinates(unsigned int n) { return n + 1U; }
inline uint16_t TranslateToXTermButton(const uint16_t button) {
	switch (button) {
		case 1U:	return 2U;
		case 2U:	return 1U;
		default:	return button;
	}
}

}

inline
char
VCSA::MakeA(
	const CharacterCell::attribute_type attributes,
	unsigned char vgaf,
	unsigned char vgab
) {
	return	(attributes & CharacterCell::BLINK ? 0x80 : 0x00) |
		(attributes & CharacterCell::BOLD ? 0x08 : 0x00) |
		(vgaf << 0) |
		(vgab << 4) |
		0;
}

void 
VCSA::MakeCA(cell & ca, const CharacterCell & c)
{
	ca.bytes[0] = c.character > 0xFE ? 0xFF : c.character;
	ca.bytes[1] = MakeA(c.attributes, VGAColour(c.foreground), VGAColour(c.background));
}

void
VCSA::ReadCell(coordinate, CharacterCell &)
{
	// Do nothing.  The Unicode buffer will handle it.
}

void 
VCSA::WriteNCells(coordinate s, coordinate n, const CharacterCell & c)
{
	cell ca[256U];
	for (coordinate i(0U); i < n && i < (sizeof ca / sizeof *ca); ++i)
		MakeCA(ca[i], c);
	for (coordinate i(0U), w(sizeof ca / sizeof *ca); i < n; i += w) {
		if (w > n) w = n;
		::pwrite(fd, ca, sizeof *ca * w, MakeOffset(s + i));
	}
}

void
VCSA::ModifyNCells(
	coordinate s,
	coordinate n,
	CharacterCell::attribute_type turnoff,
	CharacterCell::attribute_type flipon,
	bool fg_touched,
	const CharacterCell::colour_type & fg,
	bool bg_touched,
	const CharacterCell::colour_type & bg
) {
	cell ca[256U];
	coordinate w(sizeof ca / sizeof *ca);
	while (n) {
		if (w > n) w = n;
		const off_t source(MakeOffset(s));
		::pread(fd, ca, sizeof *ca * w, source);
		for (coordinate i(0U); i < w; ++i) {
			CharacterCell::attribute_type attributes = 0U
				| (ca[i].bytes[1U] & 0x80 ? CharacterCell::BLINK : 0U) 
				| (ca[i].bytes[1U] & 0x08 ? CharacterCell::BOLD : 0U)
			;
			attributes = (attributes & ~turnoff) | flipon;
			const unsigned char f(fg_touched ? VGAColour(fg) : (ca[i].bytes[1U] & 0x07) >> 0U);
			const unsigned char b(bg_touched ? VGAColour(bg) : (ca[i].bytes[1U] & 0x70) >> 4U);
			ca[i].bytes[1U] = MakeA(attributes, f, b);
		}
		::pwrite(fd, ca, sizeof *ca * w, source);
		s += w;
		n -= w;
	}
}

void 
VCSA::CopyNCells(coordinate d, coordinate s, coordinate n)
{
	cell ca[256U];
	coordinate w(sizeof ca / sizeof *ca);
	if (d < s) {
		while (n) {
			if (w > n) w = n;
			const off_t target(MakeOffset(d));
			const off_t source(MakeOffset(s));
			::pread(fd, ca, sizeof *ca * w, source);
			::pwrite(fd, ca, sizeof *ca * w, target);
			s += w;
			d += w;
			n -= w;
		}
	} else 
	if (d > s) {
		s += n;
		d += n;
		while (n) {
			if (w > n) w = n;
			s -= w;
			d -= w;
			n -= w;
			const off_t target(MakeOffset(d));
			const off_t source(MakeOffset(s));
			::pread(fd, ca, sizeof *ca * w, source);
			::pwrite(fd, ca, sizeof *ca * w, target);
		}
	}
}

void 
VCSA::ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c)
{
	cell ca;
	while (s + n < e) {
		const off_t target(MakeOffset(s));
		const off_t source(MakeOffset(s + n));
		pread(ca, source);
		pwrite(ca, target);
		++s;
	}
	MakeCA(ca, c);
	while (s < e) {
		pwrite(ca, MakeOffset(s));
		++s;
	}
}

void 
VCSA::ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c)
{
	cell ca;
	while (e > s + n) {
		--e;
		const off_t target(MakeOffset(e));
		const off_t source(MakeOffset(e - n));
		pread(ca, source);
		pwrite(ca, target);
	}
	MakeCA(ca, c);
	while (e > s) {
		--e;
		pwrite(ca, MakeOffset(e));
	}
}

void 
VCSA::SetCursorPos(coordinate x, coordinate y)
{
	const unsigned char b[2] = { static_cast<unsigned char>(x), static_cast<unsigned char>(y) };
	::pwrite(fd, b, sizeof b, 2);
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
VCSA::SetScreenFlags(ScreenFlags::flag_type)
{
}

void 
VCSA::SetSize(coordinate w, coordinate h)
{
	const unsigned char b[2] = { static_cast<unsigned char>(h), static_cast<unsigned char>(w) };
	::pwrite(fd, b, sizeof b, 0);
	ftruncate(fd, MakeOffset(w * h));
	saved_buffer.resize(w * h);
}

void
VCSA::SetAltBuffer(bool on)
{
	if (altbuffer == on) return;
	cell ca[256U];
	coordinate w(sizeof ca/sizeof *ca);
	for (SaveBuffer::iterator b(saved_buffer.begin()), e(saved_buffer.end()), p(b); p < e; p += w) {
		if (w > (e - p)) w = e - p;
		const off_t o(MakeOffset(p - b));
		for (coordinate n(0U); n < w; ++n)
			ca[n] = p[n];
		::pread(fd, &(*p), sizeof *ca * w, o);
		::pwrite(fd, ca, sizeof *ca * w, o);
	}
	altbuffer = on;
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
	UnicodeBuffer(int d);
	void WriteBOM();
	virtual void ReadCell(coordinate s, CharacterCell & c);
	virtual void WriteNCells(coordinate p, coordinate n, const CharacterCell & c);
	virtual void ModifyNCells(coordinate s, coordinate n, CharacterCell::attribute_type turnoff, CharacterCell::attribute_type flipon, bool fg_touched, const CharacterCell::colour_type & fg, bool bg_touched, const CharacterCell::colour_type & bg);
	virtual void CopyNCells(coordinate d, coordinate s, coordinate n);
	virtual void ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void SetCursorPos(coordinate x, coordinate y);
	virtual void SetCursorType(CursorSprite::glyph_type, CursorSprite::attribute_type);
	virtual void SetPointerType(PointerSprite::attribute_type);
	virtual void SetScreenFlags(ScreenFlags::flag_type);
	virtual void SetSize(coordinate w, coordinate h);
	virtual void SetAltBuffer(bool);
protected:
	enum { CELL_LENGTH = 16U, HEADER_LENGTH = 16U };
	struct cell { char bytes[CELL_LENGTH]; };
	typedef std::vector<cell> SaveBuffer;
	SaveBuffer saved_buffer;
	bool altbuffer;
	static void MakeCA(cell &, const CharacterCell & cell);
	static void MakeF(cell &, const CharacterCell::colour_type & foreground);
	static void MakeB(cell &, const CharacterCell::colour_type & background);
	static void MakeA(cell &, const CharacterCell::attribute_type & attributes);
	static void ReadCA(const cell &, CharacterCell & c);
	static void ReadF(const cell &, CharacterCell::colour_type & foreground);
	static void ReadB(const cell &, CharacterCell::colour_type & background);
	static void ReadA(const cell &, CharacterCell::attribute_type & attributes);
	static CharacterCell::attribute_type GetA(const cell &);
	static off_t MakeOffset(coordinate s) { return HEADER_LENGTH + CELL_LENGTH * static_cast<off_t>(s); }
	ssize_t pread(cell & ca, off_t o) { return ::pread(fd, &ca, sizeof ca, o); }
private:
	char header2[4];
};
}

UnicodeBuffer::UnicodeBuffer(
	int d
) : 
	FileDescriptorOwner(d),
	saved_buffer(),
	altbuffer(false) 
{
	std::fill(header2, header2 + sizeof(header2)/sizeof(*header2), '\0');
}

void
UnicodeBuffer::WriteBOM() 
{
	const uint32_t bom(0xFEFF);
	::pwrite(fd, &bom, sizeof bom, 0U);
}

inline
void
UnicodeBuffer::MakeF(cell & c, const CharacterCell::colour_type & foreground)
{
	c.bytes[0] = foreground.alpha;
	c.bytes[1] = foreground.red;
	c.bytes[2] = foreground.green;
	c.bytes[3] = foreground.blue;
}

inline
void
UnicodeBuffer::MakeB(cell & c, const CharacterCell::colour_type & background)
{
	c.bytes[4] = background.alpha;
	c.bytes[5] = background.red;
	c.bytes[6] = background.green;
	c.bytes[7] = background.blue;
}

inline
void
UnicodeBuffer::MakeA(cell & c, const CharacterCell::attribute_type & attributes)
{
	c.bytes[12] = (attributes >> 0) & 0xFF;
	c.bytes[13] = (attributes >> 8) & 0xFF;
}

inline
void
UnicodeBuffer::ReadF(const cell & c, CharacterCell::colour_type & foreground)
{
	foreground.alpha = c.bytes[0];
	foreground.red = c.bytes[1];
	foreground.green = c.bytes[2];
	foreground.blue = c.bytes[3];
}

inline
void
UnicodeBuffer::ReadB(const cell & c, CharacterCell::colour_type & background)
{
	background.alpha = c.bytes[4];
	background.red = c.bytes[5];
	background.green = c.bytes[6];
	background.blue = c.bytes[7];
}

inline
void
UnicodeBuffer::ReadA(const cell & c, CharacterCell::attribute_type & attributes)
{
	attributes = (static_cast<uint_fast16_t>(c.bytes[12]) & 0xFF) | (static_cast<uint_fast16_t>(c.bytes[13]) & 0xFF) << 8;
}

void 
UnicodeBuffer::MakeCA(cell & c, const CharacterCell & cell)
{
	std::fill(c.bytes, c.bytes + sizeof c.bytes/sizeof *c.bytes, '\0');
	MakeF(c, cell.foreground);
	MakeB(c, cell.background);
	std::memcpy(&c.bytes[8], &cell.character, 4);
	MakeA(c, cell.attributes);
}

void 
UnicodeBuffer::ReadCA(const cell & c, CharacterCell & cell)
{
	ReadF(c, cell.foreground);
	ReadB(c, cell.background);
	std::memcpy(&cell.character, &c.bytes[8], 4);
	ReadA(c, cell.attributes);
}

inline
CharacterCell::attribute_type
UnicodeBuffer::GetA(const cell & c)
{
	return static_cast<unsigned short>(c.bytes[12]) | static_cast<unsigned short>(c.bytes[13]) << 8U;
}

void
UnicodeBuffer::ReadCell(coordinate s, CharacterCell & c)
{
	cell ca;
	pread(ca, MakeOffset(s));
	ReadCA(ca, c);
}

void 
UnicodeBuffer::WriteNCells(coordinate s, coordinate n, const CharacterCell & c)
{
	cell ca[256U];
	for (coordinate i(0U); i < n && i < (sizeof ca / sizeof *ca); ++i)
		MakeCA(ca[i], c);
	for (coordinate i(0U), w(sizeof ca / sizeof *ca); i < n; i += w) {
		if (w > n) w = n;
		::pwrite(fd, ca, sizeof *ca * w, MakeOffset(s + i));
	}
}

void
UnicodeBuffer::ModifyNCells(
	coordinate s,
	coordinate n,
	CharacterCell::attribute_type turnoff,
	CharacterCell::attribute_type flipon,
	bool fg_touched,
	const CharacterCell::colour_type & fg,
	bool bg_touched,
	const CharacterCell::colour_type & bg
) {
	cell ca[256U];
	coordinate w(sizeof ca / sizeof *ca);
	while (n) {
		if (w > n) w = n;
		const off_t source(MakeOffset(s));
		::pread(fd, ca, sizeof *ca * w, source);
		for (coordinate i(0U); i < w; ++i) {
			CharacterCell::attribute_type attributes(GetA(ca[i]));
			attributes = (attributes & ~turnoff) | flipon;
			MakeA(ca[i], attributes);
		}
		if (fg_touched)
			for (coordinate i(0U); i < w; ++i)
				MakeF(ca[i], fg);
		if (bg_touched)
			for (coordinate i(0U); i < w; ++i)
				MakeB(ca[i], bg);
		::pwrite(fd, ca, sizeof *ca * w, source);
		s += w;
		n -= w;
	}
}

void 
UnicodeBuffer::CopyNCells(
	coordinate d,
	coordinate s,
	coordinate n
) {
	cell ca[256U];
	coordinate w(sizeof ca / sizeof *ca);
	if (d < s) {
		while (n) {
			if (w > n) w = n;
			const off_t target(MakeOffset(d));
			const off_t source(MakeOffset(s));
			::pread(fd, ca, sizeof *ca * w, source);
			::pwrite(fd, ca, sizeof *ca * w, target);
			s += w;
			d += w;
			n -= w;
		}
	} else 
	if (d > s) {
		s += n;
		d += n;
		while (n) {
			if (w > n) w = n;
			s -= w;
			d -= w;
			n -= w;
			const off_t target(MakeOffset(d));
			const off_t source(MakeOffset(s));
			::pread(fd, ca, sizeof *ca * w, source);
			::pwrite(fd, ca, sizeof *ca * w, target);
		}
	}
}

void 
UnicodeBuffer::ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c)
{
	cell ca[256U];
	for (coordinate w(sizeof ca / sizeof *ca); s + n < e; s += w) {
		if (s + n + w > e) w = e - (s + n);
		const off_t target(MakeOffset(s));
		const off_t source(MakeOffset(s + n));
		::pread(fd, ca, sizeof *ca * w, source);
		::pwrite(fd, ca, sizeof *ca * w, target);
	}
	for (coordinate i(0U); i < (sizeof ca / sizeof *ca); ++i)
		MakeCA(ca[i], c);
	for (coordinate w(sizeof ca / sizeof *ca); s < e; s += w) {
		if (s + w > e) w = e - s;
		::pwrite(fd, ca, sizeof *ca * w, MakeOffset(s));
	}
}

void 
UnicodeBuffer::ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c)
{
	cell ca[256U];
	for (coordinate w(sizeof ca / sizeof *ca); e > s + n; ) {
		if (s + n + w > e) w = e - (s + n);
		e -= w;
		const off_t target(MakeOffset(e));
		const off_t source(MakeOffset(e - n));
		::pread(fd, ca, sizeof *ca * w, source);
		::pwrite(fd, ca, sizeof *ca * w, target);
	}
	for (coordinate i(0U); i < (sizeof ca / sizeof *ca); ++i)
		MakeCA(ca[i], c);
	for (coordinate w(sizeof ca / sizeof *ca); e > s; ) {
		if (s + w > e) w = e - s;
		e -= w;
		::pwrite(fd, ca, sizeof *ca * w, MakeOffset(e));
	}
}

void 
UnicodeBuffer::SetCursorType(CursorSprite::glyph_type g, CursorSprite::attribute_type a)
{
	header2[0] = static_cast<char>((header2[0] & ~0x0F) | (0x0F & g));
	header2[1] = static_cast<char>((header2[1] & ~0x0F) | (0x0F & a));
	::pwrite(fd, header2, 2, 12U);
}

void 
UnicodeBuffer::SetPointerType(PointerSprite::attribute_type a)
{
	header2[2] = static_cast<char>((header2[2] & ~0x0F) | (0x0F & a));
	::pwrite(fd, header2 + 2, 1, 14U);
}

void 
UnicodeBuffer::SetScreenFlags(ScreenFlags::flag_type f)
{
	header2[2] = static_cast<char>((header2[2] & 0x0F) | (f << 4));
	::pwrite(fd, header2 + 2, 1, 14U);
}

void 
UnicodeBuffer::SetCursorPos(coordinate x, coordinate y)
{
	const uint16_t b[2] = { static_cast<uint16_t>(x), static_cast<uint16_t>(y) };
	::pwrite(fd, b, sizeof b, 8U);
}

void 
UnicodeBuffer::SetSize(coordinate w, coordinate h)
{
	const uint16_t b[2] = { static_cast<uint16_t>(w), static_cast<uint16_t>(h) };
	::pwrite(fd, b, sizeof b, 4U);
	ftruncate(fd, MakeOffset(w * h));
	saved_buffer.resize(w * h);
}

void
UnicodeBuffer::SetAltBuffer(bool on)
{
	if (altbuffer == on) return;
	cell ca[256U];
	coordinate w(sizeof ca/sizeof *ca);
	for (SaveBuffer::iterator b(saved_buffer.begin()), e(saved_buffer.end()), p(b); p < e; p += w) {
		if (w > (e - p)) w = e - p;
		const off_t o(MakeOffset(p - b));
		for (coordinate n(0U); n < w; ++n)
			ca[n] = p[n];
		::pread(fd, &(*p), sizeof *ca * w, o);
		::pwrite(fd, ca, sizeof *ca * w, o);
	}
	altbuffer = on;
}


/* input side ***************************************************************
// **************************************************************************
*/

namespace {

class ECMA48InputEncoder :
	public SoftTerm::KeyboardBuffer,
	public SoftTerm::MouseBuffer,
	public UTF8Encoder::UTF8CharacterSink
{
public:
	enum Emulation { DECVT, SCO_CONSOLE, LINUX_CONSOLE, NETBSD_CONSOLE, TEKEN, XTERM_PC };
	ECMA48InputEncoder(int, Emulation);
	void HandleMessage(uint32_t);
	void WriteOutput();
	bool OutputAvailable() { return output_pending > 0U; }
	bool HasInputSpace() { return output_pending + 128U < sizeof output_buffer; }
protected:
	const int terminal_back_end_fd;

	void WriteRawCharacters(std::size_t, const char *);
	void WriteRawCharacters(const char * s);
	void WriteRawCharacter(const char);
	void WriteLatin1Character(char);
	void WriteUnicodeCharacter(uint32_t);

	/// \name Concrete keyboard buffer
	/// Our implementation of SoftTerm::KeyboardBuffer
	/// @{
	virtual void WriteLatin1Characters(std::size_t, const char *);
	virtual void WriteControl1Character(uint8_t);
	virtual void Set8BitControl1(bool);
	virtual void SetBackspaceIsBS(bool);
	virtual void SetEscapeIsFS(bool);
	virtual void SetDeleteIsDEL(bool);
	virtual void SetDECFunctionKeys(bool);
	virtual void SetSCOFunctionKeys(bool);
	virtual void SetTekenFunctionKeys(bool);
	virtual void SetCursorApplicationMode(bool);
	virtual void SetCalculatorApplicationMode(bool);
	virtual void SetSendPasteEvent(bool);
	virtual void ReportSize(coordinate w, coordinate h);
	/// @}

	/// \name Concrete mouse buffer
	/// Our implementation of SoftTerm::MouseBuffer
	/// @{
	virtual void SetSendXTermMouse(bool);
	virtual void SetSendXTermMouseClicks(bool);
	virtual void SetSendXTermMouseButtonMotions(bool);
	virtual void SetSendXTermMouseNoButtonMotions(bool);
	virtual void SetSendDECLocator(unsigned int);
	virtual void SetSendDECLocatorPressEvent(bool);
	virtual void SetSendDECLocatorReleaseEvent(bool);
	virtual void RequestDECLocatorReport();
	/// @}

	/// \name Concrete UTF encoder sink
	/// Our implementation of UTF8Encoder::UTF8CharacterSink
	/// @{
	virtual void ProcessEncodedUTF8(std::size_t, const char *);
	/// @}

	void WriteCSI();
	void WriteSS3();
	void WriteCSISequence(unsigned r, unsigned m, char c);
	void WriteCSISequenceAmbig(unsigned r, unsigned m, char c);
	void WriteSS3Character(char c);
	void WriteBrokenSS3Sequence(unsigned m, char c);
	void WriteFNK(unsigned n, unsigned m);
	void WriteDECFNK(unsigned n, unsigned m);
	void WriteDECFNKAmbig(unsigned n, unsigned m);
	void WriteXTermModKey(unsigned n, unsigned m);
	void WriteLinuxKVTFNK(unsigned m, char c);
	void WriteUSBExtendedFNK(unsigned n, unsigned m);
	void WriteUSBConsumerFNK(unsigned n, unsigned m);
	void WriteSCOConsoleFNK(unsigned m, char c);
	static signed int SCOFNKCharacter(uint16_t k);
	void WriteLatin1OrCSISequence(char csi_char, char ord_char, unsigned m);
	void WriteSS3OrLatin1(bool shift, char shifted_char, char ord_char);
	void WriteSS3OrCSISequence(bool shift, char c, unsigned m);
	void WriteSS3OrCSISequence(bool shift, char shifted_char, char csi_char, unsigned m);
	void WriteSS3OrCSISequenceAmbig(bool shift, char c, unsigned m);
	void WriteSS3OrDECFNK(bool shift, char c, unsigned decfnk, unsigned m);
	void WriteOrdOrDECFNK(bool ord_mode, char ord_char, unsigned decfnk, unsigned m);
	void WriteOrdOrDECFNKAmbig(bool ord_mode, char ord_char, unsigned decfnk, unsigned m);
	void WriteBackspaceOrDEL(uint8_t m);
	void WriteESCOrFS(uint8_t m);
	void WriteReturnEnter(uint8_t m);
	void SetPasting(bool p);

	/// \name Function key handling
	/// @{
	void WriteFunctionKeyDECVT(uint16_t k, uint8_t m);
	void WriteFunctionKeySCOConsole(uint16_t k, uint8_t m);
	void WriteFunctionKeyTeken(uint16_t k, uint8_t m);
	void WriteFunctionKey(uint16_t k, uint8_t m);
	/// @}
	
	/// \name Extended key handling
	/// @{
	void WriteDECVTKeypadKey(bool app_mode, char app_char, unsigned decfnk, unsigned m);
	void WriteDECVTKeypadKey(bool app_mode, char app_char, char csi_char, unsigned m);
	void WriteDECVTKeypadKey(bool app_mode, char app_char, char csi_char, unsigned decfnk, unsigned m);
	void WriteXTermPCKeypadKey(bool app_mode, char app_char, unsigned decfnk, unsigned m);
	void WriteXTermPCKeypadKey(bool app_mode, char app_char, char csi_char, unsigned m);
	void WriteXTermPCKeypadKey(bool app_mode, char app_char, char csi_char, unsigned decfnk, unsigned m);
	void WriteTekenKeypadKey(bool app_mode, char app_char, char csi_char, unsigned decfnk, unsigned m);
	void WriteExtendedKeyCommonExtensions(uint16_t k, unsigned m);
	void WriteExtendedKeyDECVT(uint16_t k, unsigned m);
	void WriteExtendedKeySCOConsole(uint16_t k, unsigned m);
	void WriteExtendedKeyLinuxKVT(uint16_t k, unsigned m);
	void WriteExtendedKeyNetBSDConsole(uint16_t k, unsigned m);
	void WriteExtendedKeyXTermPC(uint16_t k, unsigned m);
	void WriteExtendedKeyTeken(uint16_t k, unsigned m);
	void WriteExtendedKey(uint16_t k, uint8_t m);
	/// @}

	/// \name Consumer key handling
	/// @{
	void WriteConsumerKey(uint16_t k, uint8_t m);
	/// @}

	/// \name Mouse handling
	/// @{
	void SetMouseX(uint16_t p, uint8_t m);
	void SetMouseY(uint16_t p, uint8_t m);
	void SetMouseButton(uint8_t b, bool v, uint8_t m);
	void WriteWheelMotion(uint8_t b, int8_t o, uint8_t m);
	void WriteXTermMouse(unsigned flags, bool pressed, uint8_t modifiers);
	void WriteXTermMouseButton(unsigned button, bool pressed, uint8_t modifiers);
	void WriteXTermMouseMotion(uint8_t modifiers);
	void WriteXTermMouseWheel(unsigned wheel, bool pressed, uint8_t modifiers);
	void WriteDECLocatorReport(unsigned event, unsigned buttons);
	void WriteDECLocatorReportButton(unsigned button, bool pressed);
	void WriteRequestedDECLocatorReport();
	/// @}

	/// \name Ordinary character message handling
	/// @{
	void WriteUCS3Character(uint32_t c, bool p, bool a);
	/// @}

	UTF8Encoder utf8encoder;
	const Emulation emulation;
	bool send_8bit_controls, backspace_is_bs, escape_is_fs, delete_is_del;
	bool cursor_application_mode, calculator_application_mode;
	bool send_xterm_mouse, send_xterm_mouse_clicks, send_xterm_mouse_button_motions, send_xterm_mouse_nobutton_motions, send_locator_press_events, send_locator_release_events, send_sco_function_keys, send_dec_function_keys, send_teken_function_keys, send_paste;
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
IsAllASCII (
	std::size_t l,
	const char * p
) {
	while (l) {
		if (!isascii(static_cast<unsigned char>(*p++)))
			return false;
		--l;
	}
	return true;
}

}

ECMA48InputEncoder::ECMA48InputEncoder(int m, Emulation e) : 
	terminal_back_end_fd(m),
	utf8encoder(*this),
	emulation(e),
	send_8bit_controls(false),
	backspace_is_bs(false),
	escape_is_fs(false),
	delete_is_del(false),
	cursor_application_mode(false),
	calculator_application_mode(false),
	send_xterm_mouse(false), 
	send_xterm_mouse_clicks(false), 
	send_xterm_mouse_button_motions(false), 
	send_xterm_mouse_nobutton_motions(false),
	send_locator_press_events(false),
	send_locator_release_events(false),
	send_sco_function_keys(false),
	send_dec_function_keys(false),
	send_teken_function_keys(false),
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

inline 
void 
ECMA48InputEncoder::WriteRawCharacter(const char c) 
{ 
	WriteRawCharacters(1U, &c); 
}

void
ECMA48InputEncoder::ProcessEncodedUTF8(std::size_t l, const char * p)
{
	WriteRawCharacters(l, p);
}

inline
void 
ECMA48InputEncoder::WriteUnicodeCharacter(uint32_t c)
{
	if (UnicodeCategorization::IsASCII(c))
		WriteRawCharacter(c);	// Minor saving of a round trip through virtual function dispatch.
	else
		utf8encoder.Process(c);
}

void 
ECMA48InputEncoder::ReportSize(coordinate w, coordinate h)
{
	winsize size = {};
	size.ws_col = w;
	size.ws_row = h;
	sane(size);
	tcsetwinsz_nointr(terminal_back_end_fd, size);
}

inline
void 
ECMA48InputEncoder::WriteLatin1Character(char c)
{
	if (isascii(static_cast<unsigned char>(c)))
		WriteRawCharacter(c);	// Minor saving of a round trip through virtual function dispatch.
	else
		utf8encoder.Process(static_cast<unsigned char>(c));
}

void 
ECMA48InputEncoder::WriteLatin1Characters(std::size_t l, const char * p)
{
	if (IsAllASCII(l, p)) 
		return WriteRawCharacters(l, p);
	else while (l) {
		WriteLatin1Character(*p++);
		--l;
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
ECMA48InputEncoder::SetEscapeIsFS(bool b)
{
	escape_is_fs = b;
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
ECMA48InputEncoder::SetDECFunctionKeys(bool b)
{
	send_dec_function_keys = b;
}

void
ECMA48InputEncoder::SetSCOFunctionKeys(bool b)
{
	send_sco_function_keys = b;
}

void
ECMA48InputEncoder::SetTekenFunctionKeys(bool b)
{
	send_teken_function_keys = b;
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
ECMA48InputEncoder::WriteCSISequence(unsigned r, unsigned m, char c)
{
	WriteCSI();
	if (0U != m || 1U != r) {
		char b[16];
		const int n(snprintf(b, sizeof b, "%u:%u", r, TranslateToDECModifiers(m)));
		WriteRawCharacters(n, b);	// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
	}
	WriteLatin1Character(c);
}

void 
ECMA48InputEncoder::WriteCSISequenceAmbig(unsigned r, unsigned m, char c)
{
	WriteCSI();
	if (0U != m || 1U != r) {
		char b[16];
		const int n(snprintf(b, sizeof b, "%u;%u", r, TranslateToDECModifiers(m)));
		WriteRawCharacters(n, b);	// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
	}
	WriteLatin1Character(c);
}

void 
ECMA48InputEncoder::WriteSS3Character(char c)
{
	WriteSS3();
	WriteLatin1Character(c);
}

/// \brief Write malformed SS3 sequences.
void 
ECMA48InputEncoder::WriteBrokenSS3Sequence(unsigned m, char c)
{
	WriteSS3();
	if (0 != m) {
		char b[16];
		const int n(snprintf(b, sizeof b, "%u", TranslateToDECModifiers(m)));
		WriteRawCharacters(n, b);	// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
	}
	WriteLatin1Character(c);
}

void 
ECMA48InputEncoder::WriteFNK(unsigned n, unsigned m)
{
	WriteCSI();
	char b[16];
	if (0U != m)
		snprintf(b, sizeof b, "%u:%u W", n, m);
	else
		snprintf(b, sizeof b, "%u W", n);
	WriteRawCharacters(b);	// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
}

void 
ECMA48InputEncoder::WriteDECFNK(unsigned n, unsigned m)
{
	WriteCSI();
	char b[16];
	if (0U != m)
		// As an extension, we encode modifiers in ISO 8613-6/ITU T.416 form
		snprintf(b, sizeof b, "%u:%u~", n, TranslateToDECModifiers(m));
	else
		snprintf(b, sizeof b, "%u~", n);
	WriteRawCharacters(b);	// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
}

void 
ECMA48InputEncoder::WriteDECFNKAmbig(unsigned n, unsigned m)
{
	WriteCSI();
	char b[16];
	if (0U != m)
		snprintf(b, sizeof b, "%u;%u~", n, TranslateToDECModifiers(m));
	else
		snprintf(b, sizeof b, "%u~", n);
	WriteRawCharacters(b);	// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
}

void 
ECMA48InputEncoder::WriteXTermModKey(unsigned n, unsigned m)
{
	WriteCSI();
	char b[16];
	snprintf(b, sizeof b, "27;%u;%u~", TranslateToDECModifiers(m), n);
	WriteRawCharacters(b);	// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
}

void 
ECMA48InputEncoder::WriteLinuxKVTFNK(unsigned m, char c)
{
	WriteCSI();
	WriteLatin1Character('[');
	if (0U != m) {
		char b[16];
		snprintf(b, sizeof b, "1;%u", TranslateToDECModifiers(m));
		WriteRawCharacters(b);	// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
	}
	WriteLatin1Character(c);
}

// This is essentially a private variation on FNK.
// Use ? as the parameter character to be sort-of DEC-like.
void 
ECMA48InputEncoder::WriteUSBExtendedFNK(unsigned n, unsigned m)
{
	WriteCSI();
	char b[16];
	if (0U != m)
		snprintf(b, sizeof b, "?%u:%u W", n, m);
	else
		snprintf(b, sizeof b, "?%u W", n);
	WriteRawCharacters(b);	// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
}

// This is essentially a private variation on FNK.
// Use = as the parameter character to be sort-of DEC-like.
void 
ECMA48InputEncoder::WriteUSBConsumerFNK(unsigned n, unsigned m)
{
	WriteCSI();
	char b[16];
	if (0U != m)
		snprintf(b, sizeof b, "=%u:%u W", n, m);
	else
		snprintf(b, sizeof b, "=%u W", n);
	WriteRawCharacters(b);	// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
}

// This isn't at all correct per SCO Unix keyboard(7) doco.
// The SCO Unix Multiscreen virtual terminal used SS3 not CSI.
// But it is the SCO-derived function key encoding that lingers in FreeBSD.
void 
ECMA48InputEncoder::WriteSCOConsoleFNK(unsigned m, char c)
{
	WriteCSI();
	if (0U != m) {
		// As an extension, we encode modifiers in ISO 8613-6/ITU T.416 form
		char b[16];
		snprintf(b, sizeof b, "1:%u", m);
		WriteRawCharacters(b);	// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
	}
	WriteLatin1Character(c);
}

/// \returns a guaranteed to be ASCII final character
/// \retval EOF function key number out of range
// This isn't at all correct per SCO Unix keyboard(7) doco.
// But it is the SCO-derived function key encoding that lingers in FreeBSD.
signed int
ECMA48InputEncoder::SCOFNKCharacter(uint16_t k)
{
	if (1U > k)
		// The SCO system has no F0 ('L').
		return EOF;
	else
	if (15U > k)
		return k - 1 + 'M';
	else
	if (41U > k)
		return k - 15 + 'a';
	else
	if (49U > k) {
		static const char other[9] = "@[\\]^_`{";
		return other[k - 41U];
	} else
		return EOF;
}

void
ECMA48InputEncoder::WriteLatin1OrCSISequence(char csi_char, char ord_char, unsigned m)
{
	if (0U != m)
		WriteCSISequence(1U,m,csi_char);
	else
		WriteLatin1Character(ord_char);
}

void 
ECMA48InputEncoder::WriteSS3OrLatin1(bool shift, char shifted_char, char ord_char)
{
	if (shift)
		WriteSS3Character(shifted_char);
	else
		WriteLatin1Character(ord_char);
}

void 
ECMA48InputEncoder::WriteSS3OrCSISequence(bool shift, char c, unsigned m)
{
	if (shift && 0U == m)
		WriteSS3Character(c);
	else
		WriteCSISequence(1U,m,c);
}

void 
ECMA48InputEncoder::WriteSS3OrCSISequence(bool shift, char shifted_char, char csi_char, unsigned m)
{
	if (shift && 0U == m)
		WriteSS3Character(shifted_char);
	else
		WriteCSISequence(1U,m,csi_char);
}

void 
ECMA48InputEncoder::WriteSS3OrCSISequenceAmbig(bool shift, char c, unsigned m)
{
	if (shift && 0U == m)
		WriteSS3Character(c);
	else
		WriteCSISequenceAmbig(1U,m,c);
}

void 
ECMA48InputEncoder::WriteSS3OrDECFNK(bool shift, char c, unsigned decfnk, unsigned m)
{
	if (shift && 0U == m)
		WriteSS3Character(c);
	else
		WriteDECFNK(decfnk, m);
}

void 
ECMA48InputEncoder::WriteOrdOrDECFNK(bool ord_mode, char ord_char, unsigned decfnk, unsigned m)
{
	if (ord_mode && 0U == m)
		WriteLatin1Character(ord_char);
	else
		WriteDECFNK(decfnk, m);
}

void
ECMA48InputEncoder::WriteOrdOrDECFNKAmbig(bool ord_mode, char ord_char, unsigned decfnk, unsigned m)
{
	if (ord_mode && 0U == m)
		WriteLatin1Character(ord_char);
	else
		WriteDECFNKAmbig(decfnk,m);
}

void
ECMA48InputEncoder::WriteBackspaceOrDEL(uint8_t m)
{
	if (0U != (m & ~INPUT_MODIFIER_CONTROL))
		WriteXTermModKey(8U,m);
	else
		// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
		WriteRawCharacter(backspace_is_bs ^ !!(INPUT_MODIFIER_CONTROL & m) ? BS : DEL);
}

void
ECMA48InputEncoder::WriteESCOrFS(uint8_t m)
{
	if (0U != m)
		WriteXTermModKey(27U,m);
	else
		WriteRawCharacter(escape_is_fs ? FS : ESC); 	// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
}

void
ECMA48InputEncoder::WriteReturnEnter(uint8_t m)
{
	if (0 != (~INPUT_MODIFIER_CONTROL & m))
		WriteXTermModKey(13U,m);
	else
		WriteRawCharacter((INPUT_MODIFIER_CONTROL & m) ? '\x0A' : '\x0D'); 	// We can bypass the ASCII check and UTF-8 encoding as we guarantee ASCII.
}

void 
ECMA48InputEncoder::SetPasting(const bool p)
{
	if (p == pasting) return;
	pasting = p;
	if (send_paste)
		WriteDECFNKAmbig(pasting ? 200 : 201, 0);
}

void 
ECMA48InputEncoder::WriteFunctionKeyDECVT(uint16_t k, uint8_t m)
{
	if (!send_dec_function_keys)
		WriteFNK(k,m);
	else switch (k) {
		case 1:		WriteDECFNKAmbig(11U,m); break;
		case 2:		WriteDECFNKAmbig(12U,m); break;
		case 3:		WriteDECFNKAmbig(13U,m); break;
		case 4:		WriteDECFNKAmbig(14U,m); break;
		case 5:		WriteDECFNKAmbig(15U,m); break;
		case 6:		WriteDECFNKAmbig(17U,m); break;
		case 7:		WriteDECFNKAmbig(18U,m); break;
		case 8:		WriteDECFNKAmbig(19U,m); break;
		case 9:		WriteDECFNKAmbig(20U,m); break;
		case 10:	WriteDECFNKAmbig(21U,m); break;
		case 11:	WriteDECFNKAmbig(23U,m); break;
		case 12:	WriteDECFNKAmbig(24U,m); break;
		case 13:	WriteDECFNKAmbig(25U,m); break;
		case 14:	WriteDECFNKAmbig(26U,m); break;
		case 15:	WriteDECFNKAmbig(28U,m); break;
		case 16:	WriteDECFNKAmbig(29U,m); break;
		case 17:	WriteDECFNKAmbig(31U,m); break;
		case 18:	WriteDECFNKAmbig(32U,m); break;
		case 19:	WriteDECFNKAmbig(33U,m); break;
		case 20:	WriteDECFNKAmbig(34U,m); break;
		case 21:	WriteDECFNKAmbig(35U,m); break;
		case 22:	WriteDECFNKAmbig(36U,m); break;
		case 23:	WriteDECFNKAmbig(42U,m); break;	// XTerm extension
		case 24:	WriteDECFNKAmbig(43U,m); break;	// XTerm extension
		default:	WriteFNK(k,m); break;		// our extension (sic): Fallback to using the standard control sequence.
	}
}

// Since the SCO folding of modifiers into function key numbers will have been done by realizers, we don't do any folding ourselves.
void 
ECMA48InputEncoder::WriteFunctionKeySCOConsole(uint16_t k, uint8_t m)
{
	if (!send_sco_function_keys)
		WriteFunctionKeyDECVT(k,m);
	else {
		const int c(SCOFNKCharacter(k));
		if (EOF == c)
			// our extension (sic): Fallback to using the standard control sequence.
			WriteFNK(k, m);
		else
			WriteSCOConsoleFNK(m, c);
	}
}

// See the console-terminal-emulator(1) manual for the mess that is libteken and function keys.
// All of the modifier folding is done by keyboard maps in realizers, fortunately, so we just have to cope with the crazy DECFNK switching.
// F1 to F4 do not occur on the FreeBSD kernel terminal emulator (although F5 does, contrary to DEC VT behaviour), only PF1 to PF4.
// But we extend teken's change to F1 to F4 as well anyway, as the SCO codes are very unhelpful when debugging with synthetic input events.
void 
ECMA48InputEncoder::WriteFunctionKeyTeken(uint16_t k, uint8_t m)
{
	if (!send_teken_function_keys || (13U > k && 0U == m))
		WriteFunctionKeyDECVT(k, m);
	else
		WriteFunctionKeySCOConsole(k, m);
}

void 
ECMA48InputEncoder::WriteFunctionKey(uint16_t k, uint8_t m)
{
	SetPasting(false);
	switch (emulation) {
#if 0	// Actually unreachable, and generates a warning.
		default:		[[clang::fallthrough]];
#endif
		case XTERM_PC:		[[clang::fallthrough]];
		case LINUX_CONSOLE:	[[clang::fallthrough]];
		case NETBSD_CONSOLE:	[[clang::fallthrough]];
		case DECVT:		return WriteFunctionKeyDECVT(k, m);
		case TEKEN:		return WriteFunctionKeyTeken(k, m);
		case SCO_CONSOLE:	return WriteFunctionKeySCOConsole(k, m);
	}
}

void 
ECMA48InputEncoder::WriteDECVTKeypadKey(bool app_mode, char app_char, unsigned decfnk, unsigned m)
{
	if (app_mode)
		// Strict DEC VT conformance means that modifiers are ignored in application modes.
		WriteSS3Character(app_char);
	else
		WriteDECFNKAmbig(decfnk, m);
}

void 
ECMA48InputEncoder::WriteDECVTKeypadKey(bool app_mode, char app_char, char csi_char, unsigned m)
{
	if (app_mode)
		// Strict DEC VT conformance means that modifiers are ignored in application modes.
		WriteSS3Character(app_char);
	else
		WriteCSISequenceAmbig(1U, m, csi_char);
}

void 
ECMA48InputEncoder::WriteDECVTKeypadKey(bool app_mode, char app_char, char csi_char, unsigned decfnk, unsigned m)
{
	if (app_mode)
		// Strict DEC VT conformance means that modifiers are ignored in application modes.
		WriteSS3Character(app_char);
	else
	if (INPUT_MODIFIER_LEVEL3 & m)
		WriteDECFNKAmbig(decfnk, m);
	else
		WriteCSISequenceAmbig(1U, m, csi_char);
}

void 
ECMA48InputEncoder::WriteXTermPCKeypadKey(bool app_mode, char app_char, unsigned decfnk, unsigned m)
{
	if (app_mode && (INPUT_MODIFIER_LEVEL2 & m))
		WriteBrokenSS3Sequence(m, app_char);
	else
		WriteDECFNKAmbig(decfnk, m);
}

void 
ECMA48InputEncoder::WriteXTermPCKeypadKey(bool app_mode, char app_char, char csi_char, unsigned m)
{
	if (app_mode && (INPUT_MODIFIER_LEVEL2 & m))
		WriteBrokenSS3Sequence(m, app_char);
	else
		WriteCSISequenceAmbig(1U, m, csi_char);
}

void 
ECMA48InputEncoder::WriteXTermPCKeypadKey(bool app_mode, char app_char, char csi_char, unsigned decfnk, unsigned m)
{
	if (app_mode && (INPUT_MODIFIER_LEVEL2 & m))
		WriteBrokenSS3Sequence(m, app_char);
	else
	if (INPUT_MODIFIER_LEVEL3 & m)
		WriteDECFNKAmbig(decfnk, m);
	else
		WriteCSISequenceAmbig(1U, m, csi_char);
}

void 
ECMA48InputEncoder::WriteTekenKeypadKey(bool app_mode, char app_char, char csi_char, unsigned decfnk, unsigned m)
{
	if (0 != m)
		WriteDECFNK(decfnk, m);
	else
		WriteSS3OrCSISequence(app_mode, app_char, csi_char, m);
}

void 
ECMA48InputEncoder::WriteExtendedKeyCommonExtensions(uint16_t k, unsigned m)
{
	switch (k) {
 		case EXTENDED_KEY_PAD_00:		WriteRawCharacters("00"); break;
 		case EXTENDED_KEY_PAD_000:		WriteRawCharacters("000"); break;
 		case EXTENDED_KEY_PAD_THOUSANDS_SEP:	WriteRawCharacter(','); break;
 		case EXTENDED_KEY_PAD_DECIMAL_SEP:	WriteRawCharacter('.'); break;
 		case EXTENDED_KEY_PAD_CURRENCY_UNIT:	WriteUnicodeCharacter(0x0000A4); break;
 		case EXTENDED_KEY_PAD_CURRENCY_SUB:	WriteUnicodeCharacter(0x0000A2); break;
 		case EXTENDED_KEY_PAD_OPEN_BRACKET:	WriteRawCharacter('['); break;
 		case EXTENDED_KEY_PAD_CLOSE_BRACKET:	WriteRawCharacter(']'); break;
 		case EXTENDED_KEY_PAD_OPEN_BRACE:	WriteRawCharacter('{'); break;
 		case EXTENDED_KEY_PAD_CLOSE_BRACE:	WriteRawCharacter('}'); break;
 		case EXTENDED_KEY_PAD_A:		WriteRawCharacter('A'); break;
 		case EXTENDED_KEY_PAD_B:		WriteRawCharacter('B'); break;
 		case EXTENDED_KEY_PAD_C:		WriteRawCharacter('C'); break;
 		case EXTENDED_KEY_PAD_D:		WriteRawCharacter('D'); break;
 		case EXTENDED_KEY_PAD_E:		WriteRawCharacter('E'); break;
 		case EXTENDED_KEY_PAD_F:		WriteRawCharacter('F'); break;
 		case EXTENDED_KEY_PAD_XOR:		WriteUnicodeCharacter(0x0022BB); break;
 		case EXTENDED_KEY_PAD_CARET:		WriteRawCharacter('^'); break;
 		case EXTENDED_KEY_PAD_PERCENT:		WriteRawCharacter('%'); break;
 		case EXTENDED_KEY_PAD_LESS:		WriteRawCharacter('<'); break;
 		case EXTENDED_KEY_PAD_GREATER:		WriteRawCharacter('>'); break;
 		case EXTENDED_KEY_PAD_AND:		WriteUnicodeCharacter(0x002227); break;
 		case EXTENDED_KEY_PAD_ANDAND:		WriteRawCharacters("&&"); break;
 		case EXTENDED_KEY_PAD_OR:		WriteUnicodeCharacter(0x002228); break;
 		case EXTENDED_KEY_PAD_OROR:		WriteRawCharacters("||"); break;
		case EXTENDED_KEY_PAD_COLON:		WriteRawCharacter(':'); break;
 		case EXTENDED_KEY_PAD_HASH:		WriteRawCharacter('#'); break;
 		case EXTENDED_KEY_PAD_SPACE:		WriteRawCharacter(' '); break;
 		case EXTENDED_KEY_PAD_AT:		WriteRawCharacter('@'); break;
 		case EXTENDED_KEY_PAD_EXCLAMATION:	WriteRawCharacter('!'); break;
 		case EXTENDED_KEY_PAD_SIGN:		WriteUnicodeCharacter(0x0000B1); break;
		default:
			if ((k & 0x0F00) != 0x0F00)	// Mask out our non-USB extensions.
				WriteUSBExtendedFNK(k, m);
			else
				std::fprintf(stderr, "WARNING: %s: %08" PRIx32 "\n", "Unknown extended key", k);
			break;
	}
}

// These are the sequences defined by the DEC VT510 and VT520 programmers' references.
// Most termcaps/terminfos name this "vt220", or "vt420", or "vt520".
//
// * There is no way to transmit modifier state with "application mode" keys.
void 
ECMA48InputEncoder::WriteExtendedKeyDECVT(uint16_t k, unsigned m)
{
	//                                                                         flag                        ss3 csi raw fnk mod
	switch (k) {
	// The calculator keypad
		case EXTENDED_KEY_PAD_TAB:		WriteLatin1OrCSISequence  (                                'I',TAB,    m); break;
		case EXTENDED_KEY_PAD_ENTER:		if (calculator_application_mode)
							WriteSS3Character         (                            'M'              );
							else WriteReturnEnter(m);
							break;
		case EXTENDED_KEY_PAD_F1:		WriteSS3Character         (                            'P'              ); break;
		case EXTENDED_KEY_PAD_F2:		WriteSS3Character         (                            'Q'              ); break;
		case EXTENDED_KEY_PAD_F3:		WriteSS3Character         (                            'R'              ); break;
		case EXTENDED_KEY_PAD_F4:		WriteSS3Character         (                            'S'              ); break;
		case EXTENDED_KEY_PAD_F5:		WriteSS3Character         (                            'T'              ); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_EQUALS:		WriteSS3OrLatin1	  (calculator_application_mode,'X',    '='      ); break;
		case EXTENDED_KEY_PAD_ASTERISK:		WriteSS3OrLatin1	  (calculator_application_mode,'j',    '*'      ); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteSS3OrLatin1	  (calculator_application_mode,'k',    '+'      ); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteSS3OrLatin1	  (calculator_application_mode,'l',    ','      ); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteSS3OrLatin1	  (calculator_application_mode,'m',    '-'      ); break;
		case EXTENDED_KEY_PAD_DELETE:		WriteDECVTKeypadKey	  (calculator_application_mode,'n',         3U,m); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteSS3OrLatin1	  (calculator_application_mode,'o',    '/'      ); break;
		case EXTENDED_KEY_PAD_INSERT:		WriteDECVTKeypadKey	  (calculator_application_mode,'p',         2U,m); break;
		case EXTENDED_KEY_PAD_END:		WriteDECVTKeypadKey	  (calculator_application_mode,'q','F',        m); break;
		case EXTENDED_KEY_PAD_DOWN:		WriteDECVTKeypadKey	  (calculator_application_mode,'r','B',     8U,m); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	WriteDECVTKeypadKey	  (calculator_application_mode,'s',         6U,m); break;
		case EXTENDED_KEY_PAD_LEFT:		WriteDECVTKeypadKey	  (calculator_application_mode,'t','D',     7U,m); break;
		case EXTENDED_KEY_PAD_CENTRE:		WriteDECVTKeypadKey	  (calculator_application_mode,'u','E',        m); break;
		case EXTENDED_KEY_PAD_RIGHT:		WriteDECVTKeypadKey	  (calculator_application_mode,'v','C',    10U,m); break;
		case EXTENDED_KEY_PAD_HOME:		WriteDECVTKeypadKey	  (calculator_application_mode,'w','H',        m); break;
		case EXTENDED_KEY_PAD_UP:		WriteDECVTKeypadKey	  (calculator_application_mode,'x','A',     9U,m); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		WriteDECVTKeypadKey	  (calculator_application_mode,'y',         5U,m); break;
	// The cursor/editing keypad
		case EXTENDED_KEY_SCROLL_UP:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_UP_ARROW:		WriteDECVTKeypadKey       (cursor_application_mode,    'A','A',     9U,m); break;
		case EXTENDED_KEY_SCROLL_DOWN:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_DOWN_ARROW:		WriteDECVTKeypadKey       (cursor_application_mode,    'B','B',     8U,m); break;
		case EXTENDED_KEY_RIGHT_ARROW:		WriteDECVTKeypadKey       (cursor_application_mode,    'C','C',    10U,m); break;
		case EXTENDED_KEY_LEFT_ARROW:		WriteDECVTKeypadKey       (cursor_application_mode,    'D','D',     7U,m); break;
		case EXTENDED_KEY_CENTRE:		WriteDECVTKeypadKey       (false,                      'E','E',        m); break;
		case EXTENDED_KEY_END:			WriteDECVTKeypadKey       (false,                      'F','F',        m); break;
		case EXTENDED_KEY_HOME:			WriteDECVTKeypadKey       (false,                      'H','H',        m); break;
		case EXTENDED_KEY_TAB:			WriteLatin1OrCSISequence  (                                'I',TAB,    m); break;
		case EXTENDED_KEY_BACKTAB:		WriteDECVTKeypadKey       (false,                      'Z','Z',        m); break;
		case EXTENDED_KEY_FIND:			WriteDECFNKAmbig          (                                         1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_INSERT:		WriteDECFNKAmbig          (                                         2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_DELETE:		WriteOrdOrDECFNKAmbig     (delete_is_del,                      DEL, 3U,m); break;
		case EXTENDED_KEY_SELECT:		WriteDECFNKAmbig          (                                         4U,m); break;
		case EXTENDED_KEY_PREVIOUS:		// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_UP:		WriteDECFNKAmbig          (                                         5U,m); break;
		case EXTENDED_KEY_NEXT:			// This is not a DEC VT key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_DOWN:		WriteDECFNKAmbig          (                                         6U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(m); break;
		case EXTENDED_KEY_ESCAPE:		WriteESCOrFS(m); break;
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
ECMA48InputEncoder::WriteExtendedKeyXTermPC(uint16_t k, unsigned m)
{
	//                                                                         flag                        ss3 csi raw fnk mod
	switch (k) {
	// The calculator keypad
		case EXTENDED_KEY_PAD_TAB:		WriteXTermPCKeypadKey     (calculator_application_mode,'I','I',        m); break;
		case EXTENDED_KEY_PAD_ENTER:		if (calculator_application_mode)
							WriteXTermPCKeypadKey     (calculator_application_mode,'M','M',        m);
							else WriteReturnEnter(m);
							break;
		case EXTENDED_KEY_PAD_F1:		WriteXTermPCKeypadKey     (calculator_application_mode,'P','P',        m); break;
		case EXTENDED_KEY_PAD_F2:		WriteXTermPCKeypadKey     (calculator_application_mode,'Q','Q',        m); break;
		case EXTENDED_KEY_PAD_F3:		WriteXTermPCKeypadKey     (calculator_application_mode,'R','R',        m); break;
		case EXTENDED_KEY_PAD_F4:		WriteXTermPCKeypadKey     (calculator_application_mode,'S','S',        m); break;
		case EXTENDED_KEY_PAD_F5:		WriteXTermPCKeypadKey     (calculator_application_mode,'T','T',        m); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	// This is not an XTerm PC key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_EQUALS:		WriteSS3OrLatin1          (calculator_application_mode,'X',    '='      ); break;
		case EXTENDED_KEY_PAD_ASTERISK:		WriteSS3OrLatin1          (calculator_application_mode,'j',    '*'      ); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteSS3OrLatin1          (calculator_application_mode,'k',    '+'      ); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteSS3OrLatin1          (calculator_application_mode,'l',    ','      ); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteSS3OrLatin1          (calculator_application_mode,'m',    '-'      ); break;
		case EXTENDED_KEY_PAD_DELETE:		WriteXTermPCKeypadKey     (calculator_application_mode,'n',         3U,m); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteSS3OrLatin1          (calculator_application_mode,'o',    '/'      ); break;
		case EXTENDED_KEY_PAD_INSERT:		WriteXTermPCKeypadKey     (calculator_application_mode,'p',         2U,m); break;
		case EXTENDED_KEY_PAD_END:		WriteXTermPCKeypadKey     (calculator_application_mode,'q','F',        m); break;
		case EXTENDED_KEY_PAD_DOWN:		WriteXTermPCKeypadKey     (calculator_application_mode,'r','B',     8U,m); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	WriteXTermPCKeypadKey     (calculator_application_mode,'s',         6U,m); break;
		case EXTENDED_KEY_PAD_LEFT:		WriteXTermPCKeypadKey     (calculator_application_mode,'t','D',     7U,m); break;
		case EXTENDED_KEY_PAD_CENTRE:		WriteXTermPCKeypadKey     (calculator_application_mode,'u','E',        m); break;
		case EXTENDED_KEY_PAD_RIGHT:		WriteXTermPCKeypadKey     (calculator_application_mode,'v','C',    10U,m); break;
		case EXTENDED_KEY_PAD_HOME:		WriteXTermPCKeypadKey     (calculator_application_mode,'w','H',        m); break;
		case EXTENDED_KEY_PAD_UP:		WriteXTermPCKeypadKey     (calculator_application_mode,'x','A',     9U,m); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		WriteXTermPCKeypadKey     (calculator_application_mode,'y',         5U,m); break;
	// The cursor/editing keypad
		case EXTENDED_KEY_SCROLL_UP:		// This is not an XTerm PC key, but we make it equivalent to:
		case EXTENDED_KEY_UP_ARROW:		WriteSS3OrCSISequenceAmbig(cursor_application_mode,    'A',            m); break;
		case EXTENDED_KEY_SCROLL_DOWN:		// This is not an XTerm PC key, but we make it equivalent to:
		case EXTENDED_KEY_DOWN_ARROW:		WriteSS3OrCSISequenceAmbig(cursor_application_mode,    'B',            m); break;
		case EXTENDED_KEY_RIGHT_ARROW:		WriteSS3OrCSISequenceAmbig(cursor_application_mode,    'C',            m); break;
		case EXTENDED_KEY_LEFT_ARROW:		WriteSS3OrCSISequenceAmbig(cursor_application_mode,    'D',            m); break;
		case EXTENDED_KEY_CENTRE:		WriteSS3OrCSISequenceAmbig(cursor_application_mode,    'E',            m); break;
		case EXTENDED_KEY_END:			WriteSS3OrCSISequenceAmbig(cursor_application_mode,    'F',            m); break;
		case EXTENDED_KEY_HOME:			WriteSS3OrCSISequenceAmbig(cursor_application_mode,    'H',            m); break;
		case EXTENDED_KEY_TAB:			WriteLatin1OrCSISequence  (                                'I',TAB,    m); break;
		case EXTENDED_KEY_BACKTAB:		WriteSS3OrCSISequence     (cursor_application_mode,    'Z',            m); break;
		case EXTENDED_KEY_FIND:			WriteDECFNKAmbig          (                                         1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not an XTerm PC key, but we make it equivalent to:
		case EXTENDED_KEY_INSERT:		WriteDECFNKAmbig          (                                         2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not an XTerm PC key, but we make it equivalent to:
		case EXTENDED_KEY_DELETE:		WriteOrdOrDECFNKAmbig     (delete_is_del,                      DEL, 3U,m); break;
		case EXTENDED_KEY_SELECT:		WriteDECFNKAmbig          (                                         4U,m); break;
		case EXTENDED_KEY_PREVIOUS:		// This is not an XTerm PC key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_UP:		WriteDECFNKAmbig          (                                         5U,m); break;
		case EXTENDED_KEY_NEXT:			// This is not an XTerm PC key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_DOWN:		WriteDECFNKAmbig          (                                         6U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(m); break;
		case EXTENDED_KEY_ESCAPE:		WriteESCOrFS(m); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteReturnEnter(m); break;
		default:				WriteExtendedKeyCommonExtensions(k,m); break;
	}
}

// These are the sequences defined by libteken, as used by the FreeBSD kernel since version 9.0.
// The termcap/terminfo name is "teken" or "teken-16color".
//
// As an extension to libteken:
//  * The application cursor and calculator keypad modes are respected.
//    Because this is an extension anyway, we do not write XTerm PC's broken SS3 sequences, prefer DECFNK to SS3 when any modifiers are present, and write DECFNK and other CSI sequences in ISO 8613-6/ITU T.416 form.
//  * Cursor and calculator keypad keys with modifiers send the modifiers.
//    Because this is an extension anyway, we write DECFNK and other CSI sequences in ISO 8613-6/ITU T.416 form.
void 
ECMA48InputEncoder::WriteExtendedKeyTeken(uint16_t k, unsigned m)
{
	//                                                                         flag                        ss3 csi raw fnk mod
	switch (k) {
	// The calculator keypad
		case EXTENDED_KEY_PAD_ENTER:		if (calculator_application_mode)
							WriteSS3OrCSISequence     (calculator_application_mode,'M',            m);
							else WriteReturnEnter(m);
							break;
		case EXTENDED_KEY_PAD_F1:		WriteSS3OrCSISequence     (true                       ,'P',            m); break;
		case EXTENDED_KEY_PAD_F2:		WriteSS3OrCSISequence     (true                       ,'Q',            m); break;
		case EXTENDED_KEY_PAD_F3:		WriteSS3OrCSISequence     (true                       ,'R',            m); break;
		case EXTENDED_KEY_PAD_F4:		WriteSS3OrCSISequence     (true                       ,'S',            m); break;
		case EXTENDED_KEY_PAD_F5:		WriteSS3OrCSISequence     (true                       ,'T',            m); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_EQUALS:		WriteSS3OrLatin1	  (calculator_application_mode,'X',    '='      ); break;
		case EXTENDED_KEY_PAD_ASTERISK:		WriteSS3OrLatin1	  (calculator_application_mode,'j',    '*'      ); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteSS3OrLatin1	  (calculator_application_mode,'k',    '+'      ); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteSS3OrLatin1	  (calculator_application_mode,'l',    ','      ); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteSS3OrLatin1	  (calculator_application_mode,'m',    '-'      ); break;
		case EXTENDED_KEY_PAD_DELETE:		WriteSS3OrDECFNK	  (calculator_application_mode,'n',         3U,m); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteSS3OrLatin1	  (calculator_application_mode,'o',    '/'      ); break;
		case EXTENDED_KEY_PAD_INSERT:		WriteSS3OrDECFNK	  (calculator_application_mode,'p',         2U,m); break;
		case EXTENDED_KEY_PAD_END:		WriteSS3OrCSISequence	  (calculator_application_mode,'q','F',        m); break;
		case EXTENDED_KEY_PAD_DOWN:		WriteTekenKeypadKey	  (calculator_application_mode,'r','B',     8U,m); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	WriteSS3OrDECFNK	  (calculator_application_mode,'s',         6U,m); break;
		case EXTENDED_KEY_PAD_LEFT:		WriteTekenKeypadKey	  (calculator_application_mode,'t','D',     7U,m); break;
		case EXTENDED_KEY_PAD_CENTRE:		WriteSS3OrCSISequence	  (calculator_application_mode,'u','E',        m); break;
		case EXTENDED_KEY_PAD_RIGHT:		WriteTekenKeypadKey	  (calculator_application_mode,'v','C',    10U,m); break;
		case EXTENDED_KEY_PAD_HOME:		WriteSS3OrCSISequence	  (calculator_application_mode,'w','H',        m); break;
		case EXTENDED_KEY_PAD_UP:		WriteTekenKeypadKey	  (calculator_application_mode,'x','A',     9U,m); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		WriteSS3OrDECFNK	  (calculator_application_mode,'y',         5U,m); break;
	// The cursor/editing keypad
		case EXTENDED_KEY_SCROLL_UP:		// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_UP_ARROW:		WriteSS3OrCSISequence     (cursor_application_mode,        'A',        m); break;
		case EXTENDED_KEY_SCROLL_DOWN:		// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_DOWN_ARROW:		WriteSS3OrCSISequence     (cursor_application_mode,        'B',        m); break;
		case EXTENDED_KEY_RIGHT_ARROW:		WriteSS3OrCSISequence     (cursor_application_mode,        'C',        m); break;
		case EXTENDED_KEY_LEFT_ARROW:		WriteSS3OrCSISequence     (cursor_application_mode,        'D',        m); break;
		case EXTENDED_KEY_CENTRE:		WriteSS3OrCSISequence     (cursor_application_mode,        'E',        m); break;
		case EXTENDED_KEY_END:			WriteSS3OrCSISequence     (cursor_application_mode,        'F',        m); break;
		case EXTENDED_KEY_HOME:			WriteSS3OrCSISequence     (cursor_application_mode,        'H',        m); break;
		case EXTENDED_KEY_PAD_TAB:		// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_TAB:			WriteLatin1OrCSISequence  (                                'I',TAB,    m); break;
		case EXTENDED_KEY_BACKTAB:		WriteSS3OrCSISequence     (cursor_application_mode,    'Z',            m); break;
		case EXTENDED_KEY_FIND:			WriteDECFNK               (                                         1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_INSERT:		WriteDECFNK               (                                         2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_DELETE:		WriteOrdOrDECFNK          (delete_is_del,                  DEL,     3U,m); break;
		case EXTENDED_KEY_SELECT:		WriteDECFNK               (                                         4U,m); break;
		case EXTENDED_KEY_PREVIOUS:		// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_UP:		WriteDECFNK               (                                         5U,m); break;
		case EXTENDED_KEY_NEXT:			// This is not a teken key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_DOWN:		WriteDECFNK               (                                         6U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(m); break;
		case EXTENDED_KEY_ESCAPE:		WriteESCOrFS(m); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteReturnEnter(m); break;
		default:				WriteExtendedKeyCommonExtensions(k,m); break;
	}
}

// This is what a DEC VT520 produces in SCO Console mode.
// It also matches Teken's CONS25 mode, and the older cons25 FreeBSD console.
void 
ECMA48InputEncoder::WriteExtendedKeySCOConsole(uint16_t k, unsigned m)
{
	switch (k) {
		case EXTENDED_KEY_PAD_EQUALS_AS400:	// This is not a SCO console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_EQUALS:		WriteSS3OrLatin1   (calculator_application_mode,'X',    '='     ); break;
		case EXTENDED_KEY_PAD_ASTERISK:		WriteRawCharacter('*'); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteRawCharacter('+'); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteRawCharacter(','); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteRawCharacter('-'); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteRawCharacter('/'); break;
		case EXTENDED_KEY_SCROLL_UP:		// This is not a SCO console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_UP:		// this, which the SCO console does not distinguish from:
		case EXTENDED_KEY_UP_ARROW:		WriteCSISequenceAmbig(1U,m,'A'); break;
		case EXTENDED_KEY_SCROLL_DOWN:		// This is not a SCO console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DOWN:		// this, which the SCO console does not distinguish from:
		case EXTENDED_KEY_DOWN_ARROW:		WriteCSISequenceAmbig(1U,m,'B'); break;
		case EXTENDED_KEY_PAD_RIGHT:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_RIGHT_ARROW:		WriteCSISequenceAmbig(1U,m,'C'); break;
		case EXTENDED_KEY_PAD_LEFT:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_LEFT_ARROW:		WriteCSISequenceAmbig(1U,m,'D'); break;
		case EXTENDED_KEY_PAD_CENTRE:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_CENTRE:		WriteCSISequenceAmbig(1U,m,'E'); break;
		case EXTENDED_KEY_PAD_END:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_END:			WriteCSISequenceAmbig(1U,m,'F'); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	// The SCO console does not distinguish this from:
		case EXTENDED_KEY_PAGE_DOWN:		WriteCSISequenceAmbig(1U,m,'G'); break;
		case EXTENDED_KEY_PAD_HOME:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_HOME:			WriteCSISequenceAmbig(1U,m,'H'); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		// The SCO console does not distinguish this from:
		case EXTENDED_KEY_PAGE_UP:		WriteCSISequenceAmbig(1U,m,'I'); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a SCO console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_INSERT:		// this, which the SCO console does not distinguish from:
		case EXTENDED_KEY_INSERT:		WriteCSISequenceAmbig(1U,m,'L'); break;
		case EXTENDED_KEY_PAD_F1:		WriteCSISequenceAmbig(1U,m,'M'); break;
		case EXTENDED_KEY_PAD_F2:		WriteCSISequenceAmbig(1U,m,'N'); break;
		case EXTENDED_KEY_PAD_F3:		WriteCSISequenceAmbig(1U,m,'O'); break;
		case EXTENDED_KEY_PAD_F4:		WriteCSISequenceAmbig(1U,m,'P'); break;
		case EXTENDED_KEY_PAD_F5:		WriteCSISequenceAmbig(1U,m,'Q'); break;
		case EXTENDED_KEY_PAD_TAB:		// This is not a SCO console key, but we make it equivalent to:
		case EXTENDED_KEY_TAB:			WriteLatin1OrCSISequence  (                                'I',TAB,    m); break;
		case EXTENDED_KEY_BACKTAB:		WriteCSISequenceAmbig(1U,m,'Z'); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(m); break;
		case EXTENDED_KEY_ESCAPE:		WriteESCOrFS(m); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteReturnEnter(m); break;
		case EXTENDED_KEY_PAD_ENTER:		WriteReturnEnter(m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a SCO console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_DELETE:		// this, which the SCO console does not distinguish from:
		case EXTENDED_KEY_DELETE:		WriteRawCharacter(DEL); break;
		default:				WriteExtendedKeyCommonExtensions(k,m); break;
	}
}

// This is what the Linux kernel terminal emulator produces.
// The termcap/terminfo name is "linux" or (only when using a corrected terminfo) "linux-16color".
//
// Some important differences from a DEC VT with a PC keyboard and the PC Layout:
// * HOME/END are made into DECFNK instead of CSI sequences.
//
// As an extension to the Linux kernel terminal emulator:
//  * The application cursor and calculator keypad modes are respected.
//    Because this is an extension anyway, we do not write XTerm PC's broken SS3 sequences, prefer DECFNK to SS3 when any modifiers are present, and write DECFNK and other CSI sequences in ISO 8613-6/ITU T.416 form.
//  * Cursor and calculator keypad keys with modifiers send the modifiers.
//    Because this is an extension anyway, we write DECFNK and other CSI sequences in ISO 8613-6/ITU T.416 form.
void
ECMA48InputEncoder::WriteExtendedKeyLinuxKVT(uint16_t k, unsigned m)
{
	//                                                                         flag                        ss3 csi raw fnk mod
	switch (k) {
	// The calculator keypad
		case EXTENDED_KEY_PAD_ENTER:		if (calculator_application_mode)
							WriteSS3OrCSISequence     (calculator_application_mode,'M',            m);
							else WriteReturnEnter(m);
							break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	// This is not a Linux KVT key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_EQUALS:		WriteSS3OrLatin1          (calculator_application_mode,'X',    '='      ); break;
		case EXTENDED_KEY_PAD_F1:		WriteLinuxKVTFNK(m,'A'); break;
		case EXTENDED_KEY_PAD_F2:		WriteLinuxKVTFNK(m,'B'); break;
		case EXTENDED_KEY_PAD_F3:		WriteLinuxKVTFNK(m,'C'); break;
		case EXTENDED_KEY_PAD_F4:		WriteLinuxKVTFNK(m,'D'); break;
		case EXTENDED_KEY_PAD_F5:		WriteLinuxKVTFNK(m,'E'); break;
		case EXTENDED_KEY_PAD_ASTERISK:		WriteSS3OrLatin1          (calculator_application_mode,'j',    '*'      ); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteSS3OrLatin1          (calculator_application_mode,'k',    '+'      ); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteSS3OrLatin1          (calculator_application_mode,'l',    ','      ); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteSS3OrLatin1          (calculator_application_mode,'m',    '-'      ); break;
		case EXTENDED_KEY_PAD_DELETE:		if (calculator_application_mode)
							WriteSS3OrLatin1          (calculator_application_mode,'n',    DEL      );
							else
							WriteOrdOrDECFNK          (delete_is_del,                      DEL, 3U,m);
							break;
		case EXTENDED_KEY_PAD_SLASH:		WriteSS3OrLatin1          (calculator_application_mode,'o',    '/'      ); break;
		case EXTENDED_KEY_PAD_INSERT:		WriteSS3OrDECFNK          (calculator_application_mode,'p',         2U,m); break;
		case EXTENDED_KEY_PAD_END:		WriteSS3OrCSISequence     (calculator_application_mode,'q','F',        m); break;
		case EXTENDED_KEY_PAD_DOWN:		WriteTekenKeypadKey       (calculator_application_mode,'r','B',     8U,m); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	WriteSS3OrDECFNK          (calculator_application_mode,'s',         6U,m); break;
		case EXTENDED_KEY_PAD_LEFT:		WriteTekenKeypadKey       (calculator_application_mode,'t','D',     7U,m); break;
		case EXTENDED_KEY_PAD_CENTRE:		WriteSS3OrCSISequence     (calculator_application_mode,'u','G',        m); break;
		case EXTENDED_KEY_PAD_RIGHT:		WriteTekenKeypadKey       (calculator_application_mode,'v','C',    10U,m); break;
		case EXTENDED_KEY_PAD_HOME:		WriteSS3OrCSISequence     (calculator_application_mode,'w','H',        m); break;
		case EXTENDED_KEY_PAD_UP:		WriteTekenKeypadKey       (calculator_application_mode,'x','A',     9U,m); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		WriteSS3OrDECFNK          (calculator_application_mode,'y',         5U,m); break;
	// The cursor/editing keypad
		case EXTENDED_KEY_SCROLL_UP:		// This is not a Linux KVT key, but we make it equivalent to:
		case EXTENDED_KEY_UP_ARROW:		WriteSS3OrCSISequence     (cursor_application_mode,        'A',        m); break;
		case EXTENDED_KEY_SCROLL_DOWN:		// This is not a Linux KVT key, but we make it equivalent to:
		case EXTENDED_KEY_DOWN_ARROW:		WriteSS3OrCSISequence     (cursor_application_mode,        'B',        m); break;
		case EXTENDED_KEY_RIGHT_ARROW:		WriteSS3OrCSISequence     (cursor_application_mode,        'C',        m); break;
		case EXTENDED_KEY_LEFT_ARROW:		WriteSS3OrCSISequence     (cursor_application_mode,        'D',        m); break;
		case EXTENDED_KEY_CENTRE:		WriteCSISequenceAmbig(1U,m,'G'); break;
		case EXTENDED_KEY_PAD_TAB:		// This is not a Linux KVT key, but we make it equivalent to:
		case EXTENDED_KEY_TAB:			WriteLatin1OrCSISequence  (                                'I',TAB,    m); break;
		case EXTENDED_KEY_BACKTAB:		WriteSS3OrCSISequence     (cursor_application_mode,    'Z',            m); break;
		case EXTENDED_KEY_HOME:			// The Linux KVT erroneously makes this the same as:
		case EXTENDED_KEY_FIND:			WriteDECFNKAmbig          (                                         1U,m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a Linux KVT key, but we make it equivalent to:
		case EXTENDED_KEY_INSERT:		WriteDECFNKAmbig          (                                         2U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a Linux KVT key, but we make it equivalent to:
		case EXTENDED_KEY_DELETE:		WriteOrdOrDECFNK          (delete_is_del,                      DEL, 3U,m); break;
		case EXTENDED_KEY_END:			// The Linux KVT erroneously makes this the same as:
		case EXTENDED_KEY_SELECT:		WriteDECFNKAmbig          (                                         4U,m); break;
		case EXTENDED_KEY_PREVIOUS:		// This is not a Linux KVT key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_UP:		WriteDECFNKAmbig          (                                         5U,m); break;
		case EXTENDED_KEY_NEXT:			// This is not a Linux KVT key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_DOWN:		WriteDECFNKAmbig          (                                         6U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(m); break;
		case EXTENDED_KEY_ESCAPE:		WriteESCOrFS(m); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteReturnEnter(m); break;
		default:				WriteExtendedKeyCommonExtensions(k,m); break;
	}
}

// This is what the NetBSD kernel terminal emulator produces in "vt100" mode.
// The termcap/terminfo name is "netbsd6" (not "pccon" or "wsvtXX").
//
// Some important differences from a DEC VT with a PC keyboard and the PC Layout:
// * PF1 to PF5 send DECFNK.
//
// As an extension to the NetBSD kernel terminal emulator:
//  * The application cursor and calculator keypad modes are respected.
//    Because this is an extension anyway, we do not write XTerm PC's broken SS3 sequences, prefer DECFNK to SS3 when any modifiers are present, and write DECFNK and other CSI sequences in ISO 8613-6/ITU T.416 form.
//  * Cursor and calculator keypad keys with modifiers send the modifiers.
//    Because this is an extension anyway, we write DECFNK and other CSI sequences in ISO 8613-6/ITU T.416 form.
void 
ECMA48InputEncoder::WriteExtendedKeyNetBSDConsole(uint16_t k, unsigned m)
{
	//                                                                         flag                        ss3 csi raw fnk mod
	switch (k) {
	// The calculator keypad
		case EXTENDED_KEY_PAD_ENTER:		if (calculator_application_mode)
							WriteSS3OrCSISequence     (calculator_application_mode,'M',            m);
							else WriteReturnEnter(m);
							break;
		case EXTENDED_KEY_PAD_F1:		WriteSS3OrDECFNK          (calculator_application_mode,'P',        11U,m); break;
		case EXTENDED_KEY_PAD_F2:		WriteSS3OrDECFNK          (calculator_application_mode,'Q',        12U,m); break;
		case EXTENDED_KEY_PAD_F3:		WriteSS3OrDECFNK          (calculator_application_mode,'R',        13U,m); break;
		case EXTENDED_KEY_PAD_F4:		WriteSS3OrDECFNK          (calculator_application_mode,'S',        14U,m); break;
		case EXTENDED_KEY_PAD_F5:		WriteSS3OrDECFNK          (calculator_application_mode,'T',        15U,m); break;
		case EXTENDED_KEY_PAD_EQUALS_AS400:	// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_EQUALS:		WriteSS3OrLatin1          (calculator_application_mode,'X',    '='      ); break;
		case EXTENDED_KEY_PAD_ASTERISK:		WriteSS3OrLatin1          (calculator_application_mode,'j',    '*'      ); break;
		case EXTENDED_KEY_PAD_PLUS:		WriteSS3OrLatin1          (calculator_application_mode,'k',    '+'      ); break;
		case EXTENDED_KEY_PAD_COMMA:		WriteSS3OrLatin1          (calculator_application_mode,'l',    ','      ); break;
		case EXTENDED_KEY_PAD_MINUS:		WriteSS3OrLatin1          (calculator_application_mode,'m',    '-'      ); break;
		case EXTENDED_KEY_PAD_DELETE:		WriteSS3OrDECFNK          (calculator_application_mode,'n',         3U,m); break;
		case EXTENDED_KEY_PAD_SLASH:		WriteSS3OrLatin1          (calculator_application_mode,'o',    '/'      ); break;
		case EXTENDED_KEY_PAD_PAGE_UP:		WriteSS3OrDECFNK          (calculator_application_mode,'y',         5U,m); break;
		case EXTENDED_KEY_PAD_END:		WriteSS3OrCSISequence     (calculator_application_mode,'q','F',        m); break;
		case EXTENDED_KEY_PAD_DOWN:		WriteTekenKeypadKey       (calculator_application_mode,'r','B',     8U,m); break;
		case EXTENDED_KEY_PAD_PAGE_DOWN:	WriteSS3OrDECFNK          (calculator_application_mode,'s',         6U,m); break;
		case EXTENDED_KEY_PAD_LEFT:		WriteTekenKeypadKey       (calculator_application_mode,'t','D',     7U,m); break;
		case EXTENDED_KEY_PAD_CENTRE:		WriteSS3OrCSISequence     (calculator_application_mode,'u','E',        m); break;
		case EXTENDED_KEY_PAD_RIGHT:		WriteTekenKeypadKey       (calculator_application_mode,'v','C',    10U,m); break;
		case EXTENDED_KEY_PAD_HOME:		WriteSS3OrCSISequence     (calculator_application_mode,'w','H',        m); break;
		case EXTENDED_KEY_PAD_UP:		WriteTekenKeypadKey       (calculator_application_mode,'x','A',     9U,m); break;
	// The cursor/editing keypad
		case EXTENDED_KEY_SCROLL_UP:		// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_UP_ARROW:		WriteSS3OrCSISequence     (cursor_application_mode,        'A',        m); break;
		case EXTENDED_KEY_SCROLL_DOWN:		// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_DOWN_ARROW:		WriteSS3OrCSISequence     (cursor_application_mode,        'B',        m); break;
		case EXTENDED_KEY_RIGHT_ARROW:		WriteSS3OrCSISequence     (cursor_application_mode,        'C',        m); break;
		case EXTENDED_KEY_LEFT_ARROW:		WriteSS3OrCSISequence     (cursor_application_mode,        'D',        m); break;
		case EXTENDED_KEY_CENTRE:		WriteSS3OrCSISequence     (cursor_application_mode,        'E',        m); break;
		case EXTENDED_KEY_INS_CHAR:		// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_PAD_INSERT:		// this, which the NetBSD console does not distinguish from:
		case EXTENDED_KEY_INSERT:		WriteCSISequenceAmbig(1U,m,'L'); break;
		case EXTENDED_KEY_PAD_TAB:		// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_TAB:			WriteLatin1OrCSISequence  (                                'I',TAB,    m); break;
		case EXTENDED_KEY_BACKTAB:		WriteSS3OrCSISequence     (cursor_application_mode,    'Z',            m); break;
		case EXTENDED_KEY_FIND:			WriteDECFNKAmbig          (                                         1U,m); break;
		case EXTENDED_KEY_DEL_CHAR:		// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_DELETE:		WriteOrdOrDECFNK          (delete_is_del,                      DEL, 3U,m); break;
		case EXTENDED_KEY_SELECT:		WriteDECFNKAmbig          (                                         4U,m); break;
		case EXTENDED_KEY_PREVIOUS:		// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_UP:		WriteDECFNKAmbig          (                                         5U,m); break;
		case EXTENDED_KEY_NEXT:			// This is not a NetBSD console key, but we make it equivalent to:
		case EXTENDED_KEY_PAGE_DOWN:		WriteDECFNKAmbig          (                                         6U,m); break;
		case EXTENDED_KEY_HOME:			WriteDECFNKAmbig          (                                         7U,m); break;
		case EXTENDED_KEY_END:			WriteDECFNKAmbig          (                                         8U,m); break;
		case EXTENDED_KEY_BACKSPACE:		WriteBackspaceOrDEL(m); break;
		case EXTENDED_KEY_ESCAPE:		WriteESCOrFS(m); break;
		case EXTENDED_KEY_RETURN_OR_ENTER:	WriteReturnEnter(m); break;
		default:				WriteExtendedKeyCommonExtensions(k,m); break;
	}
}

void 
ECMA48InputEncoder::WriteExtendedKey(uint16_t k, uint8_t m)
{
	SetPasting(false);
	switch (emulation) {
#if 0	// Actually unreachable, and generates a warning.
		default:		[[clang::fallthrough]];
#endif
		case DECVT:		return WriteExtendedKeyDECVT(k, m);
		case SCO_CONSOLE:	return WriteExtendedKeySCOConsole(k, m);
		case LINUX_CONSOLE:	return WriteExtendedKeyLinuxKVT(k, m);
		case NETBSD_CONSOLE:	return WriteExtendedKeyNetBSDConsole(k, m);
		case XTERM_PC:		return WriteExtendedKeyXTermPC(k, m);
		case TEKEN:		return WriteExtendedKeyTeken(k, m);
	}
}

void 
ECMA48InputEncoder::WriteConsumerKey(uint16_t k, uint8_t m)
{
	SetPasting(false);
	WriteUSBConsumerFNK(k, m);
}

void 
ECMA48InputEncoder::WriteWheelMotion(uint8_t w, int8_t o, uint8_t m) 
{
	SetPasting(false);
	// DEC Locator reports use buttons 4 and upwards, because the original DEC Locator specification defined 4 actual mouse buttons.
	while (0 != o) {
		if (0 > o) {
			++o;
			const unsigned decbutton(4U + 2U * w);
			WriteXTermMouseWheel(w, true, m);
			WriteDECLocatorReportButton(decbutton, true);
			WriteXTermMouseWheel(w, false, m);
			WriteDECLocatorReportButton(decbutton, false);
		}
		if (0 < o) {
			--o;
			const unsigned decbutton(5U + 2U * w);
			WriteXTermMouseWheel(w, true, m);
			WriteDECLocatorReportButton(decbutton, true);
			WriteXTermMouseWheel(w, false, m);
			WriteDECLocatorReportButton(decbutton, false);
		}
	}
}

void 
ECMA48InputEncoder::WriteXTermMouse(
	unsigned flags, 
	bool pressed,
	uint8_t modifiers
) {
	if (INPUT_MODIFIER_LEVEL2 & modifiers)
		flags |= 4U;
	if (INPUT_MODIFIER_CONTROL & modifiers)
		flags |= 16U;
	if (INPUT_MODIFIER_SUPER & modifiers)
		flags |= 8U;

	WriteCSI();
	char b[32];
	const char c(pressed ? 'M' : 'm');
	snprintf(b, sizeof b, "<%u;%u;%u%c", flags, TranslateToDECCoordinates(mouse_column), TranslateToDECCoordinates(mouse_row), c);
	WriteRawCharacters(b);
}

// The horizontal wheel (#1) is an extension to the xterm protocol.
// XTerm wheel reports use buttons 0 to 3 with bit 6 set to indicate a wheel report.
// Bit 5 which would indicate a simultaneous motion is not ever set for wheel events in practice.
void 
ECMA48InputEncoder::WriteXTermMouseButton(
	unsigned button, 
	bool pressed,
	uint8_t modifiers
) {
	if (!send_xterm_mouse) return;
	if (button > 0x02) return;
	if (!send_xterm_mouse_clicks) return;

	const unsigned flags(TranslateToXTermButton(button));

	WriteXTermMouse(flags, pressed, modifiers);
}

void 
ECMA48InputEncoder::WriteXTermMouseMotion(
	uint8_t modifiers
) {
	if (!send_xterm_mouse) return;

	bool pressed(false);
	unsigned flags(32U);
	// We have a best effort attempt at giving a button number.
	// But in reality even XTerm gives the wrong button numbers with motion events.
	// (It seems to save and just use the number of the last button pressed.)
	for (std::size_t button(0U); button < sizeof mouse_buttons/sizeof *mouse_buttons; ++button) {
		if (button > 0x02) {
			flags |= 0x03;
			break;
		}
		if (mouse_buttons[button]) {
			flags |= TranslateToXTermButton(button);
			pressed = true;
			break;
		}
	}
	if (pressed ? !send_xterm_mouse_button_motions : !send_xterm_mouse_nobutton_motions) return;

	WriteXTermMouse(flags, pressed, modifiers);
}

void 
ECMA48InputEncoder::WriteXTermMouseWheel(
	unsigned wheel, 
	bool pressed,
	uint8_t modifiers
) {
	if (!send_xterm_mouse) return;
	if (wheel > 0x03) return;
	if (!send_xterm_mouse_clicks) return;
	// vim cannot cope with button up wheel events.
	if (!pressed) return;

	const unsigned flags(64U | wheel);

	WriteXTermMouse(flags, pressed, modifiers);
}

void 
ECMA48InputEncoder::WriteDECLocatorReport(
	unsigned event,
	unsigned buttons
) {
	for (std::size_t button(0U); button < sizeof mouse_buttons/sizeof *mouse_buttons; ++button) {
		if (button >= CHAR_BIT * sizeof(unsigned)) break;
		if (mouse_buttons[button])
			buttons |= 1U << button;
	}

	WriteCSI();
	char b[32];
	const unsigned int mouse_page(0U);
	snprintf(b, sizeof b, "%u;%u;%u;%u;%u&w", event, buttons, TranslateToDECCoordinates(mouse_row), TranslateToDECCoordinates(mouse_column), mouse_page);
	WriteRawCharacters(b);

	// Turn oneshot mode off.
	// Invalid buttons and suppressed reports don't turn oneshot mode off, because oneshot mode is from the point of view of the client.
	if (2U == send_locator_mode) send_locator_mode = 0U;
}

void 
ECMA48InputEncoder::WriteDECLocatorReportButton(
	unsigned button,
	bool pressed
) {
	if (!send_locator_mode) return;
	if (button >= CHAR_BIT * sizeof(unsigned)) return;
	if (pressed ? !send_locator_press_events : !send_locator_release_events) return;

	unsigned event(0U);
	if (button < 4U)
		event = button * 2U + 2U + (pressed ? 0U : 1U);
	else
		// This is an extension to the DEC protocol.
		event = (button - 4U) * 2U + 12U + (pressed ? 0U : 1U);
	unsigned buttons(1U << button);

	WriteDECLocatorReport(event, buttons);
}

void 
ECMA48InputEncoder::WriteRequestedDECLocatorReport(
) {
	if (!send_locator_mode) return;

	WriteDECLocatorReport(1U, 0U);
}

void 
ECMA48InputEncoder::SetMouseX(uint16_t p, uint8_t m) 
{
	SetPasting(false);
	if (mouse_column != p) {
		mouse_column = p; 
		WriteXTermMouseMotion(m);
		// DEC Locator reports only report button events.
	}
}

void 
ECMA48InputEncoder::SetMouseY(uint16_t p, uint8_t m) 
{ 
	SetPasting(false);
	if (mouse_row != p) {
		mouse_row = p; 
		WriteXTermMouseMotion(m);
		// DEC Locator reports only report button events.
	}
}

void 
ECMA48InputEncoder::SetMouseButton(uint8_t b, bool v, uint8_t m) 
{ 
	if (b >= sizeof mouse_buttons/sizeof *mouse_buttons) return;
	SetPasting(false);
	if (mouse_buttons[b] != v) {
		mouse_buttons[b] = v; 
		WriteXTermMouseButton(b, v, m);
		WriteDECLocatorReportButton(b, v);
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
	WriteRequestedDECLocatorReport();
}

void 
ECMA48InputEncoder::WriteUCS3Character(uint32_t c, bool pasted, bool accelerator)
{
	SetPasting(pasted);
	if (accelerator)
		WriteUnicodeCharacter(ESC);
	WriteUnicodeCharacter(c);
	// Interrupt after any pasted character that could otherwise begin a DECFNK sequence.
	if (ESC == c || CSI == c)
		SetPasting(false);
}

void
ECMA48InputEncoder::HandleMessage(uint32_t b)
{
	switch (b & INPUT_MSG_MASK) {
		case INPUT_MSG_UCS3:	WriteUCS3Character(b & ~INPUT_MSG_MASK, false, false); break;
		case INPUT_MSG_PUCS3:	WriteUCS3Character(b & ~INPUT_MSG_MASK, true, false); break;
		case INPUT_MSG_AUCS3:	WriteUCS3Character(b & ~INPUT_MSG_MASK, false, true); break;
		case INPUT_MSG_CKEY:	WriteConsumerKey((b >> 8U) & 0xFFFF, b & 0xFF); break;
		case INPUT_MSG_EKEY:	WriteExtendedKey((b >> 8U) & 0xFFFF, b & 0xFF); break;
		case INPUT_MSG_FKEY:	WriteFunctionKey((b >> 8U) & 0xFFFF, b & 0xFF); break;
		case INPUT_MSG_XPOS:	SetMouseX((b >> 8U) & 0xFFFF, b & 0xFF); break;
		case INPUT_MSG_YPOS:	SetMouseY((b >> 8U) & 0xFFFF, b & 0xFF); break;
		case INPUT_MSG_WHEEL:	WriteWheelMotion((b >> 16U) & 0xFF, static_cast<int8_t>((b >> 8U) & 0xFF), b & 0xFF); break;
		case INPUT_MSG_BUTTON:	SetMouseButton((b >> 16U) & 0xFF, (b >> 8U) & 0xFF, b & 0xFF); break;
		case INPUT_MSG_SESSION:	break;
		default:
			std::fprintf(stderr, "WARNING: %s: %" PRIx32 "\n", "Unknown input message", b);
			break;
	}
}

void
ECMA48InputEncoder::WriteOutput()
{
	const int l(write(terminal_back_end_fd, output_buffer, output_pending));
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
	virtual void ReadCell(coordinate s, CharacterCell & c);
	virtual void WriteNCells(coordinate p, coordinate n, const CharacterCell & c);
	virtual void ModifyNCells(coordinate s, coordinate n, CharacterCell::attribute_type turnoff, CharacterCell::attribute_type flipon, bool fg_touched, const CharacterCell::colour_type & fg, bool bg_touched, const CharacterCell::colour_type & bg);
	virtual void CopyNCells(coordinate d, coordinate s, coordinate n);
	virtual void ScrollUp(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void ScrollDown(coordinate s, coordinate e, coordinate n, const CharacterCell & c);
	virtual void SetCursorPos(coordinate x, coordinate y);
	virtual void SetCursorType(CursorSprite::glyph_type, CursorSprite::attribute_type);
	virtual void SetPointerType(PointerSprite::attribute_type);
	virtual void SetScreenFlags(ScreenFlags::flag_type);
	virtual void SetSize(coordinate w, coordinate h);
	virtual void SetAltBuffer(bool);
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
MultipleBuffer::ReadCell(coordinate s, CharacterCell & c)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->ReadCell(s, c);
}

void 
MultipleBuffer::WriteNCells(coordinate s, coordinate n, const CharacterCell & c)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->WriteNCells(s, n, c);
}

void
MultipleBuffer::ModifyNCells(coordinate s, coordinate n, CharacterCell::attribute_type turnoff, CharacterCell::attribute_type flipon, bool fg_touched, const CharacterCell::colour_type & fg, bool bg_touched, const CharacterCell::colour_type & bg)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->ModifyNCells(s, n, turnoff, flipon, fg_touched, fg, bg_touched, bg);
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
MultipleBuffer::SetScreenFlags(ScreenFlags::flag_type f)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->SetScreenFlags(f);
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

void
MultipleBuffer::SetAltBuffer(bool f)
{
	for (Buffers::iterator i(buffers.begin()); buffers.end() != i; ++i)
		(*i)->SetAltBuffer(f);
}

/* Signal handling **********************************************************
// **************************************************************************
*/

namespace {

sig_atomic_t shutdown_signalled(false);

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

}

/* Line discipline options **************************************************
// **************************************************************************
*/

namespace {

struct terminal_mode_definition : public popt::integral_definition {
public:
	terminal_mode_definition(char s, const char * l, const char * d, bool & v) : integral_definition(s, l, a, d), value(v) {}
	virtual ~terminal_mode_definition();
protected:
	static const char a[];
	virtual void action(popt::processor &, const char *);
	bool & value;
};

}

const char terminal_mode_definition::a[] = "mode";
terminal_mode_definition::~terminal_mode_definition() {}
void terminal_mode_definition::action(popt::processor & /*proc*/, const char * text)
{
	if (0 == std::strcmp(text, "canonical")
	||  0 == std::strcmp(text, "sane")
	) {
		value = true;
		set = true;
	} else
	if (0 == std::strcmp(text, "non-canonical")
	||  0 == std::strcmp(text, "raw")
	) {
		value = false;
		set = true;
	} else
		throw popt::error(text, "terminal mode specification is not {canonical|sane|non-canonical|raw}");
}

/* Emulation options ********************************************************
// **************************************************************************
*/

namespace {

struct emulation_definition : public popt::simple_named_definition {
public:
	emulation_definition(char s, const char * l, const char * d, ECMA48InputEncoder::Emulation & e, ECMA48InputEncoder::Emulation v) : simple_named_definition(s, l, d), emulation(e), value(v) {}
	virtual void action(popt::processor &);
	virtual ~emulation_definition();
protected:
	ECMA48InputEncoder::Emulation & emulation;
	ECMA48InputEncoder::Emulation value;
};

}

emulation_definition::~emulation_definition() {}
void emulation_definition::action(popt::processor &)
{
	emulation = value;
}

/* Back-end handling ********************************************************
// **************************************************************************
*/

namespace {

enum { PTY_BACK_END_FILENO = 4 };

inline
void
handle_input (
	SoftTerm & emulator,
	int fd,
	int n	///< number of characters available; can be <= 0 erroneously
) {
	do {
		char data_buffer[16384];
		const int l(read(fd, data_buffer, sizeof data_buffer));
		if (0 >= l) break;
		for (int j(0); j < l; ++j)
			emulator.Process(data_buffer[j]);
		if (l >= n) break;
		n -= l;
	} while (n > 0);
}

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
	bool vcsa(false), inverted(false), canonical_mode(true);
	// X terminal emulators choose 80 by 24, for compatibility with real DEC VTs.
	// We choose 80 by 25 because we are, rather, being compatible with the kernel terminal emluators, which have no status lines and default to PC 25 line modes.
	unsigned long columns(80U), rows(25U);

	try {
		emulation_definition linux_option('\0', "linux", "Emulate the Linux virtual console.", emulation, ECMA48InputEncoder::LINUX_CONSOLE);
		emulation_definition sco_option('\0', "sco", "Emulate the SCO virtual console.", emulation, ECMA48InputEncoder::SCO_CONSOLE);
		emulation_definition teken_option('\0', "teken", "Emulate the teken library.", emulation, ECMA48InputEncoder::TEKEN);
		emulation_definition netbsd_option('\0', "netbsd", "Emulate the NetBSD virtual console.", emulation, ECMA48InputEncoder::NETBSD_CONSOLE);
		emulation_definition decvt_option('\0', "decvt", "Emulate the DEC VT.", emulation, ECMA48InputEncoder::DECVT);
		emulation_definition xtermpc_option('\0', "xtermpc", "Emulate a subset of XTerm in Sun/PC mode.", emulation, ECMA48InputEncoder::XTERM_PC);
		popt::definition * emulations_table[] = {
			&linux_option,
			&sco_option,
			&teken_option,
			&netbsd_option,
			&decvt_option,
			&xtermpc_option,
		};
		popt::table_definition emulations_option(sizeof emulations_table/sizeof *emulations_table, emulations_table, "Emulation options");
		popt::unsigned_number_definition rows_option('\0', "rows", "count", "Set the terminal height.", rows, 0 /* parse for base */);
		popt::unsigned_number_definition columns_option('\0', "columns", "count", "Set the terminal width.", columns, 0 /* parse for base */);
		popt::definition * sizes_table[] = {
			&rows_option,
			&columns_option,
		};
		popt::table_definition sizes_option(sizeof sizes_table/sizeof *sizes_table, sizes_table, "Size options");
		popt::bool_definition vcsa_option('\0', "vcsa", "Maintain a vcsa-compatible display buffer.", vcsa);
		popt::bool_definition inverted_option('\0', "inverted", "Begin in inverted mode.", inverted);
		terminal_mode_definition canonical_mode_option('\0', "terminal-mode", "Set the terminal mode.", canonical_mode);
		popt::definition * top_table[] = {
			&emulations_option,
			&sizes_option,
			&vcsa_option,
			&inverted_option,
			&canonical_mode_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{directory}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) die_missing_directory_name(prog, envs);
	const char * dirname(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	const char * tty(envs.query("TTY"));
	if (!tty) die_missing_environment_variable(prog, envs, "TTY");

	const int queue(kqueue());
	if (0 > queue) {
		die_errno(prog, envs, "kqueue");
	}
	std::vector<struct kevent> ip;

	FileDescriptorOwner dir_fd(open_dir_at(AT_FDCWD, dirname));
	if (0 > dir_fd.get()) {
		die_errno(prog, envs, dirname);
	}

	// We need an explicit lock file, because we cannot lock FIFOs.
	FileDescriptorOwner lock_fd(open_lockfile_at(dir_fd.get(), "lock"));
	if (0 > lock_fd.get()) {
		die_errno(prog, envs, dirname, "lock");
	}
	// We are allowed to open the read end of a FIFO in non-blocking mode without having to wait for a writer.
	mkfifoat(dir_fd.get(), "input", 0620);
	InputFIFO input_fifo(open_read_at(dir_fd.get(), "input"));
	if (0 > input_fifo.get()) {
		die_errno(prog, envs, dirname, "input");
	}
	if (0 > fchown(input_fifo.get(), -1, getegid())) {
		die_errno(prog, envs, dirname, "input");
	}
	// We have to keep a client (write) end descriptor open to the input FIFO.
	// Otherwise, the first console client process triggers POLLHUP when it closes its end.
	// Opening the FIFO for read+write isn't standard, although it would work on Linux.
	FileDescriptorOwner input_write_fd(open_writeexisting_at(dir_fd.get(), "input"));
	if (0 > input_write_fd.get()) {
		die_errno(prog, envs, dirname, "input");
	}
	VCSA vbuffer(open_readwritecreate_at(dir_fd.get(), "vcsa", 0640));
	if (0 > vbuffer.get()) {
		die_errno(prog, envs, dirname, "vcsa");
	}
	if (0 > fchown(vbuffer.get(), -1, getegid())) {
		die_errno(prog, envs, dirname, "vcsa");
	}
	UnicodeBuffer ubuffer(open_readwritecreate_at(dir_fd.get(), "display", 0640));
	if (0 > ubuffer.get()) {
		die_errno(prog, envs, dirname, "display");
	}
	if (0 > fchown(ubuffer.get(), -1, getegid())) {
		die_errno(prog, envs, dirname, "display");
	}
	unlinkat(dir_fd.get(), "tty", 0);
	if (0 > linkat(AT_FDCWD, tty, dir_fd.get(), "tty", 0)) {
		if (0 > symlinkat(tty, dir_fd.get(), "tty")) {
			die_errno(prog, envs, tty, dirname);
		}
	}

	append_event(ip, PTY_BACK_END_FILENO, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	append_event(ip, PTY_BACK_END_FILENO, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
	append_event(ip, input_fifo.get(), EVFILT_READ, EV_ADD, 0, 0, nullptr);
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, 0);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);

	ubuffer.WriteBOM();
	MultipleBuffer mbuffer;
	mbuffer.Add(&ubuffer);
	if (vcsa)
		mbuffer.Add(&vbuffer);
	ECMA48InputEncoder input_encoder(PTY_BACK_END_FILENO, emulation);
	// linux and teken get it wrong: SU and SD are window pans, not buffer scrolls.
	const bool pan_is_scroll(ECMA48InputEncoder::TEKEN == emulation || ECMA48InputEncoder::LINUX_CONSOLE == emulation);
	const SoftTerm::Setup setup(columns > 255U ? 255U : columns, rows > 255U ? 255U : rows, inverted, pan_is_scroll);
	SoftTerm emulator(mbuffer, input_encoder, input_encoder, setup);
	{
		termios t;
		// We want slightly different defaults, with UTF-8 input mode on because that's what our input encoder sends, and tostop mode on.
		if (0 <= tcgetattr_nointr(PTY_BACK_END_FILENO, t))
			tcsetattr_nointr(PTY_BACK_END_FILENO, TCSADRAIN, canonical_mode ? enable_canonical_software_processing(t) : disable_tostop(disable_canonical_software_processing(t)));
	}

	bool hangup(false);
	while (!shutdown_signalled && !hangup) {
		append_event(ip, PTY_BACK_END_FILENO, EVFILT_WRITE, input_encoder.OutputAvailable() ? EV_ENABLE : EV_DISABLE, 0, 0, nullptr);
		append_event(ip, input_fifo.get(), EVFILT_READ, input_encoder.HasInputSpace() ? EV_ENABLE : EV_DISABLE, 0, 0, nullptr);

		struct kevent p[128];
		const int rc(kevent(queue, ip.data(), ip.size(), p, sizeof p/sizeof *p, nullptr));
		ip.clear();

		if (0 > rc) {
			if (EINTR == errno) continue;
			die_errno(prog, envs, "kevent");
		}

		bool fifo_hangup(false);

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_SIGNAL:
					handle_signal(e.ident);
					break;
				case EVFILT_READ:
					if (PTY_BACK_END_FILENO == e.ident) {
						handle_input(emulator, static_cast<int>(e.ident), e.data);
						if (EV_EOF & e.flags)
							hangup = true;
					} else
					if (input_fifo.get() == static_cast<int>(e.ident)) {
						input_fifo.ReadInput(e.data);
						if (EV_EOF & e.flags)
							fifo_hangup = true;
					}
					break;
				case EVFILT_WRITE:
					if (PTY_BACK_END_FILENO == e.ident)
						input_encoder.WriteOutput();
					break;
			}
		}

		while (input_fifo.HasMessage() && input_encoder.HasInputSpace())
			input_encoder.HandleMessage(input_fifo.PullMessage());
	}

	unlinkat(dir_fd.get(), "tty", 0);
	throw EXIT_SUCCESS;
}
