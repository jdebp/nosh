/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#define _XOPEN_SOURCE_EXTENDED
#include <vector>
#include <memory>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <clocale>
#include <cerrno>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/event.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#if defined(__LINUX__) || defined(__linux__)
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/input.h>
#include <endian.h>
#else
#include <sys/consio.h>
#include <sys/endian.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "CharacterCell.h"
#include "InputMessage.h"
#include "FileDescriptorOwner.h"
#include "FramebufferIO.h"
#include "KeyboardIO.h"
#include "GraphicsInterface.h"
#include "vtfont.h"
#include "kbdmap.h"
#include "kbdmap_utils.h"
#include "Monospace16x16Font.h"
#include "CompositeFont.h"
#include "UnicodeClassification.h"
#include <term.h>

/* Signal handling **********************************************************
// **************************************************************************
*/

static sig_atomic_t window_resized(false), terminate_signalled(false), interrupt_signalled(false), hangup_signalled(false), usr1_signalled(false), usr2_signalled(false), update_needed(false);

static
void
handle_signal (
	int signo
) {
	switch (signo) {
		case SIGWINCH:	window_resized = true; break;
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
		case SIGUSR1:	usr1_signalled = true; break;
		case SIGUSR2:	usr2_signalled = true; break;
	}
}

/* Loadable fonts ***********************************************************
// **************************************************************************
*/

static inline
int
pread(int fd, bsd_vtfont_header & header, off_t o)
{
	const int rc(pread(fd, &header, sizeof header, o));
	if (0 <= rc) {
		header.glyphs = htobe32(header.glyphs);
		for (unsigned i(0U); i < 4U; ++i) header.map_lengths[i] = htobe32(header.map_lengths[i]);
	}
	return rc;
}

static inline
int
pread(int fd, bsd_vtfont_map_entry & me, off_t o)
{
	const int rc(pread(fd, &me, sizeof me, o));
	if (0 <= rc) {
		me.character = htobe32(me.character);
		me.glyph = htobe16(me.glyph);
		me.count = htobe16(me.count);
	}
	return rc;
}

static inline
bool
good ( const bsd_vtfont_header & header )
{
	return 0 == std::memcmp(header.magic, "VFNT0002", sizeof header.magic);
}

static inline
bool
appropriate ( const bsd_vtfont_header & header )
{
	return 8U <= header.width && 16U >= header.width && 8U <= header.height && 16U >= header.height;
}

static inline
uint32_t
skipped_entries ( const bsd_vtfont_header & header, unsigned int n )
{
	uint32_t r(0U);
	for (unsigned int i(0U); i < n; ++i) r += header.map_lengths[i];
	return r;
}

static inline
uint32_t
entries ( const bsd_vtfont_header & header, unsigned int n )
{
	return header.map_lengths[n];
}

static inline
void
LoadFont (
	const char * prog,
	CombinedFont & font,
	CombinedFont::Font::Weight weight,
	CombinedFont::Font::Slant slant,
	unsigned vtfont_index,
	const char * name
) {
	FileDescriptorOwner font_fd(open_read_at(AT_FDCWD, name));
	if (0 > font_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
		throw EXIT_FAILURE;
	}
	struct stat t;
	if (0 > fstat(font_fd.get(), &t)) {
bad_file:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
		throw EXIT_FAILURE;
	}
	bsd_vtfont_header header;
	if (0 > pread(font_fd.get(), header, 0U)) goto bad_file;
	if (good(header)) {
		if (appropriate(header)) {
			if (CombinedFont::FileFont * f = font.AddFileFont(font_fd.release(), weight, slant, header.height, header.width)) {
				bsd_vtfont_map_entry me;
				const off_t start(f->GlyphOffset(header.glyphs) + sizeof me * skipped_entries(header, vtfont_index));
				for (unsigned c(0U); c < entries(header, vtfont_index); ++c) {
					if (0 > pread(f->get(), me, start + c * sizeof me)) goto bad_file;
					f->AddMapping(me.character, me.glyph, me.count + 1U);
				}
			}
		} else {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, "Not an appropriately sized font");
			throw EXIT_FAILURE;
		}
	} else {
		void * const base(mmap(0, t.st_size, PROT_READ, MAP_SHARED, font_fd.get(), 0));
		if (MAP_FAILED == base) goto bad_file;
		if (CombinedFont::MemoryMappedFont * f = font.AddMemoryMappedFont(weight, slant, 8U, 8U, base, t.st_size, 0U))
			f->AddMapping(0x00000000, 0U, t.st_size / 8U);
	}
}

/* Keyboard layouts *********************************************************
// **************************************************************************
*/

typedef kbdmap_entry KeyboardMap[KBDMAP_ROWS][KBDMAP_COLS];

static inline
void
LoadKeyMap (
	const char * prog,
	KeyboardMap & map,
	const char * name
) {
	FILE * const f(std::fopen(name, "r"));
	if (!f) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
		throw EXIT_FAILURE;
	}
	kbdmap_entry * p(&map[0][0]), * const e(p + sizeof map/sizeof *p);
	while (!std::feof(f)) {
		if (p >= e) break;
		uint32_t v[24] = { 0 };
		std::fread(v, sizeof v/sizeof *v, sizeof *v, f);
		const kbdmap_entry o = { 
			be32toh(v[0]), 
			{ 
				be32toh(v[ 8]), 
				be32toh(v[ 9]), 
				be32toh(v[10]), 
				be32toh(v[11]), 
				be32toh(v[12]), 
				be32toh(v[13]), 
				be32toh(v[14]), 
				be32toh(v[15]),
				be32toh(v[16]),
				be32toh(v[17]),
				be32toh(v[18]),
				be32toh(v[19]),
				be32toh(v[20]),
				be32toh(v[21]),
				be32toh(v[22]),
				be32toh(v[23]) 
			} 
		};
		*p++ = o;
	}
	std::fclose(f);
}

/* Character cell comparison ************************************************
// **************************************************************************
*/

static inline
void
invert (
	CharacterCell::colour_type & c
) {
	c.red = ~c.red;
	c.green = ~c.green;
	c.blue = ~c.blue;
}

static inline
bool
operator != (
	const CharacterCell::colour_type & a,
	const CharacterCell::colour_type & b
) {
	return a.alpha != b.alpha || a.red != b.red || a.green != b.green || a.blue != b.blue;
}

static inline
bool
operator != (
	const CharacterCell & a,
	const CharacterCell & b
) {
	return a.character != b.character || a.attributes != b.attributes || a.foreground != b.foreground || a.background != b.background;
}

/* Keyboard shift state *****************************************************
// **************************************************************************
*/

class KeyboardModifierState
{
public:
	KeyboardModifierState();

#if !defined(__LINUX__) && !defined(__linux__)
	bool is_pressed(uint8_t code) const { return pressed[code]; }
#endif
	uint8_t modifiers() const;
	uint8_t nolevel_nogroup_noctrl_modifiers() const;
	std::size_t shiftable_index() const;
	std::size_t capsable_index() const;
	std::size_t numable_index() const;
	std::size_t funcable_index() const;
	void event(uint16_t, uint8_t, uint32_t);
	void unlatch() { latch_state = 0U; }

#if !defined(__LINUX__) && !defined(__linux__)
	void set_pressed(uint8_t code, bool v) { pressed[code] = v; }
#endif

protected:
	static uint32_t bit(unsigned i) { return 1U << i; }
#if !defined(__LINUX__) && !defined(__linux__)
	bool pressed[256];	///< used for determining auto-repeat keypresses
#endif
	uint32_t shift_state, latch_state, lock_state;

	bool any(uint32_t) const;
	bool locked_or_latched(uint32_t) const;
	bool level2_lock() const;
	bool level3_lock() const;
	bool caps_lock() const;
	bool num_lock() const;
	bool level2() const;
	bool level3() const;
	bool group2() const;
	bool control() const;
	bool alt() const;
	bool super() const;
};

inline
KeyboardModifierState::KeyboardModifierState() : 
	shift_state(0U), 
	latch_state(0U), 
	lock_state(0U)
{
#if !defined(__LINUX__) && !defined(__linux__)
	for (unsigned i(0U); i < sizeof pressed/sizeof *pressed; ++i)
		pressed[i] = false;
#endif
}

inline
bool 
KeyboardModifierState::any(uint32_t bits) const
{
	return bits & (shift_state|latch_state|lock_state);
}

inline
bool 
KeyboardModifierState::locked_or_latched(uint32_t bits) const
{
	return bits & (latch_state|lock_state);
}

inline
bool 
KeyboardModifierState::level2() const
{
	return any(bit(KBDMAP_MODIFIER_1ST_LEVEL2)|bit(KBDMAP_MODIFIER_2ND_LEVEL2));
}

inline
bool 
KeyboardModifierState::level3() const
{
	return any(bit(KBDMAP_MODIFIER_1ST_LEVEL3)|bit(KBDMAP_MODIFIER_2ND_LEVEL3));
}

inline
bool 
KeyboardModifierState::group2() const
{
	return any(bit(KBDMAP_MODIFIER_1ST_GROUP2)|bit(KBDMAP_MODIFIER_2ND_GROUP2));
}

inline
bool 
KeyboardModifierState::control() const
{
	return any(bit(KBDMAP_MODIFIER_1ST_CONTROL)|bit(KBDMAP_MODIFIER_2ND_CONTROL));
}

inline
bool 
KeyboardModifierState::alt() const
{
	return any(bit(KBDMAP_MODIFIER_1ST_ALT)|bit(KBDMAP_MODIFIER_2ND_ALT));
}

inline
bool 
KeyboardModifierState::super() const
{
	return any(bit(KBDMAP_MODIFIER_1ST_SUPER)|bit(KBDMAP_MODIFIER_2ND_SUPER));
}

inline
bool 
KeyboardModifierState::level2_lock() const
{
	return locked_or_latched(bit(KBDMAP_MODIFIER_LEVEL2));
}

inline
bool 
KeyboardModifierState::level3_lock() const
{
	return locked_or_latched(bit(KBDMAP_MODIFIER_LEVEL3));
}

inline
bool 
KeyboardModifierState::num_lock() const
{
	return locked_or_latched(bit(KBDMAP_MODIFIER_NUM));
}

inline
bool 
KeyboardModifierState::caps_lock() const
{
	return locked_or_latched(bit(KBDMAP_MODIFIER_CAPS));
}

uint8_t 
KeyboardModifierState::modifiers() const
{
	return 
		(control() ? INPUT_MODIFIER_CONTROL : 0) |
		(level2_lock()||level2() ? INPUT_MODIFIER_LEVEL2 : 0) |
		(alt()||level3_lock()||level3() ? INPUT_MODIFIER_LEVEL3 : 0) |
		(group2() ? INPUT_MODIFIER_GROUP2 : 0) |
		(super() ? INPUT_MODIFIER_SUPER : 0) ;
}

uint8_t 
KeyboardModifierState::nolevel_nogroup_noctrl_modifiers() const
{
	return 
		(super() ? INPUT_MODIFIER_SUPER : 0) ;
}

inline
std::size_t 
KeyboardModifierState::shiftable_index() const
{
	return (level2_lock() || level2() ? 1U : 0U) + (control() ? 2U : 0) + (level3_lock() || level3() ? 4U : 0) + (group2() ? 8U : 0);
}

inline
std::size_t 
KeyboardModifierState::capsable_index() const
{
	const bool lock(caps_lock() || level2_lock());
	return (lock ^ level2() ? 1U : 0U) + (control() ? 2U : 0) + (level3_lock() || level3() ? 4U : 0) + (group2() ? 8U : 0);
}

inline
std::size_t 
KeyboardModifierState::numable_index() const
{
	return (num_lock() ^ level2() ? 1U : 0U) + (control() ? 2U : 0) + (level3_lock() || level3() ? 4U : 0) + (group2() ? 8U : 0);
}

inline
std::size_t 
KeyboardModifierState::funcable_index() const
{
	return (level2_lock() || level2() ? 1U : 0U) + (control() ? 2U : 0) + (alt() ? 4U : 0) + (group2() ? 8U : 0);
}

inline
void
KeyboardModifierState::event(
	uint16_t k,
	uint8_t cmd,
	uint32_t v
) {
	const uint32_t bits(bit(k));
	switch (cmd) {
		case KBDMAP_MODIFIER_CMD_MOMENTARY:
			if (v > 0U)
				shift_state |= bits;
			else
				shift_state &= ~bits;
			break;
		case KBDMAP_MODIFIER_CMD_LATCH:
			if (v > 0U)
				latch_state |= bits;
			break;
		case KBDMAP_MODIFIER_CMD_LOCK:
			if (1U == v)
				lock_state ^= bits;
			break;
	}
	if (1U == v) {
		if (bits & (bit(KBDMAP_MODIFIER_1ST_LEVEL2)|bit(KBDMAP_MODIFIER_2ND_LEVEL2)))
			lock_state &= ~bit(KBDMAP_MODIFIER_LEVEL2);
	}
}

/* Window compositing and change buffering **********************************
// **************************************************************************
*/

class Compositor
{
public:
	typedef unsigned short coordinate;

	Compositor(coordinate h, coordinate w);
	~Compositor();
	coordinate query_h() const { return h; }
	coordinate query_w() const { return w; }
	void repaint_new_to_cur();
	void poke(coordinate y, coordinate x, const CharacterCell & c);
	void move(coordinate y, coordinate x);
	bool is_marked(coordinate y, coordinate x);

protected:
	class DirtiableCell : public CharacterCell {
	public:
		DirtiableCell() : CharacterCell(), t(true) {}
		bool touched() const { return t; }
		void untouch() { t = false; }
		void touch() { t = true; }
		DirtiableCell & operator = ( const CharacterCell & c );
	protected:
		bool t;
	};

	coordinate cursor_y, cursor_x;
	const coordinate h, w;
	std::vector<DirtiableCell> cur_cells;
	std::vector<CharacterCell> new_cells;

	DirtiableCell & cur_at(coordinate y, coordinate x) { return cur_cells[static_cast<std::size_t>(y) * w + x]; }
	CharacterCell & new_at(coordinate y, coordinate x) { return new_cells[static_cast<std::size_t>(y) * w + x]; }
};

Compositor::Compositor(coordinate init_h, coordinate init_w) :
	cursor_y(0U),
	cursor_x(0U),
	h(init_h),
	w(init_w),
	cur_cells(static_cast<std::size_t>(h) * w),
	new_cells(static_cast<std::size_t>(h) * w)
{
}

Compositor::DirtiableCell & 
Compositor::DirtiableCell::operator = ( const CharacterCell & c ) 
{ 
	if (c != *this) {
		CharacterCell::operator =(c); 
		t = true; 
	}
	return *this; 
}

Compositor::~Compositor()
{
}

inline
void
Compositor::repaint_new_to_cur()
{
	for (unsigned row(0); row < h; ++row)
		for (unsigned col(0); col < w; ++col)
			cur_at(row, col) = new_at(row, col);
}

inline
void
Compositor::poke(coordinate y, coordinate x, const CharacterCell & c)
{
	if (y < h && x < w) new_at(y, x) = c;
}

void 
Compositor::move(coordinate y, coordinate x) 
{
	if (cursor_y != y || cursor_x != x) {
		cur_at(cursor_y, cursor_x).touch();
		cursor_y = y;
		cursor_x = x;
		cur_at(cursor_y, cursor_x).touch();
	}
}

/// This is a fairly minimal function for testing whether a particular cell position is within the current cursor, so that it can be displayed marked.
/// \todo TODO: When we gain mark/copy functionality, the cursor will be the entire marked region rather than just one cell.
inline
bool
Compositor::is_marked(coordinate y, coordinate x)
{
	return cursor_y == y && cursor_x == x;
}

/* Realizing onto real devices **********************************************
// **************************************************************************
*/

class Realizer :
	public Compositor
{
public:
	~Realizer();
	Realizer(coordinate y, coordinate x, GraphicsInterface & g, Monospace16x16Font & mf);

	void paint_changed_cells_onto_framebuffer() { do_paint(false); }
	void paint_all_cells_onto_framebuffer() { do_paint(true); }

protected:
	typedef GraphicsInterface::GlyphBitmapHandle GlyphBitmapHandle;
	struct GlyphCacheEntry {
		GlyphCacheEntry(GlyphBitmapHandle ha, uint32_t c, CharacterCell::attribute_type a) : handle(ha), character(c), attributes(a) {}
		GraphicsInterface::GlyphBitmapHandle handle;
		uint32_t character;
		CharacterCell::attribute_type attributes;
	};

	typedef std::list<GlyphCacheEntry> GlyphCache;
	enum { MAX_CACHED_GLYPHS = 256U };
	enum { CHARACTER_PIXEL_WIDTH = 16LU, CHARACTER_PIXEL_HEIGHT = 16LU };

	GraphicsInterface & gdi;
	Monospace16x16Font & font;
	GlyphCache glyph_cache;		///< a recently-used cache of handles to 2-colour bitmaps

	void do_paint(bool changed);

	GlyphBitmapHandle GetGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes);
	GlyphBitmapHandle GetCachedGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes);
	GlyphBitmapHandle MakeFontGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes);
	void AddCachedGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes, GlyphBitmapHandle handle);
};

Realizer::Realizer(
	coordinate y, 
	coordinate x, 
	GraphicsInterface & g,
	Monospace16x16Font & mf
) : 
	Compositor(y / CHARACTER_PIXEL_HEIGHT, x / CHARACTER_PIXEL_WIDTH),
	gdi(g),
	font(mf)
{
}

Realizer::~Realizer()
{
}

inline
Realizer::GlyphBitmapHandle 
Realizer::MakeFontGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes)
{
	GlyphBitmapHandle handle(gdi.MakeGlyphBitmap());
	
	if (const uint16_t * const s = font.ReadGlyph(character, CharacterCell::BOLD & attributes, CharacterCell::FAINT & attributes, CharacterCell::ITALIC & attributes)) {
		for (unsigned row(0U); row < 16U; ++row) handle->Plot(row, s[row]);
	} else
	if (0x20 > character || (0xA0 > character && 0x80 <= character)) {
		// C0 and C1 control characters are boxes.
		handle->Plot(0, 0xFFFF);
		handle->Plot(1, 0xFFFF);
		for (unsigned row(2U); row < 14U; ++row) handle->Plot(row, 0xC003);
		handle->Plot(14, 0xFFFF);
		handle->Plot(15, 0xFFFF);
	} else
	if (0x20 == character || 0xA0 == character || 0x200B == character) {
		// Whitespace is blank.
		for (unsigned row(0U); row < 16U; ++row) handle->Plot(row, 0x0000);
	} else
	{
		// Everything else is greeked.
		handle->Plot(0, 0);
		handle->Plot(1, 0);
		for (unsigned row(2U); row < 14U; ++row) handle->Plot(row, 0x3FFC);
		handle->Plot(14, 0);
		handle->Plot(15, 0);
	}

	if (attributes & CharacterCell::UNDERLINE) {
		handle->Plot(15, 0xFFFF);
	}
	if (attributes & CharacterCell::STRIKETHROUGH) {
		handle->Plot(7, 0xFFFF);
		handle->Plot(8, 0xFFFF);
	}
	if (attributes & CharacterCell::INVERSE) {
		for (unsigned row(0U); row < 16U; ++row) handle->Plot(row, ~handle->Row(row));
	}

	return handle;
}

inline
Realizer::GlyphBitmapHandle 
Realizer::GetCachedGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes)
{
	// A linear search isn't particularly nice, but we maintain the cache in recently-used order.
	for (GlyphCache::iterator i(glyph_cache.begin()); glyph_cache.end() != i; ++i) {
		if (i->character != character || i->attributes != attributes) continue;
		GlyphBitmapHandle handle(i->handle);
		glyph_cache.erase(i);
		glyph_cache.push_front(GlyphCacheEntry(handle, character, attributes));
		return handle;
	}
	return 0;
}

inline
void 
Realizer::AddCachedGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes, GlyphBitmapHandle handle)
{
	while (glyph_cache.size() > MAX_CACHED_GLYPHS) {
		const GlyphCacheEntry e(glyph_cache.back());
		glyph_cache.pop_back();
		gdi.DeleteGlyphBitmap(e.handle);
	}
	glyph_cache.push_back(GlyphCacheEntry(handle, character, attributes));
}

Realizer::GlyphBitmapHandle 
Realizer::GetGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes)
{
	GlyphBitmapHandle handle(GetCachedGlyphBitmap(character, attributes));
	if (!handle) {
		handle = MakeFontGlyphBitmap(character, attributes);
		AddCachedGlyphBitmap(character, attributes, handle);
	}
	return handle;
}

inline
void
Realizer::do_paint(bool force)
{
	const GraphicsInterface::ScreenBitmapHandle screen(gdi.GetScreenBitmap());

	for (unsigned row(0); row < h; ++row) {
		for (unsigned col(0); col < w; ++col) {
			DirtiableCell & c(cur_at(row, col));
			if (!force && !c.touched()) continue;
			// Being invisible is not an alteration to the glyph (which would waste a lot of glyph cache entries) but an alteration to the foreground colour.
			const CharacterCell::attribute_type font_attributes(c.attributes & ~CharacterCell::INVISIBLE);
			const GlyphBitmapHandle handle(GetGlyphBitmap(c.character, font_attributes));
			CharacterCell::colour_type fg(c.foreground), bg(c.background);
			if (CharacterCell::INVISIBLE & c.attributes)
				fg = bg;
			if (is_marked(row, col)) {
				invert(fg);
				invert(bg);
			}
			gdi.BitBLT(screen, handle, row * CHARACTER_PIXEL_HEIGHT, col * CHARACTER_PIXEL_WIDTH, fg, bg);
			c.untouch();
		}
	}
}

/* A virtual terminal back-end **********************************************
// **************************************************************************
*/

namespace {
class VirtualTerminal 
{
public:
	typedef unsigned short coordinate;
	VirtualTerminal(bool display_only, const char * prog, const char * dirname, int dir_fd);
	~VirtualTerminal();

	int query_buffer_fd() const { return buffer_fd; }
	coordinate query_h() const { return h; }
	coordinate query_w() const { return w; }
	coordinate query_screen_y() const { return screen_y; }
	coordinate query_screen_x() const { return screen_x; }
	coordinate query_visible_y() const { return visible_y; }
	coordinate query_visible_x() const { return visible_x; }
	coordinate query_visible_h() const { return visible_h; }
	coordinate query_visible_w() const { return visible_w; }
	coordinate query_cursor_y() const { return cursor_y; }
	coordinate query_cursor_x() const { return cursor_x; }
	void reload();
	void set_position(coordinate y, coordinate x);
	void set_visible_area(coordinate h, coordinate w);
	CharacterCell & at(coordinate y, coordinate x) { return cells[static_cast<std::size_t>(y) * w + x]; }
	void WriteInputMessage(uint32_t);
	void WriteInputUCS24(uint32_t);
	void ClearDeadKeys() { dead_keys.clear(); }

protected:
	typedef std::list<uint32_t> DeadKeysList;
	VirtualTerminal(const VirtualTerminal & c);
	enum { CELL_LENGTH = 16U, HEADER_LENGTH = 16U };

	void move(coordinate y, coordinate x) { cursor_y = y; cursor_x = x; }
	void resize(coordinate, coordinate);
	void keep_visible_area_in_buffer();
	void keep_visible_area_around_cursor();

	FileDescriptorOwner dir_fd;
	const int buffer_fd, input_fd;
	FILE * const buffer_file;
	unsigned short cursor_y, cursor_x, visible_y, visible_x, visible_h, visible_w, h, w, screen_y, screen_x;
	int cursor_state;
	std::vector<CharacterCell> cells;
	DeadKeysList dead_keys;
};
}

VirtualTerminal::VirtualTerminal(
	bool display_only,
	const char * prog, 
	const char * dirname, 
	int d
) :
	dir_fd(d),
	buffer_fd(open_read_at(dir_fd.get(), "display")),
	input_fd(display_only ? -1 : open_writeexisting_at(dir_fd.get(), "input")),
	buffer_file(buffer_fd < 0 ? 0 : fdopen(buffer_fd, "r")),
	cursor_y(0U),
	cursor_x(0U),
	visible_y(0U),
	visible_x(0U),
	visible_h(0U),
	visible_w(0U),
	h(0U),
	w(0U),
	screen_y(0U),
	screen_x(0U),
	cursor_state(-1),
	cells()
{
	if (buffer_fd < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (!display_only && input_fd < 0) {
		const int error(errno);
		close(buffer_fd);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (!buffer_file) {
		const int error(errno);
		close(buffer_fd);
		if (-1 != input_fd) close(input_fd);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
}

VirtualTerminal::~VirtualTerminal()
{
	std::fclose(buffer_file);
	if (-1 != input_fd) close(input_fd);
}

void 
VirtualTerminal::resize(
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
VirtualTerminal::set_position(
	coordinate new_y, 
	coordinate new_x
) {
	screen_y = new_y;
	screen_x = new_x;
}

inline
void 
VirtualTerminal::keep_visible_area_in_buffer()
{
	if (visible_y + visible_h > h) visible_y = h - visible_h;
	if (visible_x + visible_w > w) visible_x = w - visible_w;
}

inline
void 
VirtualTerminal::keep_visible_area_around_cursor()
{
	// When programs repaint the screen the cursor is instantaneously all over the place, leading to the window scrolling all over the shop.
	// But some programs, like vim, make the cursor invisible during the repaint in order to reduce cursor flicker.
	// We take advantage of this by only scrolling the screen to include the cursor position if the cursor is actually visible.
	if (cursor_state > 0) {
		// The window includes the cursor position.
		if (visible_y > cursor_y) visible_y = cursor_y; else if (visible_y + visible_h <= cursor_y) visible_y = cursor_y - visible_h + 1;
		if (visible_x > cursor_x) visible_x = cursor_x; else if (visible_x + visible_w <= cursor_x) visible_x = cursor_x - visible_w + 1;
	}
}

void 
VirtualTerminal::set_visible_area(
	coordinate new_h, 
	coordinate new_w
) {
	if (new_h > h) { visible_h = h; } else { visible_h = new_h; }
	if (new_w > w) { visible_w = w; } else { visible_w = new_w; }
	keep_visible_area_in_buffer();
	keep_visible_area_around_cursor();
}

/// \brief Pull the display buffer from file into the memory buffer, but don't output anything.
inline
void
VirtualTerminal::reload () 
{
	std::fseek(buffer_file, 4, SEEK_SET);
	uint16_t header1[4] = { 0, 0, 0, 0 };
	std::fread(header1, sizeof header1, 1U, buffer_file);
	uint8_t header2[4] = { 0, 0, 0, 0 };
	std::fread(header2, sizeof header2, 1U, buffer_file);
	std::fseek(buffer_file, HEADER_LENGTH, SEEK_SET);
	resize(header1[1], header1[0]);
	move(header1[3], header1[2]);
	const unsigned ctype(header2[0]), cattr(header2[1]);
	if (CursorSprite::VISIBLE != (cattr & CursorSprite::VISIBLE))
		cursor_state = 0;
	else if (CursorSprite::UNDERLINE == ctype)
		cursor_state = 1;
	else
		cursor_state = 2;

	for (unsigned row(0); row < h; ++row) {
		for (unsigned col(0); col < w; ++col) {
			unsigned char b[CELL_LENGTH] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			std::fread(b, sizeof b, 1U, buffer_file);
			uint32_t wc;
			std::memcpy(&wc, &b[8], 4);
			const CharacterCell::attribute_type a(b[12]);
			const CharacterCell::colour_type fg(b[0], b[1], b[2], b[3]);
			const CharacterCell::colour_type bg(b[4], b[5], b[6], b[7]);
			CharacterCell cc(wc, a, fg, bg);
			at(row, col) = cc;
		}
	}
}

void 
VirtualTerminal::WriteInputMessage(uint32_t m)
{
	write(input_fd, &m, sizeof m);
}

static const
struct Combination {
	uint32_t c1, c2, r;
} peculiar_combinations1[] = {
	// Combiner + Combiner => Result
	{	0x00000300, 0x00000300, 0x0000030F	},
	{	0x00000302, 0x00000302, 0x00001DCD	},
	{	0x00000303, 0x00000303, 0x00000360	},
	{	0x00000304, 0x00000304, 0x0000035E	},
	{	0x00000306, 0x00000306, 0x0000035D	},
	{	0x00000307, 0x00000306, 0x00000310	},
	{	0x0000030D, 0x0000030D, 0x0000030E	},
	{	0x00000311, 0x00000311, 0x00000361	},
	{	0x00000313, 0x00000313, 0x00000315	},
	{	0x00000323, 0x00000323, 0x00000324	},
	{	0x00000329, 0x00000329, 0x00000348	},
	{	0x0000032E, 0x0000032E, 0x0000035C	},
	{	0x00000331, 0x00000331, 0x00000347	},
	{	0x00000332, 0x00000332, 0x0000035F	}, 
}, peculiar_combinations2[] = {
	// Combiner + Base => Result
	{	0x00000301, ' ', 0x000000B4	},
	{	0x00000306, ' ', 0x000002D8	},
	{	0x0000030C, ' ', 0x000002C7	},
	{	0x00000327, ' ', 0x000000B8	},
	{	0x00000302, ' ', 0x0000005E	},
	{	0x00000308, ' ', 0x000000A8	},
	{	0x00000307, ' ', 0x000002D9	},
	{	0x0000030B, ' ', 0x000002DD	},
	{	0x00000300, ' ', 0x00000060	},
	{	0x00000304, ' ', 0x000000AF	},
	{	0x00000328, ' ', 0x000002DB	},
	{	0x0000030A, ' ', 0x000002DA	},
	{	0x00000303, ' ', 0x0000007E	},
	{	0x0000032E, ' ', 0x00000000	},
	{	0x00000310, ' ', 0x00000000	},
	{	0x0000032D, ' ', 0x0000A788	},
	{	0x00000313, ' ', 0x000002BC	},
	{	0x00000315, ' ', 0x00000000	},
	{	0x00000326, ' ', 0x00000000	},
	{	0x00000324, ' ', 0x00000000	},
	{	0x00000323, ' ', 0x00000000	},
	{	0x0000035D, ' ', 0x00000000	},
	{	0x0000035C, ' ', 0x00000000	},
	{	0x0000030F, ' ', 0x00000000	},
	{	0x00000361, ' ', 0x00000000	},
	{	0x0000035E, ' ', 0x00000000	},
	{	0x0000035F, ' ', 0x00000000	},
	{	0x00000360, ' ', 0x00000000	},
	{	0x0000030E, ' ', 0x00000000	},
	{	0x00000348, ' ', 0x00000000	},
	{	0x00000347, ' ', 0x00000000	},
	{	0x00000309, ' ', 0x00000000	},
	{	0x0000031B, ' ', 0x00000000	},
	{	0x00000311, ' ', 0x00000000	},
	{	0x00000338, ' ', 0x00002215	},
	{	0x00000332, ' ', 0x00000000	},
	{	0x00000331, ' ', 0x000002CD	},
	{	0x00000325, ' ', 0x00000000	},
	{	0x00000335, ' ', 0x00002212	},
	{	0x0000030D, ' ', 0x000002C8	},
	{	0x00000329, ' ', 0x000002CC	},
	{	0x00000331, '<', 0x00002264	},
	{	0x00000331, '>', 0x00002265	},
	{	0x00000335, 'b', 0x00000180	},
	{	0x00000335, 'B', 0x00000243	},
	{	0x00000335, 'd', 0x00000111	},
	{	0x00000335, 'D', 0x00000110	},
	{	0x00000335, 'g', 0x000001E5	},
	{	0x00000335, 'G', 0x000001E4	},
	{	0x00000335, 'h', 0x00000127	},
	{	0x00000335, 'H', 0x00000126	},
	{	0x00000335, 'i', 0x00000268	},
	{	0x00000335, 'I', 0x00000197	},
	{	0x00000335, 'j', 0x00000249	},
	{	0x00000335, 'J', 0x00000248	},
	{	0x00000335, 'l', 0x0000019A	},
	{	0x00000335, 'L', 0x0000023D	},
	{	0x00000335, 'o', 0x00000275	},
	{	0x00000335, 'O', 0x0000019F	},
	{	0x00000335, 'p', 0x00001D7D	},
	{	0x00000335, 'P', 0x00002C63	},
	{	0x00000335, 'r', 0x0000024D	},
	{	0x00000335, 'R', 0x0000024C	},
	{	0x00000335, 't', 0x00000167	},
	{	0x00000335, 'T', 0x00000166	},
	{	0x00000335, 'u', 0x00000289	},
	{	0x00000335, 'U', 0x00000244	},
	{	0x00000335, 'y', 0x0000024F	},
	{	0x00000335, 'Y', 0x0000024E	},
	{	0x00000335, 'z', 0x000001B6	},
	{	0x00000335, 'Z', 0x000001B5	},
	{	0x00000338, 'a', 0x00002C65	},
	{	0x00000338, 'A', 0x0000023A	},
	{	0x00000338, 'c', 0x0000023C	},
	{	0x00000338, 'C', 0x0000023B	},
	{	0x00000338, 'e', 0x00000247	},
	{	0x00000338, 'E', 0x00000246	},
	{	0x00000338, 'l', 0x00000142	},
	{	0x00000338, 'L', 0x00000141	},
	{	0x00000338, 'm', 0x000020A5	},
	{	0x00000338, 'o', 0x000000F8	},
	{	0x00000338, 'O', 0x000000D8	},
	{	0x00000338, 't', 0x00002C66	},
	{	0x00000338, 'T', 0x0000023E	},
	{	0x00000338, '=', 0x00002260	},
	{	0x00000338, 0x000000B0, 0x00002300	},
};

/// Combine multiple dead keys into different dead keys.
/// This is not Unicode composition; it is a means of typing dead key R (that is not in the common group 2) by typing two dead keys C1 and C2.
/// It therefore applies to the exactly-as-typed sequences; whereas Unicode requires combiners to be sorted by combination class.
/// It is therefore commutative; whereas Unicode composition rules are not.
static inline
bool
combine_peculiar_combiners (
	uint32_t & c1,
	const uint32_t c2
) {
	for (const Combination * p(peculiar_combinations1), *e(peculiar_combinations1 + sizeof peculiar_combinations1/sizeof *peculiar_combinations1); p < e; ++p)  {
		if ((p->c1 == c1 && p->c2 == c2)
		|| (p->c1 == c2 && p->c2 == c1)
	     	) {
			c1 = p->r;
			return true;
		}
	}
	return false;
}

static inline
bool
combine_peculiar_non_combiners (
	const uint32_t c,
	uint32_t & b
) {
	for (const Combination * p(peculiar_combinations2), *e(peculiar_combinations2 + sizeof peculiar_combinations2/sizeof *peculiar_combinations2); p < e; ++p)  {
		if (p->c1 == c && p->c2 == b) {
			b = p->r;
			return true;
		}
	}
	return false;
}

void 
VirtualTerminal::WriteInputUCS24(uint32_t c)
{
	if (UnicodeCategorization::IsMarkEnclosing(c)
	||  UnicodeCategorization::IsMarkNonSpacing(c)
	) {
		// Per ISO 9995-3 there are certain pairs of dead keys that make other dead keys.
		// Per DIN 2137, this happens on the fly as the keys are typed, so we only need to test combining against the preceding key.
		if (dead_keys.empty() || !combine_peculiar_combiners(dead_keys.back(), c))
			dead_keys.push_back(c);
	} else
	if (0x200C == c) {
		// Per DIN 2137, Zero-Width Non-Joiner means emit the dead-key sequence as-is.
		// We must not sort into Unicode combination class order.
		// ZWNJ is essentially a pass-through mechanism for combiners.
		for (DeadKeysList::iterator i(dead_keys.begin()); i != dead_keys.end(); i = dead_keys.erase(i))
			WriteInputMessage(MessageForUCS24(*i));
	} else
	if (!dead_keys.empty()) {
		// Per ISO 9995-3 there are certain C+B=R combinations that apply in addition to the Unicode composition rules.
		for (DeadKeysList::iterator i(dead_keys.begin()); i != dead_keys.end(); )
			if (combine_peculiar_non_combiners(*i, c)) {
				dead_keys.erase(i);
				i = dead_keys.begin();
			} else
				++i;
		// If we have any dead keys left, we emit them standalone, before the composed key (i.e. in original typing order, as best we can).
		// We don't send the raw combiners, as in the ZWNJ case.
		// Instead, we make use of the fact that ISO 9995-3 defines rules for combining with the space character to produce a composed spacing character.
		for (DeadKeysList::iterator i(dead_keys.begin()); i != dead_keys.end(); i = dead_keys.erase(i)) {
			uint32_t s(' ');
			if (combine_peculiar_non_combiners(*i, s))
				WriteInputMessage(MessageForUCS24(s));
			else
				WriteInputMessage(MessageForUCS24(*i));
		}
		// This is the final composed key.
		WriteInputMessage(MessageForUCS24(c));
	} else
	{
		WriteInputMessage(MessageForUCS24(c));
	}
}

/* Sharing the framebuffer with kernel virtual terminals ********************
// **************************************************************************
*/

class KernelVTSubsystemLockout :
	public FileDescriptorOwner
{
public:
	KernelVTSubsystemLockout(int d, int rs, int as);
	~KernelVTSubsystemLockout();
	static int open(const char *);
protected:
	struct vt_mode vtmode;
	long kdmode;
#if defined(__LINUX__) || defined(__linux__)
#if defined(KDGKBMUTE) && defined(KDSKBMUTE)
	// This is the newest mechanism, that doesn't react badly with other things that independently change the KVT mode.
	int kbmute;
#elif defined(K_OFF)
	// This is an older mechanism, that is at least better than setting raw mode.
	long kbmode;
#else
	// Fall back to just setting raw mode when turning the KVT's keyboard input queue off.
	long kbmode;
#endif
#endif
};

KernelVTSubsystemLockout::KernelVTSubsystemLockout(int d, int release, int acquire) : 
	FileDescriptorOwner(d),
	vtmode(),
	kdmode(KD_TEXT)
#if defined(__LINUX__) || defined(__linux__)
	,
#if defined(KDGKBMUTE) && defined(KDSKBMUTE)
	kbmute(1)
#elif defined(K_OFF)
	kbmode(K_XLATE)
#else
	kbmode(K_XLATE)
#endif
#endif
{
	if (-1 != fd) {
		ioctl(fd, VT_GETMODE, &vtmode);
		struct vt_mode m = { VT_PROCESS, 0, static_cast<short>(release), static_cast<char>(acquire), 0 };
		ioctl(fd, VT_SETMODE, &m);
		ioctl(fd, KDGETMODE, &kdmode);
		ioctl(fd, KDSETMODE, KD_GRAPHICS);
#if defined(__LINUX__) || defined(__linux__)
#if defined(KDGKBMUTE) && defined(KDSKBMUTE)
		ioctl(fd, KDGKBMUTE, &kbmute);
		ioctl(fd, KDSKBMUTE, 0);
#elif defined(K_OFF)
		ioctl(fd, KDGKBMODE, &kbmode);
		ioctl(fd, KDSKBMODE, K_OFF);
#else
		ioctl(fd, KDGKBMODE, &kbmode);
		ioctl(fd, KDSKBMODE, K_RAW);
#endif
#endif
	}
}

KernelVTSubsystemLockout::~KernelVTSubsystemLockout()
{
	if (-1 != fd) {
		ioctl(fd, VT_SETMODE, &vtmode);
		ioctl(fd, KDSETMODE, kdmode);
#if defined(__LINUX__) || defined(__linux__)
#if defined(KDGKBMUTE) && defined(KDSKBMUTE)
		ioctl(fd, KDSKBMUTE, kbmute);
#elif defined(K_OFF)
		ioctl(fd, KDSKBMODE, kbmode);
#else
		ioctl(fd, KDSKBMODE, kbmode);
#endif
#endif
	}
}

int 
KernelVTSubsystemLockout::open(const char * dev)
{
	return open_readwriteexisting_at(AT_FDCWD, dev);
}

/* Support routines  ********************************************************
// **************************************************************************
*/

static const CharacterCell::colour_type overscan_fg(0,255,255,255), overscan_bg(0,0,0,0);
static const CharacterCell overscan_blank(' ', 0U, overscan_fg, overscan_bg);

static inline
void
paint_backdrop (
	Compositor & r
) {
	for (unsigned short row(0U); row < r.query_h(); ++row)
		for (unsigned short col(0U); col < r.query_w(); ++col)
			r.poke(row, col, overscan_blank);
}

/// \brief Clip and position the visible portion of the terminal's display buffer.
static inline
void
position (
	Compositor & r, 
	VirtualTerminal & vt
) {
	const unsigned short screen_y(vt.query_h() < r.query_h() ? r.query_h() - vt.query_h() : 0);	// Glue the terminal window to the bottom edge of the realizer screen buffer.
	const unsigned short screen_x(/*vt.query_visible_w() < r.query_w() ? r.query_w() - vt.query_visible_w() :*/ 0);	// Glue the terminal window to the left-hand edge of the realizer screen buffer.
	vt.set_position(screen_y, screen_x);
	vt.set_visible_area(r.query_h() - screen_y, r.query_w() - screen_x);
}

/// \brief Render the terminal's display buffer onto the realizer's screen buffer.
static inline
void
compose (
	Compositor & r, 
	VirtualTerminal & vt
) {
	for (unsigned short row(0U); row < vt.query_visible_h(); ++row)
		for (unsigned short col(0U); col < vt.query_visible_w(); ++col)
			r.poke(row + vt.query_screen_y(), col + vt.query_screen_x(), vt.at(vt.query_visible_y() + row, vt.query_visible_x() + col));
}

static inline
void
set_cursor_pos (
	Compositor & r, 
	VirtualTerminal & vt
) {
	r.move(vt.query_cursor_y() - vt.query_visible_y() + vt.query_screen_y(), vt.query_cursor_x() - vt.query_visible_x() + vt.query_screen_x());
}

/// Actions can be simple transmissions of an input message, or complex procedures with the input method.
static inline
void
Enact (
	const uint32_t cmd,
	KeyboardModifierState & r,
	VirtualTerminal & vt,
	uint32_t v
) {
	switch (cmd & KBDMAP_ACTION_MASK) {
		default:	break;
		case KBDMAP_ACTION_UCS24:
		{
			if (v < 1U) break;
			const uint32_t c(cmd & 0x00FFFFFF);
			vt.WriteInputUCS24(c);
			r.unlatch();
			break;
		}
		case KBDMAP_ACTION_MODIFIER:
		{
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			const uint32_t c(cmd & 0x000000FF);
			r.event(k, c, v);
			break;
		}
		case 0x06000000:
		{
			break;
		}
		case KBDMAP_ACTION_SCREEN:
		{
			if (v < 1U) break;
			const uint32_t s((cmd & 0x00FFFF00) >> 8U);
			vt.ClearDeadKeys();
			vt.WriteInputMessage(MessageForSession(s, r.modifiers()));
			r.unlatch();
			break;
		}
		case KBDMAP_ACTION_SYSTEM:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			vt.ClearDeadKeys();
			vt.WriteInputMessage(MessageForSystemKey(k, r.modifiers()));
			r.unlatch();
			break;
		}
		case KBDMAP_ACTION_CONSUMER:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			vt.ClearDeadKeys();
			vt.WriteInputMessage(MessageForConsumerKey(k, r.modifiers()));
			r.unlatch();
			break;
		}
		case KBDMAP_ACTION_EXTENDED:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			vt.ClearDeadKeys();
			vt.WriteInputMessage(MessageForExtendedKey(k, r.modifiers()));
			r.unlatch();
			break;
		}
		case KBDMAP_ACTION_FUNCTION:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			vt.ClearDeadKeys();
			vt.WriteInputMessage(MessageForFunctionKey(k, r.modifiers()));
			r.unlatch();
			break;
		}
		case KBDMAP_ACTION_FUNCTION1:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			vt.ClearDeadKeys();
			vt.WriteInputMessage(MessageForFunctionKey(k, r.nolevel_nogroup_noctrl_modifiers()));
			r.unlatch();
			break;
		}
	}
}

static inline
std::size_t 
query_parameter (
	const uint32_t cmd,
	const KeyboardModifierState & r
) {
	switch (cmd) {
		default:	return -1U;
		case 'p':	return 0U;
		case 'n':	return r.numable_index();
		case 'c':	return r.capsable_index();
		case 's':	return r.shiftable_index();
		case 'f':	return r.funcable_index();
	}
}

static inline
void
handle_input_event(
	FileDescriptorOwner & event_fd,
	KeyboardModifierState & r, 
	const KeyboardMap & keymap,
	VirtualTerminal & vt
) {
#if defined(__LINUX__) || defined(__linux__)
	input_event b[16];
#else
	unsigned char b[16];
#endif
	const int n(read(event_fd.get(), b, sizeof b));
	if (0 > n) return;
	for (unsigned i(0); i * sizeof *b < static_cast<unsigned>(n); ++i) {
#if defined(__LINUX__) || defined(__linux__)
		const input_event & e(b[i]);
		if (EV_KEY != e.type) continue;
		const uint16_t code(e.code);
		const uint32_t value(e.value);
#else
		uint16_t code(b[i]);
		const bool bit7(code & 0x80);
		code &= 0x7F;
		const unsigned value(bit7 ? 0U : r.is_pressed(code) ? 2U : 1U);
		r.set_pressed(code, !bit7);
#endif
		const uint16_t index(keycode_to_keymap_index(code));
		if (0xFFFF == index) continue;
		const uint8_t row(index >> 8U);
		if (row >= KBDMAP_ROWS) continue;
		const uint8_t col(index & 0xFF);
		if (col >= KBDMAP_COLS) continue;
		const kbdmap_entry & action(keymap[row][col]);
		const std::size_t o(query_parameter(action.cmd, r));
		if (o >= sizeof action.p/sizeof *action.p) continue;
		Enact(action.p[o], r, vt, value);
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_fb_realizer ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	const char * kernel_vt(0);
	bool display_only(false);

	enum { FONTLIST_MEDIUM, FONTLIST_BOLD };
	enum { FONTLIST_UPRIGHT, FONTLIST_ITALIC, FONTLIST_OBLIQUE };
	std::list<std::string> font_names[2][3];
	static const CombinedFont::Font::Weight font_weights[2] = { CombinedFont::Font::MEDIUM, CombinedFont::Font::BOLD };
	static const CombinedFont::Font::Slant font_slants[3] = { CombinedFont::Font::UPRIGHT, CombinedFont::Font::OBLIQUE, CombinedFont::Font::ITALIC };

	try {
		popt::bool_definition display_only_option('\0', "display-only", "Only render the display; do not send input.", display_only);
		popt::string_definition kernel_vt_option('\0', "kernel-vt", "device", "Use the kernel FB sharing protocol via this device.", kernel_vt);
		popt::string_list_definition font_medium_r_option('\0', "font-medium-r", "filename", "Use this font as a medium-upright font.", font_names[FONTLIST_MEDIUM][FONTLIST_UPRIGHT]);
		popt::string_list_definition font_medium_o_option('\0', "font-medium-o", "filename", "Use this font as a medium-oblique font.", font_names[FONTLIST_MEDIUM][FONTLIST_OBLIQUE]);
		popt::string_list_definition font_medium_i_option('\0', "font-medium-i", "filename", "Use this font as a medium-italic font.", font_names[FONTLIST_MEDIUM][FONTLIST_ITALIC]);
		popt::string_list_definition font_bold_r_option('\0', "font-bold-r", "filename", "Use this font as a bold-upright font.", font_names[FONTLIST_MEDIUM][FONTLIST_UPRIGHT]);
		popt::string_list_definition font_bold_o_option('\0', "font-bold-o", "filename", "Use this font as a bold-oblique font.", font_names[FONTLIST_MEDIUM][FONTLIST_OBLIQUE]);
		popt::string_list_definition font_bold_i_option('\0', "font-bold-i", "filename", "Use this font as a bold-italic font.", font_names[FONTLIST_MEDIUM][FONTLIST_ITALIC]);
		popt::definition * top_table[] = {
			&display_only_option,
			&kernel_vt_option,
			&font_medium_r_option,
			&font_medium_o_option,
			&font_medium_i_option,
			&font_bold_r_option,
			&font_bold_o_option,
			&font_bold_i_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "framebuffer event-device kbd-map virtual-terminal");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing framebuffer device file name.");
		throw EXIT_FAILURE;
	}
	const char * fb_filename(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing event device file name.");
		throw EXIT_FAILURE;
	}
	const char * event_filename(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing virtual terminal directory name.");
		throw EXIT_FAILURE;
	}
	const char * keymap_filename(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing keyboard map file name.");
		throw EXIT_FAILURE;
	}
	const char * vt_dirname(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	CombinedFont font;

	for (unsigned w(0U); w < 2U; ++w) {
		for (unsigned s(0U); s < 3U; ++s) {
			const std::list<std::string> & l(font_names[w][s]);
			for (std::list<std::string>::const_iterator n(l.begin()); l.end() != n; ++n)
				LoadFont(prog, font, font_weights[w], font_slants[s], w * 2U + (s > 0U), n->c_str());
		}
	}

	KeyboardMap keymap;

	LoadKeyMap(prog, keymap, keymap_filename);

	FileDescriptorOwner vt_dir_fd(open_dir_at(AT_FDCWD, vt_dirname));
	if (0 > vt_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, vt_dirname, std::strerror(error));
		throw EXIT_FAILURE;
	}
	VirtualTerminal vt(display_only, prog, vt_dirname, vt_dir_fd.release());

	FramebufferIO fb(open_readwriteexisting_at(AT_FDCWD, fb_filename));
	if (fb.get() < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
#if !defined(__LINUX__) && !defined(__linux__)
	KeyboardIO event(open_read_at(AT_FDCWD, event_filename));
#else
	FileDescriptorOwner event(open_read_at(AT_FDCWD, event_filename));
#endif
	if (event.get() < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, event_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct sigaction sa;
	sa.sa_flags=0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler=handle_signal;
	sigaction(SIGWINCH,&sa,NULL);
#if !defined(__LINUX__) && !defined(__linux__)
	// We only need to set handlers for those signals that would otherwise directly terminate the process.
	sa.sa_handler=SIG_IGN;
	sigaction(SIGTERM,&sa,NULL);
	sigaction(SIGINT,&sa,NULL);
	sigaction(SIGHUP,&sa,NULL);
	sigaction(SIGPIPE,&sa,NULL);
#endif

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct kevent p[16];
	{
		std::size_t index(0U);
		EV_SET(&p[index++], vt.query_buffer_fd(), EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_WRITE, 0, 0);
		if (!display_only)
			EV_SET(&p[index++], event.get(), EVFILT_READ, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGUSR1, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGUSR2, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		if (0 > kevent(queue, p, index, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	std::auto_ptr<KernelVTSubsystemLockout> kvt(0);
	if (kernel_vt) {
		FileDescriptorOwner fd(KernelVTSubsystemLockout::open(kernel_vt));
		if (0 > fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, kernel_vt, std::strerror(error));
			throw EXIT_FAILURE;
		}
		kvt.reset(new KernelVTSubsystemLockout(fd.release(), SIGUSR1, SIGUSR2));
	}

	fb.save_and_set_graphics_mode(prog, fb_filename);
#if !defined(__LINUX__) && !defined(__linux__)
	event.save_and_set_code_mode();
#endif

	void * const base(mmap(0, fb.query_size(), PROT_READ|PROT_WRITE, MAP_SHARED, fb.get(), 0));
	if (MAP_FAILED == base) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}

	KeyboardModifierState kbd;
	GraphicsInterface gdi(base, fb.query_size(), fb.query_yres(), fb.query_xres(), fb.query_stride(), fb.query_depth());
	Realizer realizer(fb.query_yres(), fb.query_xres(), gdi, font);

	update_needed = true;
	usr2_signalled = true;

	bool active(true);

	vt.reload();
	while (true) {
		if (terminate_signalled||interrupt_signalled||hangup_signalled) 
			break;
		if (usr1_signalled) 
			active = false;
		if (usr2_signalled) {
			if (!active) 
				realizer.paint_all_cells_onto_framebuffer();
			active = true;
		}
		if (update_needed) {
			update_needed = false;
			paint_backdrop(realizer);
			position(realizer, vt);
			compose(realizer, vt);
			set_cursor_pos(realizer, vt);
			realizer.repaint_new_to_cur();
			if (active)
				realizer.paint_changed_cells_onto_framebuffer();
		}

		const int rc(kevent(queue, p, 0, p, sizeof p/sizeof *p, 0));

		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
			fb.restore();
			kvt.reset(0);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			if (EVFILT_VNODE == e.filter) {
				if (vt.query_buffer_fd() == static_cast<int>(e.ident)) {
					vt.reload();
					update_needed = true;
				}
			}
			if (EVFILT_SIGNAL == e.filter)
				handle_signal(e.ident);
			if (EVFILT_READ == e.filter && event.get() == static_cast<int>(e.ident)) 
				handle_input_event(event, kbd, keymap, vt);
		}
	}

	throw EXIT_SUCCESS;
}
