/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_VIRTUALTERMINALBACKEND_H)
#define INCLUDE_VIRTUALTERMINALBACKEND_H

#include <cstdio>
#include <vector>
#include "FileStar.h"
#include "FileDescriptorOwner.h"
#include "CharacterCell.h"

class VirtualTerminalBackEnd 
{
public:
	typedef unsigned short coordinate;
	VirtualTerminalBackEnd(const char * dirname, FILE * buffer_file, int input_fd);
	~VirtualTerminalBackEnd();

	const char * query_dir_name() const { return dir_name; }
	int query_buffer_fd() const { return fileno(buffer_file.operator FILE *()); }
	int query_input_fd() const { return input_fd.get(); }
	FILE * query_buffer_file() const { return buffer_file; }
	coordinate query_h() const { return h; }
	coordinate query_w() const { return w; }
	coordinate query_visible_y() const { return visible_y; }
	coordinate query_visible_x() const { return visible_x; }
	coordinate query_visible_h() const { return visible_h; }
	coordinate query_visible_w() const { return visible_w; }
	coordinate query_cursor_y() const { return cursor_y; }
	coordinate query_cursor_x() const { return cursor_x; }
	CursorSprite::glyph_type query_cursor_glyph() const { return cursor_glyph; }
	CursorSprite::attribute_type query_cursor_attributes() const { return cursor_attributes; }
	PointerSprite::attribute_type query_pointer_attributes() const { return pointer_attributes; }
	bool query_reload_needed() const { return reload_needed; }
	void set_reload_needed() { reload_needed = true; }
	void reload();
	void set_visible_area(coordinate h, coordinate w);
	CharacterCell & at(coordinate y, coordinate x) { return cells[static_cast<std::size_t>(y) * w + x]; }
	void WriteInputMessage(uint32_t);
	bool MessageAvailable() const { return message_pending > 0U; }
	void FlushMessages();
	bool query_polling_for_write() const { return polling_for_write; }
	void set_polling_for_write(bool v) { polling_for_write = v; }

protected:
	VirtualTerminalBackEnd(const VirtualTerminalBackEnd & c);
	enum { CELL_LENGTH = 16U, HEADER_LENGTH = 16U };

	void move_cursor(coordinate y, coordinate x);
	void resize(coordinate, coordinate);
	void keep_visible_area_in_buffer();
	void keep_visible_area_around_cursor();

	const char * dir_name;
	char display_stdio_buffer[128U * 1024U];
	FileStar buffer_file;
	FileDescriptorOwner input_fd;
	char message_buffer[4096];
	std::size_t message_pending;
	bool polling_for_write, reload_needed;
	unsigned short cursor_y, cursor_x, visible_y, visible_x, visible_h, visible_w, h, w;
	CursorSprite::glyph_type cursor_glyph;
	CursorSprite::attribute_type cursor_attributes;
	PointerSprite::attribute_type pointer_attributes;
	std::vector<CharacterCell> cells;
};

#endif
