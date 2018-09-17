/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstdio>
#include <vector>
#include <cstring>
#include <unistd.h>
#include "FileStar.h"
#include "FileDescriptorOwner.h"
#include "CharacterCell.h"
#include "VirtualTerminalBackEnd.h"

VirtualTerminalBackEnd::VirtualTerminalBackEnd(
	const char * dirname, 
	FILE * b, 
	int d
) :
	dir_name(dirname),
	buffer_file(b),
	input_fd(d),
	message_pending(0U),
	polling_for_write(false),
	reload_needed(true),
	cursor_y(0U),
	cursor_x(0U),
	visible_y(0U),
	visible_x(0U),
	visible_h(0U),
	visible_w(0U),
	h(0U),
	w(0U),
	cursor_glyph(CursorSprite::BLOCK),
	cursor_attributes(CursorSprite::VISIBLE),
	pointer_attributes(0),
	cells()
{
	std::setvbuf(buffer_file, display_stdio_buffer, _IOFBF, sizeof display_stdio_buffer);
}

VirtualTerminalBackEnd::~VirtualTerminalBackEnd()
{
}

void 
VirtualTerminalBackEnd::move_cursor(
	coordinate y, 
	coordinate x
) { 
	if (y >= h) y = h - 1;
	if (x >= w) x = w - 1;
	cursor_y = y; 
	cursor_x = x; 
}

void 
VirtualTerminalBackEnd::resize(
	coordinate new_h, 
	coordinate new_w
) {
	if (h == new_h && w == new_w) return;
	h = new_h;
	w = new_w;
	const std::size_t s(static_cast<std::size_t>(h) * w);
	if (cells.size() == s) return;
	cells.resize(s);
}

inline
void 
VirtualTerminalBackEnd::keep_visible_area_in_buffer()
{
	if (visible_y + visible_h > h) visible_y = h - visible_h;
	if (visible_x + visible_w > w) visible_x = w - visible_w;
}

inline
void 
VirtualTerminalBackEnd::keep_visible_area_around_cursor()
{
	// When programs repaint the screen the cursor is instantaneously all over the place, leading to the window scrolling all over the shop.
	// But some programs, like vim, make the cursor invisible during the repaint in order to reduce cursor flicker.
	// We take advantage of this by only scrolling the screen to include the cursor position if the cursor is actually visible.
	if (CursorSprite::VISIBLE & cursor_attributes) {
		// The window includes the cursor position.
		if (visible_y > cursor_y || visible_h < 1U) visible_y = cursor_y; else if (visible_y + visible_h <= cursor_y) visible_y = cursor_y - visible_h + 1;
		if (visible_x > cursor_x || visible_w < 1U) visible_x = cursor_x; else if (visible_x + visible_w <= cursor_x) visible_x = cursor_x - visible_w + 1;
	}
}

void 
VirtualTerminalBackEnd::set_visible_area(
	coordinate new_h, 
	coordinate new_w
) {
	if (new_h > h) { visible_h = h; } else { visible_h = new_h; }
	if (new_w > w) { visible_w = w; } else { visible_w = new_w; }
	keep_visible_area_in_buffer();
	keep_visible_area_around_cursor();
}

/// \brief Pull the display buffer from file into the memory buffer, but don't output anything.
void
VirtualTerminalBackEnd::reload () 
{
	// The stdio buffers may well be out of synch, so we need to reset them.
#if defined(__LINUX__) || defined(__linux__)
	std::fflush(buffer_file);
#endif
	std::rewind(buffer_file);
	uint8_t header0[4] = { 0, 0, 0, 0 };
	std::fread(header0, sizeof header0, 1U, buffer_file);
	uint16_t header1[4] = { 0, 0, 0, 0 };
	std::fread(header1, sizeof header1, 1U, buffer_file);
	uint8_t header2[4] = { 0, 0, 0, 0 };
	std::fread(header2, sizeof header2, 1U, buffer_file);

	// Don't fseek() if we can avoid it; it causes duplicate VERY LARGE reads to re-fill the stdio buffer.
	if (HEADER_LENGTH != ftello(buffer_file))
		std::fseek(buffer_file, HEADER_LENGTH, SEEK_SET);

	resize(header1[1], header1[0]);
	move_cursor(header1[3], header1[2]);
	cursor_glyph = static_cast<CursorSprite::glyph_type>(header2[0]);
	cursor_attributes = header2[1];
	pointer_attributes = header2[2];

	for (unsigned row(0); row < h; ++row) {
		for (unsigned col(0); col < w; ++col) {
			unsigned char b[CELL_LENGTH] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			std::fread(b, sizeof b, 1U, buffer_file);
			uint32_t wc;
			std::memcpy(&wc, &b[8], 4);
			const CharacterCell::attribute_type a((static_cast<unsigned short>(b[13]) << 8U) + b[12]);
			const CharacterCell::colour_type fg(b[0], b[1], b[2], b[3]);
			const CharacterCell::colour_type bg(b[4], b[5], b[6], b[7]);
			CharacterCell cc(wc, a, fg, bg);
			at(row, col) = cc;
		}
	}

	reload_needed = false;
}

void 
VirtualTerminalBackEnd::WriteInputMessage(uint32_t m)
{
	if (sizeof m > (sizeof message_buffer - message_pending)) return;
	std::memmove(message_buffer + message_pending, &m, sizeof m);
	message_pending += sizeof m;
}
void
VirtualTerminalBackEnd::FlushMessages()
{
	const ssize_t l(write(input_fd.get(), message_buffer, message_pending));
	if (l > 0) {
		std::memmove(message_buffer, message_buffer + l, message_pending - l);
		message_pending -= l;
	}
}
